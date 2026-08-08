#include "SeaweedFsClient.hpp"

#include <cstdio>

#include <curl/curl.h>
#include <spdlog/spdlog.h>

#include "util/Hex.hpp"

namespace storage {

namespace {

// libcurl write callback that discards the response body.
size_t discard_write(char* /*buf*/, size_t size, size_t nmemb, void* /*userdata*/) {
    return size * nmemb;
}

// libcurl read callback for in-memory upload buffers.
struct MemReader {
    const char* data;
    std::size_t size;
    std::size_t offset{0};
};

size_t mem_read_cb(char* buf, size_t size, size_t nmemb, void* userdata) {
    auto* mr = static_cast<MemReader*>(userdata);
    const std::size_t avail = mr->size - mr->offset;
    const std::size_t to_copy = std::min(size * nmemb, avail);
    std::memcpy(buf, mr->data + mr->offset, to_copy);
    mr->offset += to_copy;
    return to_copy;
}

struct CurlGuard {
    CURL* handle;
    explicit CurlGuard(CURL* h) : handle(h) {}
    ~CurlGuard() { if (handle) { curl_easy_cleanup(handle); } }
    CurlGuard(const CurlGuard&)            = delete;
    CurlGuard& operator=(const CurlGuard&) = delete;
};

struct FileGuard {
    FILE* fp;
    explicit FileGuard(FILE* f) : fp(f) {}
    ~FileGuard() { if (fp) { std::fclose(fp); } }
    FileGuard(const FileGuard&)            = delete;
    FileGuard& operator=(const FileGuard&) = delete;
};

} // namespace

// ─── Static lifecycle ─────────────────────────────────────────────────────────

void SeaweedFsClient::global_init() noexcept {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void SeaweedFsClient::global_shutdown() noexcept {
    curl_global_cleanup();
}

// ─── Constructor ─────────────────────────────────────────────────────────────

SeaweedFsClient::SeaweedFsClient(std::string filer_base_url, std::string public_prefix)
    : filer_base_url_(std::move(filer_base_url))
    , public_prefix_(std::move(public_prefix)) {}

// ─── Private helpers ─────────────────────────────────────────────────────────

std::string SeaweedFsClient::filer_subpath(const std::string& user,
                                            const std::string& repo,
                                            const std::string& sha,
                                            const std::string& file_path,
                                            bool               light) {
    const std::string path_hash = util::sha256_hex(file_path.data(), file_path.size());
    return "/glb/" + user + "/" + repo + "/" + sha + "/" + path_hash.substr(0, 16)
         + (light ? "_light" : "") + ".glb";
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool SeaweedFsClient::upload(const std::string& user,
                              const std::string& repo,
                              const std::string& sha,
                              const std::string& file_path,
                              const fs::path&    local_file,
                              bool               light) const noexcept {
    CURL* raw = curl_easy_init();
    if (!raw) {
        spdlog::error("SeaweedFsClient: curl_easy_init failed");
        return false;
    }
    CurlGuard guard{raw};

    FILE* fp = std::fopen(local_file.c_str(), "rb");
    if (!fp) {
        spdlog::error("SeaweedFsClient: cannot open '{}'", local_file.string());
        return false;
    }
    FileGuard fg{fp};

    std::error_code ec{};
    const auto file_size = static_cast<curl_off_t>(fs::file_size(local_file, ec));
    if (ec) {
        spdlog::error("SeaweedFsClient: file_size error: {}", ec.message());
        return false;
    }

    const std::string url = filer_base_url_ + filer_subpath(user, repo, sha, file_path, light);
    curl_easy_setopt(raw, CURLOPT_URL, url.c_str());
    curl_easy_setopt(raw, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(raw, CURLOPT_READDATA, fp);
    curl_easy_setopt(raw, CURLOPT_INFILESIZE_LARGE, file_size);
    curl_easy_setopt(raw, CURLOPT_WRITEFUNCTION, discard_write);

    const CURLcode rc = curl_easy_perform(raw);
    if (rc != CURLE_OK) {
        spdlog::error("SeaweedFsClient: upload to '{}' failed: {}", url, curl_easy_strerror(rc));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(raw, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        spdlog::error("SeaweedFsClient: upload HTTP {} for '{}'", http_code, url);
        return false;
    }

    return true;
}

bool SeaweedFsClient::exists(const std::string& user,
                              const std::string& repo,
                              const std::string& sha,
                              const std::string& file_path,
                              bool               light) const noexcept {
    CURL* raw = curl_easy_init();
    if (!raw) { return false; }
    CurlGuard guard{raw};

    const std::string url = filer_base_url_ + filer_subpath(user, repo, sha, file_path, light);
    curl_easy_setopt(raw, CURLOPT_URL, url.c_str());
    curl_easy_setopt(raw, CURLOPT_NOBODY, 1L);   // HEAD request
    curl_easy_setopt(raw, CURLOPT_WRITEFUNCTION, discard_write);

    const CURLcode rc = curl_easy_perform(raw);
    if (rc != CURLE_OK) { return false; }

    long http_code = 0;
    curl_easy_getinfo(raw, CURLINFO_RESPONSE_CODE, &http_code);
    return http_code == 200;
}

std::string SeaweedFsClient::public_url(const std::string& user,
                                         const std::string& repo,
                                         const std::string& sha,
                                         const std::string& file_path,
                                         bool               light) const {
    return public_prefix_ + filer_subpath(user, repo, sha, file_path, light);
}

// ── LFS object API ────────────────────────────────────────────────────────────

bool SeaweedFsClient::lfs_upload(const std::string& user,
                                  const std::string& repo,
                                  const std::string& oid,
                                  const char*        data,
                                  std::size_t        size) const noexcept {
    CURL* raw = curl_easy_init();
    if (!raw) {
        spdlog::error("SeaweedFsClient::lfs_upload: curl_easy_init failed");
        return false;
    }
    CurlGuard guard{raw};

    MemReader reader{data, size};
    const std::string url = filer_base_url_ + "/lfs/" + user + "/" + repo + "/" + oid;

    curl_easy_setopt(raw, CURLOPT_URL, url.c_str());
    curl_easy_setopt(raw, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(raw, CURLOPT_READFUNCTION, mem_read_cb);
    curl_easy_setopt(raw, CURLOPT_READDATA, &reader);
    curl_easy_setopt(raw, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(size));
    curl_easy_setopt(raw, CURLOPT_WRITEFUNCTION, discard_write);

    const CURLcode rc = curl_easy_perform(raw);
    if (rc != CURLE_OK) {
        spdlog::error("SeaweedFsClient::lfs_upload: '{}' failed: {}", url, curl_easy_strerror(rc));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(raw, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        spdlog::error("SeaweedFsClient::lfs_upload: HTTP {} for '{}'", http_code, url);
        return false;
    }
    return true;
}

bool SeaweedFsClient::lfs_exists(const std::string& user,
                                  const std::string& repo,
                                  const std::string& oid) const noexcept {
    CURL* raw = curl_easy_init();
    if (!raw) return false;
    CurlGuard guard{raw};

    const std::string url = filer_base_url_ + "/lfs/" + user + "/" + repo + "/" + oid;
    curl_easy_setopt(raw, CURLOPT_URL, url.c_str());
    curl_easy_setopt(raw, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(raw, CURLOPT_WRITEFUNCTION, discard_write);

    const CURLcode rc = curl_easy_perform(raw);
    if (rc != CURLE_OK) return false;

    long http_code = 0;
    curl_easy_getinfo(raw, CURLINFO_RESPONSE_CODE, &http_code);
    return http_code == 200;
}

std::string SeaweedFsClient::lfs_public_url(const std::string& user,
                                             const std::string& repo,
                                             const std::string& oid) const {
    return public_prefix_ + "/lfs/" + user + "/" + repo + "/" + oid;
}

} // namespace storage
