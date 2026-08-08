#include "LightGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <BRepGProp_Face.hxx>

#include <IntCurvesFace_ShapeIntersector.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffset_Mode.hxx>
#include <GeomAbs_JoinType.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <ShapeFix_Shape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>

#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>

#include <spdlog/spdlog.h>

namespace cad {

namespace {

// Mesh deflection for the light export. Linear deflection scales with the
// shape's own size (a fixed absolute value is meaningless across wildly
// different part scales — too tight to matter for a large part, too coarse
// to be valid for a tiny one), with a floor no tighter than the full
// export's fixed 0.1. Angular deflection is loosened well past the full
// export's 0.5 rad: for small, curvature-heavy parts (fillets, holes,
// threads) the angular bound — not the linear one — is what actually
// controls triangle count, so a coarser *linear* value alone barely moves
// it unless the angular bound is relaxed too.
constexpr double kLightLinearDeflectionFraction = 0.01; // of bbox diagonal
constexpr double kLightLinearDeflectionFloor    = 0.1;
constexpr double kLightAngularDeflection        = 1.2;

// Shrinkwrap grow/shrink offset distance, as a fraction of the shape's own
// bounding-box diagonal (with an absolute floor so tiny parts still get a
// meaningful offset).
constexpr double kShrinkwrapFraction = 0.02;
constexpr double kShrinkwrapFloor    = 0.5;

double bounding_diagonal(const TopoDS_Shape& shape) {
    Bnd_Box box{};
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return 0.0;

    Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = xmax - xmin;
    const double dy = ymax - ymin;
    const double dz = zmax - zmin;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A face that is never the nearest thing hit by a ray cast from outside the
// shape back toward its own surface is hidden behind other geometry — the
// wall of a hole, pocket, or internal cavity — and is a candidate for
// removal. Faces that *are* their own nearest hit are the true outer shell.
std::vector<TopoDS_Face> find_internal_faces(const TopoDS_Shape& shape, double bbox_diag) {
    std::vector<TopoDS_Face> internal_faces{};
    if (bbox_diag <= Precision::Confusion()) return internal_faces;

    IntCurvesFace_ShapeIntersector intersector{};
    intersector.Load(shape, Precision::Confusion());

    const double ray_reach = bbox_diag * 2.0;

    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(exp.Current());

        Standard_Real umin{}, umax{}, vmin{}, vmax{};
        BRepTools::UVBounds(face, umin, umax, vmin, vmax);
        if (!(umax > umin) || !(vmax > vmin)) continue; // degenerate parametric domain

        const Standard_Real u = (umin + umax) * 0.5;
        const Standard_Real v = (vmin + vmax) * 0.5;

        BRepGProp_Face props(face);
        gp_Pnt sample{};
        gp_Vec normal_vec{};
        props.Normal(u, v, sample, normal_vec);
        if (normal_vec.Magnitude() <= Precision::Confusion()) continue; // degenerate normal

        const gp_Dir outward(normal_vec);
        const gp_Pnt origin = sample.Translated(gp_Vec(outward) * ray_reach);
        const gp_Lin ray(origin, outward.Reversed());

        intersector.Perform(ray, 0.0, ray_reach * 2.0);
        if (!intersector.IsDone() || intersector.NbPnt() == 0) {
            continue; // no hit at all — be conservative, keep the face
        }

        intersector.SortResult();
        const TopoDS_Face& nearest = intersector.Face(1);
        if (!nearest.IsSame(face)) {
            internal_faces.push_back(face);
        }
    }
    return internal_faces;
}

// Removes the given faces and patches the resulting gap by extending
// neighboring faces — this is what actually closes holes/pockets, not a
// capping step bolted on afterward. Best-effort: falls back to the input
// shape on any failure.
TopoDS_Shape remove_internal_features(const TopoDS_Shape& shape,
                                       const std::vector<TopoDS_Face>& internal_faces) {
    if (internal_faces.empty()) return shape;

    try {
        BRepAlgoAPI_Defeaturing defeaturing{};
        defeaturing.SetShape(shape);
        for (const auto& face : internal_faces) {
            defeaturing.AddFaceToRemove(face);
        }
        defeaturing.SetRunParallel(Standard_True);
        defeaturing.Build();

        if (defeaturing.IsDone() && !defeaturing.Shape().IsNull()) {
            return defeaturing.Shape();
        }
    } catch (const Standard_Failure& e) {
        spdlog::warn("LightGeometry: defeaturing failed, keeping pre-defeature shape: {}",
                     e.GetMessageString());
    }
    return shape;
}

// Grows the shape outward then back inward by the same distance
// (morphological "closing") — seals small-scale detail the face
// classification above didn't cleanly catch (fillets, thin slots, surface
// noise) without changing the part's overall size or silhouette.
TopoDS_Shape shrinkwrap(const TopoDS_Shape& shape, double bbox_diag) {
    if (bbox_diag <= Precision::Confusion()) return shape;
    const double offset = std::max(bbox_diag * kShrinkwrapFraction, kShrinkwrapFloor);

    try {
        BRepOffsetAPI_MakeOffsetShape grow{};
        grow.PerformByJoin(shape, offset, Precision::Confusion(),
                            BRepOffset_Skin, Standard_False, Standard_False, GeomAbs_Intersection);
        if (!grow.IsDone() || grow.Shape().IsNull()) return shape;

        BRepOffsetAPI_MakeOffsetShape shrink{};
        shrink.PerformByJoin(grow.Shape(), -offset, Precision::Confusion(),
                              BRepOffset_Skin, Standard_False, Standard_False, GeomAbs_Intersection);
        if (!shrink.IsDone() || shrink.Shape().IsNull()) return shape;

        return shrink.Shape();
    } catch (const Standard_Failure& e) {
        spdlog::warn("LightGeometry: shrinkwrap offset failed, keeping pre-offset shape: {}",
                     e.GetMessageString());
        return shape;
    }
}

// Defeaturing/offset can leave tolerance-level gaps between faces — sew
// them back into one consistent shell, then run a general healing pass so
// the result is a valid, properly-oriented manifold solid.
TopoDS_Shape sew_and_fix(const TopoDS_Shape& shape) {
    TopoDS_Shape result = shape;

    try {
        BRepBuilderAPI_Sewing sewing(Precision::Confusion() * 10.0);
        sewing.Add(result);
        sewing.Perform();
        if (!sewing.SewedShape().IsNull()) result = sewing.SewedShape();
    } catch (const Standard_Failure& e) {
        spdlog::warn("LightGeometry: sewing failed, keeping pre-sew shape: {}", e.GetMessageString());
    }

    try {
        ShapeFix_Shape fixer(result);
        fixer.Perform();
        if (!fixer.Shape().IsNull()) result = fixer.Shape();
    } catch (const Standard_Failure& e) {
        spdlog::warn("LightGeometry: shape healing failed, keeping pre-fix shape: {}",
                     e.GetMessageString());
    }

    return result;
}

int count_triangles(const TopoDS_Shape& shape) {
    int total = 0;
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        TopLoc_Location loc{};
        const Handle(Poly_Triangulation)& tri = BRep_Tool::Triangulation(TopoDS::Face(exp.Current()), loc);
        if (!tri.IsNull()) total += tri->NbTriangles();
    }
    return total;
}

TopoDS_Shape simplify_shape(const TopoDS_Shape& shape) {
    const double bbox_diag = bounding_diagonal(shape);
    const double linear_deflection =
        std::max(bbox_diag * kLightLinearDeflectionFraction, kLightLinearDeflectionFloor);

    TopoDS_Shape result = remove_internal_features(shape, find_internal_faces(shape, bbox_diag));
    result              = shrinkwrap(result, bbox_diag);
    result              = sew_and_fix(result);

    BRepMesh_IncrementalMesh(result, linear_deflection, Standard_False,
                              kLightAngularDeflection, Standard_True).Perform();

    // Defeaturing/offsetting complex STEP surfaces can occasionally
    // *increase* geometric complexity instead of reducing it (e.g. an
    // offset forcing new trimmed patches at surface-surface
    // intersections) — a shape with no internal features to begin with
    // gets no benefit from any of the steps above. Guard against that by
    // also trying the plain original shape re-triangulated at the same
    // coarse deflection (on an independent deep copy, so it can't disturb
    // the fine-detail triangulation the full-detail export still needs)
    // and keeping whichever candidate actually has fewer triangles.
    BRepBuilderAPI_Copy copier(shape, Standard_True, Standard_False);
    TopoDS_Shape baseline = copier.Shape();
    BRepMesh_IncrementalMesh(baseline, linear_deflection, Standard_False,
                              kLightAngularDeflection, Standard_True).Perform();

    return count_triangles(baseline) <= count_triangles(result) ? baseline : result;
}

} // namespace

std::optional<Handle(TDocStd_Document)> make_light_document(const Handle(TDocStd_Document)& src) {
    if (src.IsNull()) return std::nullopt;

    // Rebuilding a new document from GetFreeShapes()/AddShape() (the first
    // approach here) throws away whatever assembly-level instancing the
    // original document has — a part repeated many times normally shares
    // one mesh definition across many placement nodes, and RWGltf_CafWriter
    // relies on that XDE structure to write it once. Losing it can make a
    // "simplified" export *larger* than the full one despite fewer
    // triangles. Instead, mutate `src` in place: only the actual
    // geometry-bearing "simple shape" labels (the unique part/product
    // definitions — not the assembly/reference labels that merely place
    // them) get their shape replaced with a simplified one via SetShape(),
    // so every existing reference to that label picks up the lighter
    // geometry automatically, with the rest of the document (assembly
    // hierarchy, names, colors, instancing) untouched. Safe to do
    // destructively — by the time this runs, the full-detail export has
    // already been written from `src`, and nothing reads it again.
    Handle(XCAFDoc_ShapeTool) shape_tool = XCAFDoc_DocumentTool::ShapeTool(src->Main());
    TDF_LabelSequence labels{};
    shape_tool->GetShapes(labels);
    if (labels.IsEmpty()) return std::nullopt;

    int simplified_count = 0;
    for (const TDF_Label& label : labels) {
        if (!XCAFDoc_ShapeTool::IsSimpleShape(label)) continue;

        TopoDS_Shape shape{};
        if (!shape_tool->GetShape(label, shape) || shape.IsNull()) continue;

        try {
            shape_tool->SetShape(label, simplify_shape(shape));
        } catch (const Standard_Failure& e) {
            spdlog::warn("LightGeometry: simplification failed for a part, keeping its full geometry: {}",
                         e.GetMessageString());
            continue;
        }
        ++simplified_count;
    }

    if (simplified_count == 0) return std::nullopt;
    return src;
}

} // namespace cad
