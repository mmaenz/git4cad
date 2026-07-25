#pragma once

#include <crow.h>

#include "config.hpp"
#include "cad/Pipeline.hpp"
#include "storage/SeaweedFsClient.hpp"

namespace api {

class CadApiRoutes {
public:
    CadApiRoutes(const g4c::Config&        config,
                 cad::CadPipeline&         pipeline,
                 storage::SeaweedFsClient& seaweedfs);

    void register_routes(crow::SimpleApp& app);

private:
    const g4c::Config&        config_;
    cad::CadPipeline&         pipeline_;
    storage::SeaweedFsClient& seaweedfs_;
};

} // namespace api
