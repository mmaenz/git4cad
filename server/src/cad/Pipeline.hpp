#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "storage/SeaweedFsClient.hpp"

namespace cad {

enum class JobStatus { Pending, Processing, Ready, Error };

struct CadJob {
    std::string           user{};
    std::string           repo{};
    std::string           sha{};
    std::string           file_path{};
    std::vector<uint8_t>  blob_data{};
    // Whether this job should also produce the lightweight hull-GLB variant
    // (in addition to the full-detail one) once the CAD kernel has parsed
    // the file — reusing that same in-memory document rather than parsing
    // the file a second time.
    bool                   with_light{false};
};

class CadPipeline {
public:
    explicit CadPipeline(storage::SeaweedFsClient& seaweedfs, int num_workers = 4);
    ~CadPipeline();

    CadPipeline(const CadPipeline&)            = delete;
    CadPipeline& operator=(const CadPipeline&) = delete;
    CadPipeline(CadPipeline&&)                 = delete;
    CadPipeline& operator=(CadPipeline&&)      = delete;

    /// Enqueue a conversion job. Ensures the full-detail GLB exists; if
    /// `job.with_light` is set, also ensures the light GLB exists. No-op
    /// for whichever variant(s) already exist in SeaweedFS — safe to call
    /// repeatedly, including to lazily request just the light variant for a
    /// file whose full GLB was already produced earlier.
    void enqueue(CadJob job);

    /// Query conversion status for a variant. Falls back to SeaweedFS HEAD
    /// check on cache miss.
    [[nodiscard]] JobStatus status(const std::string& user,
                                   const std::string& repo,
                                   const std::string& sha,
                                   const std::string& file_path,
                                   bool               light = false) const;

    /// True if this job/variant is already tracked (queued, in progress, or
    /// done) — a cheap in-memory check callers can use before doing the
    /// work to re-enqueue a job that may simply have never been seen (e.g.
    /// after a server restart lost the original push-time enqueue, or a
    /// repo had `resolve_links` switched on after this file was pushed).
    [[nodiscard]] bool has_tracked_job(const std::string& user,
                                       const std::string& repo,
                                       const std::string& sha,
                                       const std::string& file_path,
                                       bool               light = false) const;

private:
    static std::string job_key(const std::string& user,
                               const std::string& repo,
                               const std::string& sha,
                               const std::string& file_path,
                               bool               light = false);

    void worker_loop();
    void process_job(const CadJob& job);

    storage::SeaweedFsClient&        seaweedfs_;
    std::deque<CadJob>               queue_{};
    mutable std::mutex               queue_mtx_{};
    std::condition_variable          cv_{};
    bool                             stopping_{false};

    mutable std::mutex               status_mtx_{};
    std::map<std::string, JobStatus> status_map_{};

    // OCCT's STEP/XSTEP reader relies on process-global interface state
    // (Interface_Static et al.) that isn't safe under concurrent reads —
    // two worker threads parsing CAD files at the same time can corrupt
    // each other and crash the process. Only the parse step is this
    // fragile (BRepMesh/offset/defeaturing/sewing/healing all operate on
    // already-parsed, independent in-memory shapes and are fine
    // concurrently), so only it is serialized.
    std::mutex                       parse_mtx_{};

    std::vector<std::thread>         workers_{};
};

} // namespace cad
