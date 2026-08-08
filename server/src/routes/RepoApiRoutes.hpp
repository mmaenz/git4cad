#pragma once

#include <crow.h>

#include "config.hpp"
#include "auth/UserStore.hpp"
#include "store/RepoStore.hpp"

namespace api {

class RepoApiRoutes {
public:
    RepoApiRoutes(const g4c::Config& config,
                  auth::UserStore&   users,
                  store::RepoStore&  repos);

    void register_routes(crow::SimpleApp& app);

private:
    const g4c::Config& config_;
    auth::UserStore&   users_;
    store::RepoStore&  repos_;

    [[nodiscard]] std::string bearer_auth(const crow::request& req) const;
    [[nodiscard]] std::string basic_auth(const crow::request& req) const;

    // Each group below registers a self-contained slice of the API —
    // split out of register_routes() so each stays focused and short.
    void register_auth_routes(crow::SimpleApp& app);      // register / login
    void register_repo_crud_routes(crow::SimpleApp& app); // list/create/get/update/delete repo
    void register_member_routes(crow::SimpleApp& app);    // collaborator list/add/remove
    void register_history_routes(crow::SimpleApp& app);   // commit list/get
    void register_content_routes(crow::SimpleApp& app);   // tree/blob browsing
};

} // namespace api
