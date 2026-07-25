#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <TDocStd_Document.hxx>
#include <Standard_Handle.hxx>

namespace cad {

/// Read a FreeCAD FCStd file from an in-memory blob.
/// Extracts the ZIP, parses Document.xml, reads BRep files and applies
/// placements, then builds an XDE document with all shapes.
/// Returns nullopt on any failure.
[[nodiscard]] std::optional<Handle(TDocStd_Document)>
read_fcstd(const std::vector<uint8_t>& data);

} // namespace cad
