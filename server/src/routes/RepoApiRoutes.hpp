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
};

} // namespace api
