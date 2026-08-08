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

    // Best-effort recovery for a job the pipeline has never seen (e.g. the
    // original push-time enqueue was lost to a server restart before the
    // GLB finished uploading). No-op if the job is already tracked or the
    // blob can't be read. Safe to call on every status poll.
    void lazy_enqueue_if_untracked(const std::string& user,
                                   const std::string& repo,
                                   const std::string& sha,
                                   const std::string& file_path) const;
};

} // namespace api
