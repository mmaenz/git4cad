#include "Pipeline.hpp"

#include <algorithm>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "StepConverter.hpp"
#include "FcstdConverter.hpp"
#include "GltfWriter.hpp"
#include "util/Hex.hpp"

namespace cad {

namespace fs = std::filesystem;

// ─── Helpers ─────────────────────────────────────────────────────────────────

std::string CadPipeline::job_key(const std::string& user,
                                  const std::string& repo,
                                  const std::string& sha,
                                  const std::string& file_path) {
    const std::string path_hash = util::sha256_hex(file_path.data(), file_path.size());
    return user + "/" + repo + "/" + sha + "/" + path_hash.substr(0, 16);
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

CadPipeline::CadPipeline(storage::SeaweedFsClient& seaweedfs, int num_workers)
    : seaweedfs_(seaweedfs) {
    workers_.reserve(static_cast<std::size_t>(num_workers));
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back([this]{ worker_loop(); });
    }
}

CadPipeline::~CadPipeline() {
    {
        std::lock_guard lk{queue_mtx_};
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) { t.join(); }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

void CadPipeline::enqueue(CadJob job) {
    const std::string key = job_key(job.user, job.repo, job.sha, job.file_path);

    // Check SeaweedFS first — handles re-enqueue after server restart.
    if (seaweedfs_.exists(job.user, job.repo, job.sha, job.file_path)) {
        std::lock_guard lk{status_mtx_};
        status_map_[key] = JobStatus::Ready;
        return;
    }

    {
        std::lock_guard lk{status_mtx_};
        if (status_map_.count(key)) { return; } // already queued / in-progress
        status_map_[key] = JobStatus::Pending;
    }

    {
        std::lock_guard lk{queue_mtx_};
        queue_.push_back(std::move(job));
    }
    cv_.notify_one();
}

JobStatus CadPipeline::status(const std::string& user,
                               const std::string& repo,
                               const std::string& sha,
                               const std::string& file_path) const {
    const std::string key = job_key(user, repo, sha, file_path);

    {
        std::lock_guard lk{status_mtx_};
        const auto it = status_map_.find(key);
        if (it != status_map_.end()) { return it->second; }
    }

    // Cache miss (e.g. after server restart): check SeaweedFS directly.
    if (seaweedfs_.exists(user, repo, sha, file_path)) { return JobStatus::Ready; }
    return JobStatus::Pending;
}

// ─── Worker ──────────────────────────────────────────────────────────────────

void CadPipeline::worker_loop() {
    while (true) {
        CadJob job{};
        {
            std::unique_lock lk{queue_mtx_};
            cv_.wait(lk, [this]{ return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) { return; }
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        const std::string key = job_key(job.user, job.repo, job.sha, job.file_path);
        {
            std::lock_guard lk{status_mtx_};
            status_map_[key] = JobStatus::Processing;
        }

        const bool ok = [&]() noexcept -> bool {
            try {
                process_job(job);
                return true;
            } catch (const std::exception& e) {
                spdlog::error("CadPipeline: job {} failed: {}", key, e.what());
                return false;
            } catch (...) {
                spdlog::error("CadPipeline: job {} failed (unknown exception)", key);
                return false;
            }
        }();

        std::lock_guard lk{status_mtx_};
        status_map_[key] = ok ? JobStatus::Ready : JobStatus::Error;
    }
}

void CadPipeline::process_job(const CadJob& job) {
    spdlog::info("CadPipeline: converting {}/{}/{}/{}",
                 job.user, job.repo, job.sha, job.file_path);

    // Temp file — cleaned up via RAII guard regardless of outcome.
    const std::string key     = job_key(job.user, job.repo, job.sha, job.file_path);
    const fs::path    tmp_glb = fs::temp_directory_path() / ("git4cad-" + key.substr(key.rfind('/') + 1) + ".glb");

    struct TempGuard {
        const fs::path& path;
        ~TempGuard() { std::error_code ec; fs::remove(path, ec); }
    } tmp_guard{tmp_glb};

    // Determine file type by extension (case-insensitive).
    std::string lower_path = job.file_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    std::optional<Handle(TDocStd_Document)> doc_opt{};
    if (lower_path.ends_with(".step") || lower_path.ends_with(".stp")) {
        doc_opt = cad::read_step(job.blob_data);
    } else if (lower_path.ends_with(".fcstd")) {
        doc_opt = cad::read_fcstd(job.blob_data);
    } else {
        spdlog::warn("CadPipeline: unsupported file type '{}'", job.file_path);
        return;
    }

    if (!doc_opt) {
        throw std::runtime_error("CAD read failed: " + job.file_path);
    }

    if (!write_glb(*doc_opt, tmp_glb)) {
        throw std::runtime_error("GLB write failed: " + tmp_glb.string());
    }

    // Upload to SeaweedFS.
    if (!seaweedfs_.upload(job.user, job.repo, job.sha, job.file_path, tmp_glb)) {
        throw std::runtime_error("SeaweedFS upload failed for: " + job.file_path);
    }

    spdlog::info("CadPipeline: uploaded GLB to SeaweedFS for {}/{}/{}/{}",
                 job.user, job.repo, job.sha, job.file_path);
}

} // namespace cad
