#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <crow.h>

#include "config.hpp"
#include "auth/UserStore.hpp"
#include "cad/Pipeline.hpp"
#include "git/Repository.hpp"
#include "storage/SeaweedFsClient.hpp"
#include "store/RepoStore.hpp"

namespace api {

namespace fs = std::filesystem;

class GitHttpRoutes {
public:
    GitHttpRoutes(const g4c::Config&              config,
                  const auth::UserStore&           users,
                  cad::CadPipeline&               pipeline,
                  const storage::SeaweedFsClient& seaweedfs,
                  store::RepoStore&               repos);

    void register_routes(crow::SimpleApp& app);

private:
    using RefMap = std::map<std::string, std::string>;

    const g4c::Config&              config_;
    const auth::UserStore&          users_;
    cad::CadPipeline&               pipeline_;
    const storage::SeaweedFsClient& seaweedfs_;
    store::RepoStore&               repos_;

    bool authenticate(const crow::request& req, std::string& out_user) const;

    void handle_git_request(const crow::request& req,
                             crow::response&      res,
                             const std::string&   user,
                             const std::string&   repo,
                             const std::string&   git_path) const;

    void handle_lfs(const crow::request& req,
                    crow::response&      res,
                    bool                 authed,
                    bool                 can_read,
                    bool                 can_push,
                    const std::string&   user,
                    const std::string&   repo,
                    const std::string&   lfs_path) const;

    void scan_post_receive(const std::string& user,
                           const std::string& repo_name,
                           const fs::path&    repo_path,
                           const RefMap&      refs_before) const;

    void enqueue_cad_for_ref(git::Repository&   repo_obj,
                              const std::string& user,
                              const std::string& repo_name,
                              const std::string& old_sha,
                              const std::string& new_sha) const;
};

} // namespace api
