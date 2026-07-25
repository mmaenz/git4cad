#include "GltfWriter.hpp"

#include <filesystem>
#include <stdexcept>

#include <RWGltf_CafWriter.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <Message_ProgressRange.hxx>
#include <TCollection_AsciiString.hxx>

#include <spdlog/spdlog.h>

namespace cad {

bool write_glb(const Handle(TDocStd_Document)& doc, const fs::path& output_path) {
    if (doc.IsNull()) {
        spdlog::error("GltfWriter: document handle is null");
        return false;
    }

    std::error_code ec{};
    fs::create_directories(output_path.parent_path(), ec);
    if (ec) {
        spdlog::error("GltfWriter: cannot create directories for '{}': {}",
                      output_path.string(), ec.message());
        return false;
    }

    const TCollection_AsciiString out_str(output_path.string().c_str());
    RWGltf_CafWriter writer(out_str, Standard_True /*binary GLB*/);

    TColStd_IndexedDataMapOfStringString metadata{};
    const Standard_Boolean ok =
        writer.Perform(doc, metadata, Message_ProgressRange());

    if (!ok) {
        spdlog::error("GltfWriter: RWGltf_CafWriter::Perform failed for '{}'",
                      output_path.string());
        return false;
    }

    return true;
}

} // namespace cad
