#include "FcstdConverter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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

#include <XCAFApp_Application.hxx>
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

// ─── Public API ──────────────────────────────────────────────────────────────

std::optional<Handle(TDocStd_Document)> read_fcstd(const std::vector<uint8_t>& data) {
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

    const fs::path xml_path = extract_dir / "Document.xml";
    if (!fs::exists(xml_path)) {
        spdlog::error("FcstdConverter: Document.xml not found in FCStd");
        return std::nullopt;
    }

    const std::vector<ObjectDef> objects = parse_document_xml(xml_path);

    std::vector<TopoDS_Shape> shapes{};
    shapes.reserve(objects.size());

    for (const ObjectDef& od : objects) {
        if (!od.has_shape) continue;

        auto maybe_shape = load_brep(extract_dir, od.brep_file);
        if (!maybe_shape) continue;

        TopoDS_Shape shape = *maybe_shape;
        apply_placement(shape, od.placement);
        shapes.push_back(std::move(shape));
    }

    if (shapes.empty()) {
        spdlog::warn("FcstdConverter: no shapes found in FCStd file");
        return std::nullopt;
    }

    // Build a compound if multiple shapes
    TopoDS_Shape final_shape{};
    if (shapes.size() == 1) {
        final_shape = shapes[0];
    } else {
        TopoDS_Compound compound{};
        BRep_Builder builder{};
        builder.MakeCompound(compound);
        for (const auto& s : shapes) {
            builder.Add(compound, s);
        }
        final_shape = compound;
    }

    // Triangulate so RWGltf_CafWriter has mesh data to write.
    BRepMesh_IncrementalMesh mesh(final_shape, 0.1, Standard_False, 0.5, Standard_True);
    mesh.Perform();

    Handle(TDocStd_Document) doc{};
    XCAFApp_Application::GetApplication()->NewDocument(
        TCollection_ExtendedString("MDTV-XCAF"), doc);

    Handle(XCAFDoc_ShapeTool) shape_tool =
        XCAFDoc_DocumentTool::ShapeTool(doc->Main());

    if (shapes.size() == 1) {
        shape_tool->AddShape(final_shape);
    } else {
        for (const auto& s : shapes) {
            shape_tool->AddShape(s);
        }
        shape_tool->UpdateAssemblies();
    }

    return doc;
}

} // namespace cad
