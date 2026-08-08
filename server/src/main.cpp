#include <filesystem>
#include <memory>

#include <crow.h>
#include <spdlog/spdlog.h>

#include "config.hpp"
#include "auth/UserStore.hpp"
#include "cad/Pipeline.hpp"
#include "git/Repository.hpp"
#include "storage/SeaweedFsClient.hpp"
#include "store/RepoStore.hpp"
#include "routes/CadApiRoutes.hpp"
#include "routes/GitHttpRoutes.hpp"
#include "routes/RepoApiRoutes.hpp"

namespace fs = std::filesystem;

namespace {

void create_data_dirs(const g4c::Config& cfg) {
    fs::create_directories(cfg.repos_dir());
    fs::create_directories(cfg.users_db().parent_path());
    fs::create_directories(cfg.repos_db().parent_path());
}

void setup_logging(bool debug) {
    spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
}

} // namespace

int main() {
    const g4c::Config cfg = g4c::Config::from_env();

    setup_logging(cfg.debug);
    spdlog::info("git4cad starting on {}:{}", cfg.host, cfg.port);
    spdlog::info("Data directory: {}", cfg.data_dir.string());
    spdlog::info("SeaweedFS filer: {}", cfg.seaweedfs_filer_url);

    create_data_dirs(cfg);

    // Initialize libcurl before any threads start.
    storage::SeaweedFsClient::global_init();

    // Initialize libgit2.
    git::Repository::init();
    spdlog::info("libgit2 initialized");

    // Core services.
    storage::SeaweedFsClient seaweedfs{cfg.seaweedfs_filer_url, cfg.seaweedfs_public_prefix};

    auto users_store = auth::UserStore::open(cfg.users_db());
    if (!users_store) {
        spdlog::critical("Failed to open user database at '{}'", cfg.users_db().string());
        return 1;
    }
    auto repos_store = store::RepoStore::open(cfg.repos_db());
    if (!repos_store) {
        spdlog::critical("Failed to open repo database at '{}'", cfg.repos_db().string());
        return 1;
    }

    auth::UserStore&  users      = *users_store;
    store::RepoStore& repo_store = *repos_store;
    cad::CadPipeline  pipeline{seaweedfs, cfg.cad_workers};
    spdlog::info("CAD pipeline started with {} workers", cfg.cad_workers);

    // HTTP application.
    crow::SimpleApp app{};

    // Route handlers — registered in priority order:
    // API routes first (more specific), git catch-all last.
    api::RepoApiRoutes repo_routes{cfg, users, repo_store};
    api::CadApiRoutes  cad_routes{cfg, pipeline, seaweedfs};
    api::GitHttpRoutes git_routes{cfg, users, pipeline, seaweedfs, repo_store};

    repo_routes.register_routes(app);
    cad_routes.register_routes(app);
    git_routes.register_routes(app); // catch-all — must be last

    // Global OPTIONS preflight.
    CROW_ROUTE(app, "/<path>")
    .methods(crow::HTTPMethod::Options)
    ([](const crow::request&, crow::response& res, std::string) {
        res.code = 204;
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.end();
    });

    app.bindaddr(cfg.host)
       .port(cfg.port)
       .multithreaded()
       .run();

    spdlog::info("Server stopped");
    git::Repository::shutdown();
    storage::SeaweedFsClient::global_shutdown();

    return 0;
}
