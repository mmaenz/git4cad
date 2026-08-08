#include "Pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "StepConverter.hpp"
#include "FCStdConverter.hpp"
#include "GltfWriter.hpp"
#include "LightGeometry.hpp"
#include "util/Hex.hpp"

namespace cad {

namespace fs = std::filesystem;

// ─── Helpers ─────────────────────────────────────────────────────────────────

std::string CadPipeline::job_key(const std::string& user,
                                  const std::string& repo,
                                  const std::string& sha,
                                  const std::string& file_path,
                                  bool               light) {
    const std::string path_hash = util::sha256_hex(file_path.data(), file_path.size());
    return user + "/" + repo + "/" + sha + "/" + path_hash.substr(0, 16) + (light ? "/light" : "");
}

namespace {

std::string links_to_json(const std::vector<FcstdLinkRef>& links) {
    if (links.empty()) return {};
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& l : links) {
        arr.push_back({
            {"file", l.file},
            {"px", l.px}, {"py", l.py}, {"pz", l.pz},
            {"qw", l.qw}, {"qx", l.qx}, {"qy", l.qy}, {"qz", l.qz}
        });
    }
    return arr.dump();
}

} // namespace

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
    const std::string full_key  = job_key(job.user, job.repo, job.sha, job.file_path, false);
    const std::string light_key = job.with_light
        ? job_key(job.user, job.repo, job.sha, job.file_path, true) : std::string{};

    // Check SeaweedFS first — handles re-enqueue after server restart, and
    // lets a lazy light-only request skip straight past a full GLB that
    // was already produced earlier.
    const bool full_exists  = seaweedfs_.exists(job.user, job.repo, job.sha, job.file_path, false);
    const bool light_exists = job.with_light &&
        seaweedfs_.exists(job.user, job.repo, job.sha, job.file_path, true);

    bool need_run = false;
    {
        std::lock_guard lk{status_mtx_};

        if (full_exists) {
            status_map_[full_key] = JobStatus::Ready;
        } else if (!status_map_.count(full_key)) {
            status_map_[full_key] = JobStatus::Pending;
            need_run = true;
        }

        if (job.with_light) {
            if (light_exists) {
                status_map_[light_key] = JobStatus::Ready;
            } else if (!status_map_.count(light_key)) {
                status_map_[light_key] = JobStatus::Pending;
                need_run = true;
            }
        }
    }

    if (!need_run) { return; } // both variants already tracked/ready

    {
        std::lock_guard lk{queue_mtx_};
        queue_.push_back(std::move(job));
    }
    cv_.notify_one();
}

JobStatus CadPipeline::status(const std::string& user,
                               const std::string& repo,
                               const std::string& sha,
                               const std::string& file_path,
                               bool               light) const {
    const std::string key = job_key(user, repo, sha, file_path, light);

    {
        std::lock_guard lk{status_mtx_};
        const auto it = status_map_.find(key);
        if (it != status_map_.end()) { return it->second; }
    }

    // Cache miss (e.g. after server restart): check SeaweedFS directly.
    if (seaweedfs_.exists(user, repo, sha, file_path, light)) { return JobStatus::Ready; }
    return JobStatus::Pending;
}

bool CadPipeline::has_tracked_job(const std::string& user,
                                   const std::string& repo,
                                   const std::string& sha,
                                   const std::string& file_path,
                                   bool               light) const {
    const std::string key = job_key(user, repo, sha, file_path, light);
    std::lock_guard lk{status_mtx_};
    return status_map_.count(key) > 0;
}

// ─── Worker ──────────────────────────────────────────────────────────────────

namespace {

// Writes `doc` to a temp GLB (cleaned up via RAII regardless of outcome)
// and uploads it. Returns false on either step's failure.
bool write_and_upload(storage::SeaweedFsClient&        seaweedfs,
                      const CadJob&                     job,
                      const Handle(TDocStd_Document)&   doc,
                      const std::string&                links_json,
                      const std::string&                tmp_name,
                      bool                               light) {
    const fs::path tmp_path = fs::temp_directory_path() / tmp_name;
    struct TempGuard {
        const fs::path& path;
        ~TempGuard() { std::error_code ec; fs::remove(path, ec); }
    } guard{tmp_path};

    if (!write_glb(doc, tmp_path, links_json)) { return false; }
    return seaweedfs.upload(job.user, job.repo, job.sha, job.file_path, tmp_path, light);
}

} // namespace

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

        const std::string full_key  = job_key(job.user, job.repo, job.sha, job.file_path, false);
        const std::string light_key = job.with_light
            ? job_key(job.user, job.repo, job.sha, job.file_path, true) : std::string{};
        {
            std::lock_guard lk{status_mtx_};
            status_map_[full_key] = JobStatus::Processing;
            if (job.with_light) { status_map_[light_key] = JobStatus::Processing; }
        }

        // process_job() sets each variant's status to Ready itself as that
        // variant completes (a light-generation failure is caught inside
        // process_job and only downgrades the light status — it never
        // throws out to here). An exception escaping this far means the
        // conversion never got off the ground at all (bad blob, unreadable
        // CAD file), so every variant this job was tracking goes to Error.
        try {
            process_job(job);
        } catch (const std::exception& e) {
            spdlog::error("CadPipeline: job {} failed: {}", full_key, e.what());
            std::lock_guard lk{status_mtx_};
            status_map_[full_key] = JobStatus::Error;
            if (job.with_light) { status_map_[light_key] = JobStatus::Error; }
        } catch (...) {
            spdlog::error("CadPipeline: job {} failed (unknown exception)", full_key);
            std::lock_guard lk{status_mtx_};
            status_map_[full_key] = JobStatus::Error;
            if (job.with_light) { status_map_[light_key] = JobStatus::Error; }
        }
    }
}

void CadPipeline::process_job(const CadJob& job) {
    spdlog::info("CadPipeline: converting {}/{}/{}/{}",
                 job.user, job.repo, job.sha, job.file_path);

    const std::string full_key  = job_key(job.user, job.repo, job.sha, job.file_path, false);
    const std::string light_key = job.with_light
        ? job_key(job.user, job.repo, job.sha, job.file_path, true) : std::string{};
    const std::string tmp_stem = full_key.substr(full_key.rfind('/') + 1);

    // Determine file type by extension (case-insensitive).
    std::string lower_path = job.file_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    std::optional<Handle(TDocStd_Document)> doc_opt{};
    std::string links_json{};

    if (lower_path.ends_with(".step") || lower_path.ends_with(".stp")) {
        std::lock_guard parse_lk{parse_mtx_};
        doc_opt = cad::read_step(job.blob_data);
    } else if (lower_path.ends_with(".fcstd")) {
        // App::Link references are never followed here — they're embedded as
        // glTF metadata for the frontend to resolve (fetch each linked
        // file's own GLB and compose it into the scene client-side).
        std::lock_guard parse_lk{parse_mtx_};
        if (auto fcstd_result = cad::read_fcstd(job.blob_data, job.file_path)) {
            doc_opt    = fcstd_result->doc;
            links_json = links_to_json(fcstd_result->links);
        }
    } else {
        spdlog::warn("CadPipeline: unsupported file type '{}'", job.file_path);
        // Matches the pre-existing behavior of leaving this tracked as
        // Ready even though no GLB exists — not something introduced here.
        std::lock_guard lk{status_mtx_};
        status_map_[full_key] = JobStatus::Ready;
        if (job.with_light) { status_map_[light_key] = JobStatus::Ready; }
        return;
    }

    if (!doc_opt) {
        throw std::runtime_error("CAD read failed: " + job.file_path);
    }

    // Full-detail GLB — skip the (re)write if it's already on SeaweedFS, so
    // a lazily-enqueued light-only job doesn't redo this for nothing.
    if (!seaweedfs_.exists(job.user, job.repo, job.sha, job.file_path, false)) {
        if (!write_and_upload(seaweedfs_, job, *doc_opt, links_json, tmp_stem + ".glb", false)) {
            throw std::runtime_error("full GLB write/upload failed for: " + job.file_path);
        }
        spdlog::info("CadPipeline: uploaded full GLB to SeaweedFS for {}/{}/{}/{}",
                     job.user, job.repo, job.sha, job.file_path);
    }
    {
        std::lock_guard lk{status_mtx_};
        status_map_[full_key] = JobStatus::Ready;
    }

    if (!job.with_light) { return; }

    if (seaweedfs_.exists(job.user, job.repo, job.sha, job.file_path, true)) {
        std::lock_guard lk{status_mtx_};
        status_map_[light_key] = JobStatus::Ready;
        return;
    }

    // Best-effort: a light-generation failure never rolls back the
    // already-successful full conversion above — only the light variant's
    // status reflects it.
    try {
        auto light_doc = make_light_document(*doc_opt);
        if (!light_doc) {
            throw std::runtime_error("light document has no shapes to export");
        }
        if (!write_and_upload(seaweedfs_, job, *light_doc, links_json, tmp_stem + "-light.glb", true)) {
            throw std::runtime_error("light GLB write/upload failed for: " + job.file_path);
        }
        spdlog::info("CadPipeline: uploaded light GLB to SeaweedFS for {}/{}/{}/{}",
                     job.user, job.repo, job.sha, job.file_path);

        std::lock_guard lk{status_mtx_};
        status_map_[light_key] = JobStatus::Ready;
    } catch (const std::exception& e) {
        spdlog::error("CadPipeline: light generation failed for {}/{}/{}/{}: {}",
                     job.user, job.repo, job.sha, job.file_path, e.what());
        std::lock_guard lk{status_mtx_};
        status_map_[light_key] = JobStatus::Error;
    }
}

} // namespace cad
