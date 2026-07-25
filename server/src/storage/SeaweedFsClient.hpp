#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace storage {

/// HTTP client for the SeaweedFS Filer API.
/// All GLB blobs are stored at filer path: /glb/{user}/{repo}/{sha}/{path_sha256_prefix}.glb
/// Public access goes through nginx proxy at {public_prefix}/glb/...
class SeaweedFsClient {
public:
    /// filer_base_url  : internal URL of SeaweedFS filer, e.g. "http://seaweedfs:8888"
    /// public_prefix   : nginx proxy prefix for browser-facing redirects, e.g. "/seaweed"
    SeaweedFsClient(std::string filer_base_url, std::string public_prefix);

    SeaweedFsClient(const SeaweedFsClient&)            = delete;
    SeaweedFsClient& operator=(const SeaweedFsClient&) = delete;
    SeaweedFsClient(SeaweedFsClient&&)                 = default;
    SeaweedFsClient& operator=(SeaweedFsClient&&)      = default;

    /// Must be called once before any threads start (wraps curl_global_init).
    static void global_init() noexcept;

    /// Must be called after all threads have stopped (wraps curl_global_cleanup).
    static void global_shutdown() noexcept;

    /// Upload a local file to SeaweedFS. Returns true on HTTP 2xx.
    [[nodiscard]] bool upload(const std::string& user,
                              const std::string& repo,
                              const std::string& sha,
                              const std::string& file_path,
                              const fs::path&    local_file) const noexcept;

    /// HEAD request to check if the GLB exists in SeaweedFS.
    [[nodiscard]] bool exists(const std::string& user,
                              const std::string& repo,
                              const std::string& sha,
                              const std::string& file_path) const noexcept;

    /// Return the nginx-proxied public URL for a 302 redirect.
    [[nodiscard]] std::string public_url(const std::string& user,
                                         const std::string& repo,
                                         const std::string& sha,
                                         const std::string& file_path) const;

    // ── Git LFS object storage ────────────────────────────────────────────────
    // LFS objects are stored at filer path: /lfs/{user}/{repo}/{oid}
    // Served publicly at:                   {public_prefix}/lfs/{user}/{repo}/{oid}

    /// Upload raw bytes as an LFS object. Returns true on HTTP 2xx.
    [[nodiscard]] bool lfs_upload(const std::string& user,
                                   const std::string& repo,
                                   const std::string& oid,
                                   const char*        data,
                                   std::size_t        size) const noexcept;

    /// HEAD check — true if the LFS object already exists.
    [[nodiscard]] bool lfs_exists(const std::string& user,
                                   const std::string& repo,
                                   const std::string& oid) const noexcept;

    /// Return the nginx-proxied public URL for an LFS object.
    [[nodiscard]] std::string lfs_public_url(const std::string& user,
                                              const std::string& repo,
                                              const std::string& oid) const;

private:
    /// Compute the canonical filer sub-path for a GLB artifact.
    [[nodiscard]] static std::string filer_subpath(const std::string& user,
                                                    const std::string& repo,
                                                    const std::string& sha,
                                                    const std::string& file_path);

    std::string filer_base_url_;
    std::string public_prefix_;
};

} // namespace storage
