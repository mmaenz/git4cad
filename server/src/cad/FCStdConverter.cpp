#include "FCStdConverter.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <pugixml.hpp>

#include <BRep_Builder.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <Quantity_Color.hxx>
#include <TDF_Label.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDocStd_Document.hxx>

#include <spdlog/spdlog.h>

#include "util/ZipExtract.hpp"

namespace cad {

namespace fs = std::filesystem;

// ─── Types ───────────────────────────────────────────────────────────────────

struct Placement {
    double px{0.0}, py{0.0}, pz{0.0};
    double qw{1.0}, qx{0.0}, qy{0.0}, qz{0.0};
};

struct ObjectDef {
    std::string name{};
    std::string type{};
    std::string brep_file{};   // relative path inside FCStd extract dir
    Placement   placement{};
    bool        has_shape{false};
    bool        has_material{false}; // has a ShapeMaterial property — see
                                      // parse_shape_appearance()

    // App::Link (FreeCAD's cross-document link) fields.
    bool        is_link{false};
    std::string link_file{};        // XLink "file" attribute — relative to
                                     // this document's own directory
    std::string link_object_name{}; // XLink "name" attribute — target object
};

// ─── XML parsing helpers ──────────────────────────────────────────────────────

static Placement parse_placement(const pugi::xml_node& prop_node) {
    Placement p{};
    const pugi::xml_node pp = prop_node.child("PropertyPlacement");
    if (!pp) return p;
    p.px = pp.attribute("Px").as_double(0.0);
    p.py = pp.attribute("Py").as_double(0.0);
    p.pz = pp.attribute("Pz").as_double(0.0);
    // FreeCAD: Q0=w, Q1=x, Q2=y, Q3=z
    p.qw = pp.attribute("Q0").as_double(1.0);
    p.qx = pp.attribute("Q1").as_double(0.0);
    p.qy = pp.attribute("Q2").as_double(0.0);
    p.qz = pp.attribute("Q3").as_double(0.0);
    return p;
}

static std::vector<ObjectDef> parse_document_xml(const fs::path& xml_path) {
    std::vector<ObjectDef> objects{};

    pugi::xml_document doc{};
    const pugi::xml_parse_result result = doc.load_file(xml_path.string().c_str());
    if (!result) {
        spdlog::error("FcstdConverter: cannot parse Document.xml: {}", result.description());
        return objects;
    }

    const pugi::xml_node obj_data = doc.child("Document").child("ObjectData");
    if (!obj_data) return objects;

    for (const pugi::xml_node& obj_node : obj_data.children("Object")) {
        ObjectDef od{};
        od.name = obj_node.attribute("name").as_string("");
        od.type = obj_node.attribute("type").as_string("");

        const pugi::xml_node props = obj_node.child("Properties");
        for (const pugi::xml_node& prop : props.children("Property")) {
            const std::string prop_name = prop.attribute("name").as_string("");
            const std::string prop_type = prop.attribute("type").as_string("");

            if (prop_name == "Shape" && prop_type == "Part::PropertyPartShape") {
                const pugi::xml_node part_node = prop.child("Part");
                if (part_node) {
                    od.brep_file  = part_node.attribute("file").as_string("");
                    od.has_shape  = !od.brep_file.empty();
                }
            } else if (prop_name == "Placement") {
                od.placement = parse_placement(prop);
            } else if (prop_name == "LinkedObject" && prop_type == "App::PropertyXLink") {
                const pugi::xml_node xlink_node = prop.child("XLink");
                if (xlink_node) {
                    od.link_file        = xlink_node.attribute("file").as_string("");
                    od.link_object_name = xlink_node.attribute("name").as_string("");
                    od.is_link           = !od.link_file.empty();
                }
            } else if (prop_name == "ShapeMaterial" && prop_type == "Materials::PropertyMaterial") {
                od.has_material = true;
            }
        }
        objects.push_back(std::move(od));
    }

    return objects;
}

// ─── Shape building ──────────────────────────────────────────────────────────

static std::optional<TopoDS_Shape> load_brep(const fs::path& extract_dir,
                                              const std::string& brep_file) {
    const fs::path brep_path = extract_dir / brep_file;
    if (!fs::exists(brep_path)) {
        spdlog::warn("FcstdConverter: BRep file not found: {}", brep_path.string());
        return std::nullopt;
    }

    TopoDS_Shape shape{};
    BRep_Builder builder{};
    std::ifstream in(brep_path);
    if (!in) return std::nullopt;

    // BRepTools::Read returns void in OCCT 7.5; check shape validity instead.
    BRepTools::Read(shape, in, builder);
    if (shape.IsNull()) {
        spdlog::warn("FcstdConverter: BRepTools::Read produced null shape for '{}'",
                     brep_path.string());
        return std::nullopt;
    }
    return shape;
}

static void apply_placement(TopoDS_Shape& shape, const Placement& p) {
    gp_Trsf trsf{};
    const gp_Quaternion quat(p.qx, p.qy, p.qz, p.qw);
    trsf.SetRotation(quat);
    trsf.SetTranslationPart(gp_Vec(p.px, p.py, p.pz));
    const TopLoc_Location loc(trsf);
    shape.Move(loc);
}

// Resolves an XLink's "file" attribute (relative to this document's own
// directory) to a repo-relative path, normalizing any "..".
static std::string resolve_link_path(const std::string& doc_repo_path,
                                      const std::string& link_file) {
    const fs::path parent_dir = fs::path(doc_repo_path).parent_path();
    const fs::path resolved   = parent_dir.empty() ? fs::path(link_file)
                                                    : parent_dir / link_file;
    return resolved.lexically_normal().generic_string();
}

// A shape plus its visual (diffuse) color, if FreeCAD assigned one.
struct ColoredShape {
    TopoDS_Shape                         shape{};
    std::optional<std::array<double, 3>> color{}; // RGB, 0..1
};

namespace {

std::optional<uint32_t> read_u32(std::ifstream& in) {
    uint32_t v{};
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (!in) return std::nullopt;
    return v;
}

// FreeCAD packs each color as 4 bytes: alpha, red, green, blue.
std::optional<std::array<double, 3>> read_rgb(std::ifstream& in) {
    uint8_t buf[4]{};
    in.read(reinterpret_cast<char*>(buf), sizeof(buf));
    if (!in) return std::nullopt;
    return std::array<double, 3>{buf[1] / 255.0, buf[2] / 255.0, buf[3] / 255.0};
}

} // namespace

// Reads FreeCAD's binary "ShapeAppearance" stream and returns each entry's
// diffuse color, in file order. Objects are matched to entries positionally
// (see ObjectDef::has_material / its use below) — reverse-engineered from
// real FCStd output (FreeCAD's serialization source wasn't available to
// consult directly), so this is deliberately conservative: any parse
// failure just yields fewer colors than objects, which the caller already
// treats as "no color for the rest" rather than an error.
//
// Per entry: ambient/diffuse/specular/emissive colors (4 bytes ARGB each),
// 16 bytes of shininess/transparency (unused here), then a length-prefixed
// UUID string (a material-library cross-reference, empty when the color was
// custom-picked rather than from a named preset — also unused here, since
// matching is purely positional).
static std::vector<std::array<double, 3>> parse_shape_appearance(const fs::path& extract_dir) {
    std::vector<std::array<double, 3>> colors{};

    std::ifstream in(extract_dir / "ShapeAppearance", std::ios::binary);
    if (!in) return colors; // no appearance data — not an error, just untextured

    const auto count = read_u32(in);
    if (!count) return colors;

    for (uint32_t i = 0; i < *count; ++i) {
        const auto ambient  = read_rgb(in);
        const auto diffuse  = read_rgb(in);
        const auto specular = read_rgb(in);
        const auto emissive = read_rgb(in);
        if (!ambient || !diffuse || !specular || !emissive) break;

        in.seekg(16, std::ios::cur); // shininess + transparency, unused
        const auto uuid_len = read_u32(in);
        if (!uuid_len) break;
        in.seekg(*uuid_len, std::ios::cur); // material UUID string, unused
        if (!in) break;

        colors.push_back(*diffuse);
    }
    return colors;
}

// Extracts this document's own shapes (with placements and any FreeCAD
// diffuse color applied) and its App::Link references from an
// already-extracted FCStd directory. Returns false (and logs) if
// Document.xml is missing.
static bool collect_shapes_and_links(const fs::path&             extract_dir,
                                      const std::string&          file_path,
                                      std::vector<ColoredShape>&  out_shapes,
                                      std::vector<FcstdLinkRef>&  out_links) {
    const fs::path xml_path = extract_dir / "Document.xml";
    if (!fs::exists(xml_path)) {
        spdlog::error("FcstdConverter: Document.xml not found in FCStd");
        return false;
    }

    const std::vector<ObjectDef> objects = parse_document_xml(xml_path);
    const std::vector<std::array<double, 3>> appearance_colors =
        parse_shape_appearance(extract_dir);
    out_shapes.reserve(objects.size());

    std::size_t material_index = 0;
    for (const ObjectDef& od : objects) {
        std::optional<std::array<double, 3>> color{};
        if (od.has_material) {
            if (material_index < appearance_colors.size()) {
                color = appearance_colors[material_index];
            }
            ++material_index;
        }

        if (od.has_shape) {
            auto maybe_shape = load_brep(extract_dir, od.brep_file);
            if (!maybe_shape) continue;

            TopoDS_Shape shape = *maybe_shape;
            apply_placement(shape, od.placement);
            out_shapes.push_back(ColoredShape{std::move(shape), color});
        } else if (od.is_link) {
            out_links.push_back(FcstdLinkRef{
                resolve_link_path(file_path, od.link_file),
                od.placement.px, od.placement.py, od.placement.pz,
                od.placement.qw, od.placement.qx, od.placement.qy, od.placement.qz
            });
        }
    }
    return true;
}

// Combines multiple shapes into one compound; returns the lone shape as-is
// when there's only one.
static TopoDS_Shape build_compound(const std::vector<ColoredShape>& shapes) {
    if (shapes.size() == 1) return shapes.front().shape;

    TopoDS_Compound compound{};
    BRep_Builder builder{};
    builder.MakeCompound(compound);
    for (const auto& s : shapes) {
        builder.Add(compound, s.shape);
    }
    return compound;
}

// Triangulates `final_shape` and builds an XDE document containing `shapes`
// (added individually so each keeps its own label/placement/color, unlike
// `final_shape` which is only used for the single-shape/meshing case).
static Handle(TDocStd_Document) build_xde_document(const std::vector<ColoredShape>& shapes,
                                                     const TopoDS_Shape& final_shape) {
    BRepMesh_IncrementalMesh mesh(final_shape, 0.1, Standard_False, 0.5, Standard_True);
    mesh.Perform();

    Handle(TDocStd_Document) doc{};
    XCAFApp_Application::GetApplication()->NewDocument(
        TCollection_ExtendedString("MDTV-XCAF"), doc);

    Handle(XCAFDoc_ShapeTool) shape_tool =
        XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) color_tool =
        XCAFDoc_DocumentTool::ColorTool(doc->Main());

    auto add_with_color = [&](const TopoDS_Shape& shape, const auto& color) {
        const TDF_Label label = shape_tool->AddShape(shape);
        if (color) {
            color_tool->SetColor(
                label,
                Quantity_Color((*color)[0], (*color)[1], (*color)[2], Quantity_TOC_RGB),
                XCAFDoc_ColorSurf);
        }
    };

    if (shapes.size() == 1) {
        add_with_color(final_shape, shapes.front().color);
    } else {
        for (const auto& s : shapes) {
            add_with_color(s.shape, s.color);
        }
        shape_tool->UpdateAssemblies();
    }
    return doc;
}

// ─── Public API ──────────────────────────────────────────────────────────────

std::optional<FcstdResult> read_fcstd(const std::vector<uint8_t>& data,
                                       const std::string&          file_path) {
    // Create a unique temp directory for extraction
    char tmpl[] = "/tmp/git4cad_fcstd_XXXXXX";
    if (!::mkdtemp(tmpl)) {
        spdlog::error("FcstdConverter: mkdtemp failed");
        return std::nullopt;
    }
    const fs::path extract_dir{tmpl};

    struct DirGuard {
        fs::path p;
        ~DirGuard() { std::error_code ec; fs::remove_all(p, ec); }
    } guard{extract_dir};

    if (!util::extract_zip(data, extract_dir)) {
        spdlog::error("FcstdConverter: ZIP extraction failed");
        return std::nullopt;
    }

    std::vector<ColoredShape>  shapes{};
    std::vector<FcstdLinkRef>  links{};
    if (!collect_shapes_and_links(extract_dir, file_path, shapes, links)) {
        return std::nullopt;
    }

    if (shapes.empty()) {
        spdlog::warn("FcstdConverter: no shapes found in FCStd file");
        return std::nullopt;
    }

    const TopoDS_Shape final_shape = build_compound(shapes);
    const Handle(TDocStd_Document) doc = build_xde_document(shapes, final_shape);

    return FcstdResult{doc, std::move(links)};
}

} // namespace cad
