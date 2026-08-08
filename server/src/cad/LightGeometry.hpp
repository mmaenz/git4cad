#pragma once

#include <optional>

#include <TDocStd_Document.hxx>
#include <Standard_Handle.hxx>

namespace cad {

/// Turns an already-parsed CAD document into a lightweight stand-in *in
/// place*: every geometry-bearing "simple shape" label (the document's
/// unique part/product definitions, as opposed to the assembly/reference
/// labels that merely place them) is run through an OpenCASCADE
/// simplification pipeline (internal-feature removal, a grow/shrink
/// "shrinkwrap" pass, sewing, and shape healing — suppressing
/// holes/pockets/internal cavities while preserving the part's true outer
/// silhouette) and re-triangulated at a coarser deflection, then written
/// back onto that same label. The document's assembly hierarchy, names,
/// colors, and — crucially — any instancing (a part repeated many times
/// still shares one simplified definition, same as it shared one
/// full-detail definition before) are left untouched.
///
/// Mutates and returns `src` itself — safe to call only once the
/// full-detail export has already been written from it, since nothing
/// about `src` survives this call unchanged. No re-reading of the original
/// CAD file is involved. Returns nullopt if `src` has no simple-shape
/// labels to simplify.
[[nodiscard]] std::optional<Handle(TDocStd_Document)>
make_light_document(const Handle(TDocStd_Document)& src);

} // namespace cad
