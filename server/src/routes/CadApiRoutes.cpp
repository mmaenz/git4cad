#include "CadApiRoutes.hpp"

#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "git/Repository.hpp"

namespace api {

using json = nlohmann::json;

namespace {

void add_cors(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
}

std::string status_string(cad::JobStatus s) {
    switch (s) {
        case cad::JobStatus::Pending:    return "pending";
        case cad::JobStatus::Processing: return "processing";
        case cad::JobStatus::Ready:      return "ready";
        case cad::JobStatus::Error:      return "error";
    }
    return "unknown";
}

} // namespace

CadApiRoutes::CadApiRoutes(const g4c::Config&        config,
                             cad::CadPipeline&         pipeline,
                             storage::SeaweedFsClient& seaweedfs)
    : config_(config), pipeline_(pipeline), seaweedfs_(seaweedfs) {}

void CadApiRoutes::lazy_enqueue_if_untracked(const std::string& user,
                                              const std::string& repo,
                                              const std::string& sha,
                                              const std::string& file_path) const {
    if (pipeline_.has_tracked_job(user, repo, sha, file_path)) return;

    const auto repo_path = config_.repos_dir() / user / (repo + ".git");
    auto maybe_repo = git::Repository::open(repo_path);
    if (!maybe_repo) return;

    auto blob = maybe_repo->read_blob_at_commit(sha, file_path);
    if (!blob) return;

    cad::CadJob job{};
    job.user      = user;
    job.repo      = repo;
    job.sha       = sha;
    job.file_path = file_path;
    job.blob_data = std::move(*blob);

    spdlog::info("CadApiRoutes: lazily re-enqueuing untracked job {}/{}/{}/{}",
                 user, repo, sha, file_path);
    pipeline_.enqueue(std::move(job));
}

void CadApiRoutes::register_routes(crow::SimpleApp& app) {

    // ── GET /api/v1/repos/:user/:repo/glb/:sha/* ────────────────────────────
    // Crow's <path> is greedy, so a separate /status route would never be
    // reached. We handle both status queries and GLB redirects in one handler
    // by detecting the "/status" suffix ourselves.
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/glb/<string>/<path>")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request&,
            crow::response& res,
            const std::string& user, const std::string& repo,
            const std::string& sha,  std::string file_path) {

        // Strip trailing "/status" to identify status-only requests.
        static constexpr std::string_view kStatusSuffix = "/status";
        const bool is_status = file_path.size() > kStatusSuffix.size()
            && file_path.ends_with(kStatusSuffix);
        if (is_status)
            file_path = file_path.substr(0, file_path.size() - kStatusSuffix.size());

        lazy_enqueue_if_untracked(user, repo, sha, file_path);
        const cad::JobStatus s = pipeline_.status(user, repo, sha, file_path);

        if (is_status) {
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            add_cors(res);
            res.body = json{{"status", status_string(s)}}.dump();
            res.end();
            return;
        }

        if (s != cad::JobStatus::Ready) {
            res.code = 202;
            res.add_header("Content-Type", "application/json");
            add_cors(res);
            res.body = json{{"status", status_string(s)}}.dump();
            res.end();
            return;
        }

        // Redirect browser directly to SeaweedFS via nginx proxy.
        const std::string url = seaweedfs_.public_url(user, repo, sha, file_path);
        res.code = 302;
        res.add_header("Location", url);
        add_cors(res);
        res.end();
    });
}

} // namespace api
