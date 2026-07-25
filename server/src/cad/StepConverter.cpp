#include "StepConverter.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <STEPCAFControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDocStd_Document.hxx>
#include <TDF_LabelSequence.hxx>
#include <TopoDS_Shape.hxx>

#include <spdlog/spdlog.h>

namespace cad {

namespace fs = std::filesystem;

namespace {

fs::path write_tmp_file(const std::vector<uint8_t>& data, std::string_view suffix) {
    char tmpl[] = "/tmp/git4cad_XXXXXX";
    const int fd = ::mkstemp(tmpl);
    if (fd < 0) throw std::runtime_error("mkstemp failed");
    ::close(fd);

    const fs::path tmp_path = std::string(tmpl) + std::string(suffix);
    fs::rename(tmpl, tmp_path);

    std::ofstream ofs(tmp_path, std::ios::binary);
    if (!ofs) throw std::runtime_error("Cannot open temp file for writing");
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return tmp_path;
}

} // namespace

std::optional<Handle(TDocStd_Document)> read_step(const std::vector<uint8_t>& data) {
    fs::path tmp_path{};
    try {
        tmp_path = write_tmp_file(data, ".step");
    } catch (const std::exception& e) {
        spdlog::error("StepConverter: temp file error: {}", e.what());
        return std::nullopt;
    }

    struct TmpGuard {
        fs::path p;
        ~TmpGuard() { std::error_code ec; fs::remove(p, ec); }
    } guard{tmp_path};

    Handle(TDocStd_Document) doc{};
    XCAFApp_Application::GetApplication()->NewDocument(
        TCollection_ExtendedString("MDTV-XCAF"), doc);

    STEPCAFControl_Reader reader{};
    reader.SetColorMode(Standard_True);
    reader.SetNameMode(Standard_True);
    reader.SetLayerMode(Standard_True);

    const IFSelect_ReturnStatus stat =
        reader.ReadFile(tmp_path.string().c_str());

    if (stat != IFSelect_RetDone) {
        spdlog::error("StepConverter: ReadFile failed (status={})", static_cast<int>(stat));
        return std::nullopt;
    }

    if (!reader.Transfer(doc)) {
        spdlog::error("StepConverter: Transfer to document failed");
        return std::nullopt;
    }

    // Triangulate all free shapes so RWGltf_CafWriter has mesh data.
    Handle(XCAFDoc_ShapeTool) shape_tool =
        XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    TDF_LabelSequence labels{};
    shape_tool->GetFreeShapes(labels);
    for (const TDF_Label& label : labels) {
        TopoDS_Shape shape{};
        if (shape_tool->GetShape(label, shape) && !shape.IsNull()) {
            BRepMesh_IncrementalMesh mesh(shape, 0.1, Standard_False, 0.5, Standard_True);
            mesh.Perform();
        }
    }

    return doc;
}

} // namespace cad
