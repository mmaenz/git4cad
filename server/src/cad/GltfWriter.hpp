#pragma once

#include <filesystem>

#include <TDocStd_Document.hxx>
#include <Standard_Handle.hxx>

namespace cad {

namespace fs = std::filesystem;

/// Write an XDE document as a binary GLB file to output_path.
/// Parent directories are created automatically.
/// Returns true on success.
bool write_glb(const Handle(TDocStd_Document)& doc, const fs::path& output_path);

} // namespace cad
