#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <TDocStd_Document.hxx>
#include <Standard_Handle.hxx>

namespace cad {

/// A FreeCAD App::Link reference discovered in a document — not resolved or
/// followed server-side. The frontend fetches the linked file's own
/// (independently converted) GLB and composes it into the scene itself,
/// positioned by `placement`.
struct FcstdLinkRef {
    std::string file{};                             // repo-relative path
    double      px{0.0}, py{0.0}, pz{0.0};           // link's own placement
    double      qw{1.0}, qx{0.0}, qy{0.0}, qz{0.0};  // (position + quaternion)
};

struct FcstdResult {
    Handle(TDocStd_Document)  doc{};
    std::vector<FcstdLinkRef> links{};
};

/// Read a FreeCAD FCStd file from an in-memory blob.
/// Extracts the ZIP, parses Document.xml, reads BRep files and applies
/// placements, then builds an XDE document from this file's own shapes.
///
/// `file_path` is this file's own repo-relative path — used to resolve
/// relative App::Link "file" references to repo-relative paths.
///
/// App::Link objects are never followed or merged here — they're reported
/// in the returned `links` list for the caller (ultimately the frontend) to
/// resolve. Returns nullopt on any failure.
[[nodiscard]] std::optional<FcstdResult>
read_fcstd(const std::vector<uint8_t>& data, const std::string& file_path);

} // namespace cad
