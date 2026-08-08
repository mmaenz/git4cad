#include "RepoApiRoutes.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "git/Repository.hpp"
#include "util/Base64.hpp"

namespace api {

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

void add_cors(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, PUT, DELETE, OPTIONS");
}

crow::response json_response(int code, const json& body) {
    crow::response res{code, body.dump()};
    res.add_header("Content-Type", "application/json");
    add_cors(res);
    return res;
}

crow::response error_response(int code, std::string_view msg) {
    return json_response(code, json{{"error", msg}});
}

std::string clone_url(const g4c::Config& cfg,
                      const std::string& user,
                      const std::string& repo) {
    const std::string base = cfg.public_url.empty()
        ? ("http://" + cfg.host + ":" + std::to_string(cfg.port))
        : cfg.public_url;
    return base + "/" + user + "/" + repo + ".git";
}

json repo_info(const g4c::Config& cfg,
               const std::string& user,
               const std::string& repo_name,
               const fs::path&    repo_path,
               bool               is_priv       = false,
               bool               z_up          = false,
               bool               resolve_links = false) {
    std::string default_branch = "main";
    bool        is_empty       = true;

    if (auto r = git::Repository::open(repo_path)) {
        default_branch = r->default_branch();
        is_empty       = r->is_empty();
    }

    return json{
        {"name",           repo_name},
        {"owner",          user},
        {"description",    ""},
        {"clone_url",      clone_url(cfg, user, repo_name)},
        {"default_branch", default_branch},
        {"empty",          is_empty},
        {"private",        is_priv},
        {"z_up",           z_up},
        {"resolve_links",  resolve_links}
    };
}

} // namespace

// ─── RepoApiRoutes ──────────────────────────────────────────────────────────

RepoApiRoutes::RepoApiRoutes(const g4c::Config& config,
                              auth::UserStore&   users,
                              store::RepoStore&  repos)
    : config_(config), users_(users), repos_(repos) {}

std::string RepoApiRoutes::bearer_auth(const crow::request& req) const {
    const std::string hdr = req.get_header_value("Authorization");
    if (hdr.size() > 7 && hdr.substr(0, 7) == "Bearer ") {
        const std::string token = hdr.substr(7);
        const std::size_t colon = token.find(':');
        if (colon != std::string::npos) {
            const std::string user     = token.substr(0, colon);
            const std::string real_tok = token.substr(colon + 1);
            if (users_.validate_token(user, real_tok)) return user;
        }
        const std::string xuser = req.get_header_value("X-Username");
        if (!xuser.empty() && users_.validate_token(xuser, token)) return xuser;
    }
    return {};
}

std::string RepoApiRoutes::basic_auth(const crow::request& req) const {
    const std::string hdr = req.get_header_value("Authorization");
    if (hdr.size() > 6 && hdr.substr(0, 6) == "Basic ") {
        const auto decoded = util::base64_decode(hdr.substr(6));
        return std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    }
    return {};
}

void RepoApiRoutes::register_routes(crow::SimpleApp& app) {
    // ── OPTIONS preflight ───────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/<path>")
    .methods(crow::HTTPMethod::Options)
    ([](const crow::request&, crow::response& res, std::string) {
        res.code = 204;
        add_cors(res);
        res.end();
    });

    register_auth_routes(app);
    register_repo_crud_routes(app);
    register_member_routes(app);
    register_history_routes(app);
    register_content_routes(app);
}

void RepoApiRoutes::register_auth_routes(crow::SimpleApp& app) {
    // ── POST /api/v1/users ──────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/users")
    .methods(crow::HTTPMethod::Post)
    ([this](const crow::request& req) -> crow::response {
        json body{};
        try { body = json::parse(req.body); }
        catch (...) { return error_response(400, "Invalid JSON"); }

        const std::string username = body.value("username", "");
        const std::string password = body.value("password", "");
        if (username.empty() || password.empty())
            return error_response(400, "username and password required");

        const auto token = users_.create_user(username, password);
        if (!token) return error_response(409, "User already exists");

        return json_response(201, json{{"username", username}, {"token", *token}});
    });

    // ── POST /api/v1/auth/login ─────────────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/auth/login")
    .methods(crow::HTTPMethod::Post)
    ([this](const crow::request& req) -> crow::response {
        const std::string creds = basic_auth(req);
        if (creds.empty()) return error_response(401, "Basic auth required");

        const std::size_t colon = creds.find(':');
        if (colon == std::string::npos) return error_response(401, "Bad credentials");

        const std::string user     = creds.substr(0, colon);
        const std::string password = creds.substr(colon + 1);

        const auto token = users_.login(user, password);
        if (!token) return error_response(401, "Invalid credentials");

        return json_response(200, json{{"token", *token}, {"username", user}});
    });
}

void RepoApiRoutes::register_repo_crud_routes(crow::SimpleApp& app) {
    // ── GET /api/v1/repos ───────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        // No hard auth requirement — unauthenticated users see public repos.

        json repos_arr = json::array();
        const auto repos_root = config_.repos_dir();
        if (!fs::exists(repos_root)) return json_response(200, repos_arr);

        for (const auto& user_entry : fs::directory_iterator(repos_root)) {
            if (!user_entry.is_directory()) continue;
            const std::string owner = user_entry.path().filename().string();
            for (const auto& repo_entry : fs::directory_iterator(user_entry.path())) {
                if (!repo_entry.is_directory()) continue;
                std::string name = repo_entry.path().filename().string();
                if (name.ends_with(".git")) name = name.substr(0, name.size() - 4);

                if (!repos_.can_read(owner, name, auth_user)) continue;

                const bool is_priv       = repos_.is_private(owner, name);
                const bool z_up          = repos_.is_z_up(owner, name);
                const bool resolve_links = repos_.is_resolve_links(owner, name);
                repos_arr.push_back(repo_info(config_, owner, name,
                                              repo_entry.path(), is_priv, z_up, resolve_links));
            }
        }
        return json_response(200, repos_arr);
    });

    // ── POST /api/v1/repos ──────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos")
    .methods(crow::HTTPMethod::Post)
    ([this](const crow::request& req) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (auth_user.empty()) return error_response(401, "Unauthorized");

        json body{};
        try { body = json::parse(req.body); }
        catch (...) { return error_response(400, "Invalid JSON"); }

        const std::string name    = body.value("name", "");
        const bool        is_priv = body.value("private", false);
        if (name.empty()) return error_response(400, "name required");

        const auto repo_path = config_.repos_dir() / auth_user / (name + ".git");
        if (fs::exists(repo_path)) return error_response(409, "Repository already exists");

        const std::string description = body.value("description", "");

        fs::create_directories(repo_path);
        auto maybe_repo = git::Repository::init_bare(repo_path);
        if (!maybe_repo)
            return error_response(500, "Failed to initialize repository");
        maybe_repo->create_initial_commit(name, description, auth_user);
        git::Repository::configure_lfs(repo_path);

        repos_.create_repo(auth_user, name, is_priv);

        return json_response(201, repo_info(config_, auth_user, name, repo_path, is_priv));
    });

    // ── GET /api/v1/repos/:user/:repo ───────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (!repos_.can_read(user, repo, auth_user))
            return error_response(404, "Repository not found");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        if (!fs::exists(repo_path)) return error_response(404, "Repository not found");

        const bool is_priv       = repos_.is_private(user, repo);
        const bool z_up          = repos_.is_z_up(user, repo);
        const bool resolve_links = repos_.is_resolve_links(user, repo);
        return json_response(200, repo_info(config_, user, repo, repo_path, is_priv, z_up, resolve_links));
    });

    // ── PATCH /api/v1/repos/:user/:repo ─────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>")
    .methods(crow::HTTPMethod::Patch)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (auth_user.empty()) return error_response(401, "Unauthorized");
        if (auth_user != user)  return error_response(403, "Forbidden");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        if (!fs::exists(repo_path)) return error_response(404, "Repository not found");

        json body{};
        try { body = json::parse(req.body); }
        catch (...) { return error_response(400, "Invalid JSON"); }

        if (body.contains("private"))
            repos_.set_private(user, repo, body["private"].get<bool>());
        if (body.contains("z_up"))
            repos_.set_z_up(user, repo, body["z_up"].get<bool>());
        if (body.contains("resolve_links"))
            repos_.set_resolve_links(user, repo, body["resolve_links"].get<bool>());

        const bool is_priv       = repos_.is_private(user, repo);
        const bool z_up          = repos_.is_z_up(user, repo);
        const bool resolve_links = repos_.is_resolve_links(user, repo);
        return json_response(200, repo_info(config_, user, repo, repo_path, is_priv, z_up, resolve_links));
    });

    // ── DELETE /api/v1/repos/:user/:repo ────────────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>")
    .methods(crow::HTTPMethod::Delete)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (auth_user.empty()) return error_response(401, "Unauthorized");
        if (auth_user != user)  return error_response(403, "Forbidden");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        if (!fs::exists(repo_path)) return error_response(404, "Repository not found");

        std::error_code ec{};
        fs::remove_all(repo_path, ec);
        if (ec) return error_response(500, "Failed to delete repository");

        repos_.delete_repo(user, repo);
        return json_response(200, json{{"deleted", true}});
    });
}

void RepoApiRoutes::register_member_routes(crow::SimpleApp& app) {
    // ── GET /api/v1/repos/:user/:repo/members ───────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/members")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (auth_user.empty()) return error_response(401, "Unauthorized");
        if (auth_user != user)  return error_response(403, "Forbidden");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        if (!fs::exists(repo_path)) return error_response(404, "Repository not found");

        const auto members = repos_.list_members(user, repo);
        json arr = json::array();
        for (const auto& m : members)
            arr.push_back(json{{"username", m.username}, {"can_push", m.can_push}});
        return json_response(200, arr);
    });

    // ── PUT /api/v1/repos/:user/:repo/members/:member ───────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/members/<string>")
    .methods(crow::HTTPMethod::Put)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo,
            const std::string& member) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (auth_user.empty()) return error_response(401, "Unauthorized");
        if (auth_user != user)  return error_response(403, "Forbidden");
        if (member == user)     return error_response(400, "Owner cannot be added as member");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        if (!fs::exists(repo_path)) return error_response(404, "Repository not found");
        if (!users_.user_exists(member)) return error_response(404, "User not found");

        json body{};
        try { body = json::parse(req.body); }
        catch (...) { return error_response(400, "Invalid JSON"); }

        const bool can_push = body.value("can_push", false);
        repos_.add_member(user, repo, member, can_push);
        return json_response(200, json{{"username", member}, {"can_push", can_push}});
    });

    // ── DELETE /api/v1/repos/:user/:repo/members/:member ────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/members/<string>")
    .methods(crow::HTTPMethod::Delete)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo,
            const std::string& member) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (auth_user.empty()) return error_response(401, "Unauthorized");
        if (auth_user != user)  return error_response(403, "Forbidden");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        if (!fs::exists(repo_path)) return error_response(404, "Repository not found");

        repos_.remove_member(user, repo, member);
        return json_response(200, json{{"removed", true}});
    });
}

void RepoApiRoutes::register_history_routes(crow::SimpleApp& app) {
    // ── GET /api/v1/repos/:user/:repo/commits ───────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/commits")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (!repos_.can_read(user, repo, auth_user))
            return error_response(404, "Repository not found");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        auto maybe_repo = git::Repository::open(repo_path);
        if (!maybe_repo) return error_response(404, "Repository not found");

        const std::string ref    = req.url_params.get("ref")
                                   ? req.url_params.get("ref") : "HEAD";
        const int limit  = req.url_params.get("limit")
                           ? std::stoi(req.url_params.get("limit")) : 30;
        const int offset = req.url_params.get("offset")
                           ? std::stoi(req.url_params.get("offset")) : 0;

        const auto commits = maybe_repo->list_commits(ref, limit, offset);
        json result = json::array();
        for (const auto& ci : commits)
            result.push_back(json{{"sha", ci.sha}, {"message", ci.message},
                                  {"author", ci.author}, {"email", ci.email},
                                  {"timestamp", ci.timestamp}});
        return json_response(200, result);
    });

    // ── GET /api/v1/repos/:user/:repo/commits/:sha ──────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/commits/<string>")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo,
            const std::string& sha) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (!repos_.can_read(user, repo, auth_user))
            return error_response(404, "Repository not found");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        auto maybe_repo = git::Repository::open(repo_path);
        if (!maybe_repo) return error_response(404, "Repository not found");

        const auto ci = maybe_repo->get_commit(sha);
        if (!ci) return error_response(404, "Commit not found");
        return json_response(200, json{{"sha", ci->sha}, {"message", ci->message},
                                       {"author", ci->author}, {"email", ci->email},
                                       {"timestamp", ci->timestamp}});
    });
}

void RepoApiRoutes::register_content_routes(crow::SimpleApp& app) {
    // ── GET /api/v1/repos/:user/:repo/tree/:ref ─────────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/tree/<string>")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo,
            const std::string& ref) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (!repos_.can_read(user, repo, auth_user))
            return error_response(404, "Repository not found");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        auto maybe_repo = git::Repository::open(repo_path);
        if (!maybe_repo) return error_response(404, "Repository not found");

        const auto entries = maybe_repo->list_tree(ref, "");
        json result = json::array();
        for (const auto& te : entries)
            result.push_back(json{{"name", te.name}, {"type", te.type},
                                  {"sha", te.sha}, {"size", te.size}});
        return json_response(200, result);
    });

    // ── GET /api/v1/repos/:user/:repo/tree/:ref/* ───────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/tree/<string>/<path>")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req,
            const std::string& user, const std::string& repo,
            const std::string& ref, const std::string& path) -> crow::response {
        const std::string auth_user = bearer_auth(req);
        if (!repos_.can_read(user, repo, auth_user))
            return error_response(404, "Repository not found");

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        auto maybe_repo = git::Repository::open(repo_path);
        if (!maybe_repo) return error_response(404, "Repository not found");

        const auto entries = maybe_repo->list_tree(ref, path);
        json result = json::array();
        for (const auto& te : entries)
            result.push_back(json{{"name", te.name}, {"type", te.type},
                                  {"sha", te.sha}, {"size", te.size}});
        return json_response(200, result);
    });

    // ── GET /api/v1/repos/:user/:repo/blob/:ref/* ───────────────────────────
    CROW_ROUTE(app, "/api/v1/repos/<string>/<string>/blob/<string>/<path>")
    .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req,
            crow::response& res,
            const std::string& user, const std::string& repo,
            const std::string& ref, const std::string& path) {
        const std::string auth_user = bearer_auth(req);
        if (!repos_.can_read(user, repo, auth_user)) {
            res.code = 404; add_cors(res); res.end("Not Found"); return;
        }

        const auto repo_path = config_.repos_dir() / user / (repo + ".git");
        auto maybe_repo = git::Repository::open(repo_path);
        if (!maybe_repo) {
            res.code = 404; add_cors(res); res.end("Repository not found"); return;
        }

        const auto blob = maybe_repo->read_blob(ref, path);
        if (!blob) {
            res.code = 404; add_cors(res); res.end("File not found"); return;
        }

        res.code = 200;
        res.add_header("Content-Type", "application/octet-stream");
        add_cors(res);
        res.body = std::string(reinterpret_cast<const char*>(blob->data()), blob->size());
        res.end();
    });
}

} // namespace api
