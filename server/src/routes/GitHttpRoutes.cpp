#include "GitHttpRoutes.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "util/Base64.hpp"
#include "util/Hex.hpp"

namespace api {

namespace {

// ─── CORS helper ─────────────────────────────────────────────────────────────

void add_cors(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
}

// ─── CGI parsing ─────────────────────────────────────────────────────────────

struct CgiResponse {
    int                                 status{200};
    std::map<std::string, std::string>  headers{};
    std::vector<uint8_t>                body{};
};

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

CgiResponse parse_cgi_output(const std::vector<uint8_t>& raw) {
    CgiResponse resp{};

    const std::string haystack(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::size_t sep = haystack.find("\r\n\r\n");
    std::size_t sep_len = 4;
    if (sep == std::string::npos) {
        sep = haystack.find("\n\n");
        sep_len = 2;
    }
    if (sep == std::string::npos) {
        resp.body = raw;
        return resp;
    }

    const std::string header_block = haystack.substr(0, sep);
    const std::size_t body_start   = sep + sep_len;
    resp.body.assign(raw.begin() + static_cast<std::ptrdiff_t>(body_start), raw.end());

    std::istringstream ss(header_block);
    std::string line{};
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key   = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        const std::size_t val_start = value.find_first_not_of(' ');
        if (val_start != std::string::npos) value = value.substr(val_start);

        const std::string lower_key = to_lower(key);
        if (lower_key == "status") resp.status = std::stoi(value.substr(0, 3));
        resp.headers[lower_key] = value;
    }

    return resp;
}

// ─── CGI subprocess ──────────────────────────────────────────────────────────

struct CgiEnv {
    std::string project_root{};
    std::string path_info{};
    std::string method{};
    std::string query{};
    std::string content_type{};
    std::string content_length{};
    std::string remote_user{};
    std::string remote_addr{};
    std::string git_protocol{};
};

[[noreturn]] static void exec_git_backend(const CgiEnv& env, int in_fd, int out_fd) {
    ::dup2(in_fd,  STDIN_FILENO);
    ::dup2(out_fd, STDOUT_FILENO);

    ::setenv("GIT_HTTP_EXPORT_ALL", "1",                          1);
    ::setenv("GIT_PROJECT_ROOT",    env.project_root.c_str(),     1);
    ::setenv("PATH_INFO",           env.path_info.c_str(),        1);
    ::setenv("REQUEST_METHOD",      env.method.c_str(),           1);
    ::setenv("QUERY_STRING",        env.query.c_str(),            1);
    ::setenv("CONTENT_TYPE",        env.content_type.c_str(),     1);
    ::setenv("CONTENT_LENGTH",      env.content_length.c_str(),   1);
    ::setenv("REMOTE_USER",         env.remote_user.c_str(),      1);
    ::setenv("REMOTE_ADDR",         env.remote_addr.c_str(),      1);
    if (!env.git_protocol.empty())
        ::setenv("HTTP_GIT_PROTOCOL", env.git_protocol.c_str(),   1);
    ::setenv("GIT_COMMITTER_NAME",  env.remote_user.c_str(),      1);
    ::setenv("GIT_COMMITTER_EMAIL", "",                           1);

    ::execlp("git", "git", "http-backend", nullptr);
    ::_exit(1);
}

static std::vector<uint8_t> pump_pipes(pid_t pid,
                                        int write_fd,
                                        int read_fd,
                                        const std::vector<uint8_t>& body) {
    std::size_t written = 0;
    while (written < body.size()) {
        const ssize_t n = ::write(write_fd, body.data() + written, body.size() - written);
        if (n <= 0) break;
        written += static_cast<std::size_t>(n);
    }
    ::close(write_fd);

    std::vector<uint8_t> output{};
    output.reserve(65536);
    std::array<char, 65536> buf{};
    ssize_t n = 0;
    while ((n = ::read(read_fd, buf.data(), buf.size())) > 0)
        output.insert(output.end(), buf.data(), buf.data() + n);
    ::close(read_fd);

    int wstatus = 0;
    ::waitpid(pid, &wstatus, 0);
    return output;
}

CgiResponse run_git_http_backend(const CgiEnv& env,
                                  const std::vector<uint8_t>& request_body) {
    int stdin_pipe[2]  = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (::pipe(stdin_pipe) != 0 || ::pipe(stdout_pipe) != 0) {
        spdlog::error("GitHttp: pipe() failed");
        return CgiResponse{500, {}, {}};
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(stdin_pipe[0]);  ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]); ::close(stdout_pipe[1]);
        spdlog::error("GitHttp: fork() failed");
        return CgiResponse{500, {}, {}};
    }

    if (pid == 0) {
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        exec_git_backend(env, stdin_pipe[0], stdout_pipe[1]);
    }

    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);

    const auto raw = pump_pipes(pid, stdin_pipe[1], stdout_pipe[0], request_body);
    return parse_cgi_output(raw);
}

} // namespace

// ─── GitHttpRoutes ────────────────────────────────────────────────────────────

GitHttpRoutes::GitHttpRoutes(const g4c::Config&              config,
                              const auth::UserStore&           users,
                              cad::CadPipeline&               pipeline,
                              const storage::SeaweedFsClient& seaweedfs,
                              store::RepoStore&               repos)
    : config_(config), users_(users), pipeline_(pipeline),
      seaweedfs_(seaweedfs), repos_(repos) {}

bool GitHttpRoutes::authenticate(const crow::request& req, std::string& out_user) const {
    const std::string auth_header = req.get_header_value("Authorization");
    if (auth_header.empty()) return false;

    if (auth_header.size() > 6 && auth_header.substr(0, 6) == "Basic ") {
        const std::string b64 = auth_header.substr(6);
        const auto decoded = util::base64_decode(b64);
        const std::string creds(reinterpret_cast<const char*>(decoded.data()), decoded.size());
        const std::size_t colon = creds.find(':');
        if (colon == std::string::npos) return false;
        const std::string user  = creds.substr(0, colon);
        const std::string token = creds.substr(colon + 1);
        if (users_.validate_token(user, token) || users_.validate_password(user, token)) {
            out_user = user;
            return true;
        }
    }
    return false;
}

void GitHttpRoutes::enqueue_cad_for_ref(git::Repository&   repo_obj,
                                         const std::string& user,
                                         const std::string& repo_name,
                                         const std::string& old_sha,
                                         const std::string& new_sha) const {
    repo_obj.walk_new_commits(
        old_sha, new_sha,
        [&](const git::CommitInfo& ci, const std::vector<std::string>& files) {
            for (const std::string& fp : files) {
                auto blob = repo_obj.read_blob_at_commit(ci.sha, fp);
                if (!blob) continue;
                cad::CadJob job{};
                job.user      = user;
                job.repo      = repo_name;
                job.sha       = ci.sha;
                job.file_path = fp;
                job.blob_data = std::move(*blob);
                pipeline_.enqueue(std::move(job));
            }
        });
}

void GitHttpRoutes::scan_post_receive(const std::string& user,
                                       const std::string& repo_name,
                                       const fs::path&    repo_path,
                                       const RefMap&      refs_before) const {
    auto repo_opt = git::Repository::open(repo_path);
    if (!repo_opt) return;

    const RefMap refs_after = repo_opt->snapshot_refs();
    for (const auto& [ref_name, new_sha] : refs_after) {
        const std::string old_sha =
            refs_before.count(ref_name) ? refs_before.at(ref_name) : "";
        if (old_sha == new_sha) continue;
        enqueue_cad_for_ref(*repo_opt, user, repo_name, old_sha, new_sha);
    }
}

void GitHttpRoutes::handle_git_request(const crow::request& req,
                                        crow::response&      res,
                                        const std::string&   url_user,
                                        const std::string&   repo,
                                        const std::string&   git_path) const {
    std::string auth_user{};
    const bool  authed = authenticate(req, auth_user);

    const std::string repo_name = repo.ends_with(".git") ? repo : (repo + ".git");
    const std::string repo_base = repo_name.size() > 4
        ? repo_name.substr(0, repo_name.size() - 4) : repo_name;
    const fs::path    repo_path = config_.repos_dir() / url_user / repo_name;

    // Pre-compute permission flags (single SQLite read each).
    const bool can_r = repos_.can_read(url_user, repo_base, auth_user);
    const bool can_w = repos_.can_push(url_user, repo_base, auth_user);

    // Intercept Git LFS API before forwarding to git-http-backend.
    static constexpr std::string_view kLfsPrefix = "info/lfs/";
    if (git_path.starts_with(kLfsPrefix)) {
        handle_lfs(req, res, authed, can_r, can_w, url_user, repo_name,
                   git_path.substr(kLfsPrefix.size()));
        return;
    }

    // Determine if this is a write (push) or read (clone/fetch) operation.
    bool is_write = false;
    if (git_path == "git-receive-pack") {
        is_write = true;
    } else if (git_path == "info/refs") {
        const char* svc = req.url_params.get("service");
        if (svc && std::string{svc} == "git-receive-pack") is_write = true;
    }

    if (is_write) {
        if (!authed) {
            res.code = 401;
            res.add_header("WWW-Authenticate", "Basic realm=\"git4cad\"");
            add_cors(res);
            res.end("Unauthorized");
            return;
        }
        if (!can_w) {
            res.code = 403;
            add_cors(res);
            res.end("Forbidden");
            return;
        }
    } else {
        // Read operation: public repos need no auth; private repos require it.
        if (!can_r) {
            if (!authed) {
                res.code = 401;
                res.add_header("WWW-Authenticate", "Basic realm=\"git4cad\"");
                add_cors(res);
                res.end("Unauthorized");
                return;
            }
            res.code = 403;
            add_cors(res);
            res.end("Forbidden");
            return;
        }
    }

    const bool is_receive = (git_path == "git-receive-pack");
    RefMap refs_before{};
    if (is_receive) {
        if (auto r = git::Repository::open(repo_path)) refs_before = r->snapshot_refs();
    }

    const CgiEnv env{
        .project_root   = config_.repos_dir().string(),
        .path_info      = "/" + url_user + "/" + repo_name + "/" + git_path,
        .method         = crow::method_name(req.method),
        .query          = req.raw_url.find('?') != std::string::npos
                              ? req.raw_url.substr(req.raw_url.find('?') + 1) : "",
        .content_type   = req.get_header_value("Content-Type"),
        .content_length = req.get_header_value("Content-Length"),
        .remote_user    = auth_user,
        .remote_addr    = req.get_header_value("X-Real-IP").empty()
                              ? "127.0.0.1" : req.get_header_value("X-Real-IP"),
        .git_protocol   = req.get_header_value("Git-Protocol"),
    };
    const std::vector<uint8_t> body(req.body.begin(), req.body.end());
    const CgiResponse cgi = run_git_http_backend(env, body);

    res.code = cgi.status;
    for (const auto& [k, v] : cgi.headers) {
        if (k == "status") continue;
        res.add_header(k, v);
    }
    add_cors(res);
    res.body = std::string(reinterpret_cast<const char*>(cgi.body.data()), cgi.body.size());

    if (is_receive && cgi.status == 200)
        scan_post_receive(url_user, repo_base, repo_path, refs_before);

    res.end();
}

void GitHttpRoutes::handle_lfs(const crow::request& req,
                                crow::response&      res,
                                bool                 authed,
                                bool                 can_read,
                                bool                 can_push_access,
                                const std::string&   user,
                                const std::string&   repo,
                                const std::string&   lfs_path) const {
    using json = nlohmann::json;

    const std::string lfs_ct = "application/vnd.git-lfs+json";

    auto lfs_error = [&](int code, std::string_view msg) {
        res.code = code;
        res.add_header("Content-Type", lfs_ct);
        add_cors(res);
        res.body = json{{"message", msg}}.dump();
        res.end();
    };

    auto lfs_ok = [&](int code, const json& body) {
        res.code = code;
        res.add_header("Content-Type", lfs_ct);
        add_cors(res);
        res.body = body.dump();
        res.end();
    };

    auto require_read = [&]() -> bool {
        if (can_read) return true;
        if (!authed) { lfs_error(401, "Unauthorized"); }
        else         { lfs_error(403, "Forbidden"); }
        return false;
    };

    auto require_write = [&]() -> bool {
        if (can_push_access) return true;
        if (!authed) { lfs_error(401, "Unauthorized"); }
        else         { lfs_error(403, "Forbidden"); }
        return false;
    };

    const std::string repo_base = config_.public_url + "/" + user + "/" + repo;

    // ── POST objects/batch ──────────────────────────────────────────────────
    if (lfs_path == "objects/batch" && req.method == crow::HTTPMethod::Post) {
        json req_body{};
        try { req_body = json::parse(req.body); }
        catch (...) { lfs_error(400, "Invalid JSON"); return; }

        const std::string op = req_body.value("operation", "");

        if (op == "upload"   && !require_write()) return;
        if (op == "download" && !require_read())  return;

        const json objects = req_body.value("objects", json::array());
        json resp_objects  = json::array();
        for (const auto& obj : objects) {
            const std::string oid  = obj.value("oid",  "");
            const int64_t     size = obj.value("size", int64_t{0});
            if (oid.empty()) continue;

            json entry = json{{"oid", oid}, {"size", size}};

            if (op == "upload") {
                if (!seaweedfs_.lfs_exists(user, repo, oid)) {
                    const std::string href = repo_base + "/info/lfs/objects/" + oid;
                    entry["actions"] = {
                        {"upload", {{"href", href}, {"expires_in", 86400}}},
                        {"verify", {{"href", href + "/verify"}, {"expires_in", 86400}}}
                    };
                }
            } else if (op == "download") {
                if (seaweedfs_.lfs_exists(user, repo, oid)) {
                    entry["actions"] = {
                        {"download", {{"href", seaweedfs_.lfs_public_url(user, repo, oid)}}}
                    };
                } else {
                    entry["error"] = {{"code", 404}, {"message", "Object not found"}};
                }
            }
            resp_objects.push_back(std::move(entry));
        }

        lfs_ok(200, json{{"transfer", "basic"}, {"objects", resp_objects}});
        return;
    }

    static constexpr std::string_view kObjPrefix   = "objects/";
    static constexpr std::string_view kVerifySuffix = "/verify";

    // ── POST objects/{oid}/verify ───────────────────────────────────────────
    if (lfs_path.starts_with(kObjPrefix) && lfs_path.ends_with(kVerifySuffix)
            && req.method == crow::HTTPMethod::Post) {
        if (!require_read()) return;
        const std::string oid = lfs_path.substr(
            kObjPrefix.size(), lfs_path.size() - kObjPrefix.size() - kVerifySuffix.size());
        if (seaweedfs_.lfs_exists(user, repo, oid)) {
            res.code = 200; add_cors(res); res.end();
        } else {
            lfs_error(404, "Object not found");
        }
        return;
    }

    // ── PUT objects/{oid}  — upload ─────────────────────────────────────────
    if (lfs_path.starts_with(kObjPrefix) && req.method == crow::HTTPMethod::Put) {
        if (!require_write()) return;
        const std::string oid = lfs_path.substr(kObjPrefix.size());
        if (oid.empty() || oid.find('/') != std::string::npos) {
            lfs_error(400, "Invalid OID"); return;
        }

        const std::string actual_oid = util::sha256_hex(req.body);
        if (actual_oid != oid) {
            spdlog::warn("LFS upload: OID mismatch — expected {} got {}", oid, actual_oid);
            lfs_error(422, "SHA-256 mismatch"); return;
        }

        if (!seaweedfs_.lfs_upload(user, repo, oid, req.body.data(), req.body.size())) {
            lfs_error(500, "Failed to store object"); return;
        }
        res.code = 200; add_cors(res); res.end();
        return;
    }

    // ── GET objects/{oid}  — download redirect ──────────────────────────────
    if (lfs_path.starts_with(kObjPrefix) && req.method == crow::HTTPMethod::Get) {
        if (!require_read()) return;
        const std::string oid = lfs_path.substr(kObjPrefix.size());
        if (!seaweedfs_.lfs_exists(user, repo, oid)) {
            lfs_error(404, "Object not found"); return;
        }
        res.code = 302;
        res.add_header("Location", seaweedfs_.lfs_public_url(user, repo, oid));
        add_cors(res);
        res.end();
        return;
    }

    lfs_error(404, "Unknown LFS endpoint");
}

void GitHttpRoutes::register_routes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/<string>/<path>")
    .methods(crow::HTTPMethod::Options)
    ([](const crow::request&, crow::response& res, std::string, std::string) {
        res.code = 204;
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.end();
    });

    CROW_ROUTE(app, "/<string>/<path>")
    .methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post)
    ([this](const crow::request& req, crow::response& res,
            std::string first, std::string rest) {
        const std::size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            // No git sub-path (e.g. "/info/refs", "/git-upload-pack") — real
            // git clients always request one of those, so this is a browser
            // visiting the bare clone URL directly (the repo page's own
            // "Clone" box shows exactly this string). Send it to the
            // human-readable repo page instead of a bare 404.
            if (req.method == crow::HTTPMethod::Get && rest.ends_with(".git")) {
                const std::string repo_base = rest.substr(0, rest.size() - 4);
                res.code = 302;
                res.add_header("Location", "/" + first + "/" + repo_base);
                add_cors(res);
                res.end();
                return;
            }
            res.code = 404; add_cors(res); res.end("Not Found"); return;
        }
        const std::string repo     = rest.substr(0, slash);
        const std::string git_path = rest.substr(slash + 1);

        if (!repo.ends_with(".git")) {
            res.code = 404; add_cors(res); res.end("Not Found"); return;
        }

        handle_git_request(req, res, first, repo, git_path);
    });
}

} // namespace api
