#pragma once

#include <filesystem>
#include <string>

#include <TDocStd_Document.hxx>
#include <Standard_Handle.hxx>

namespace cad {

namespace fs = std::filesystem;

/// Write an XDE document as a binary GLB file to output_path.
/// Parent directories are created automatically.
/// `links_json`, if non-empty, is embedded as glTF asset.extras.g4c_links —
/// a JSON array of {file, px,py,pz, qw,qx,qy,qz} the frontend reads to fetch
/// and compose linked FCStd files' own GLBs into the scene.
/// Returns true on success.
bool write_glb(const Handle(TDocStd_Document)& doc, const fs::path& output_path,
                const std::string& links_json = "");

} // namespace cad
