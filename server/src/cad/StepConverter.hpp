#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <TDocStd_Document.hxx>
#include <Standard_Handle.hxx>

namespace cad {

/// Read a STEP file from an in-memory blob and return an XDE document.
/// Returns nullopt on read/transfer failure.
[[nodiscard]] std::optional<Handle(TDocStd_Document)>
read_step(const std::vector<uint8_t>& data);

} // namespace cad
