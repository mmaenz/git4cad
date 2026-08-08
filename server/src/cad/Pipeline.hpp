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
};

class CadPipeline {
public:
    explicit CadPipeline(storage::SeaweedFsClient& seaweedfs, int num_workers = 4);
    ~CadPipeline();

    CadPipeline(const CadPipeline&)            = delete;
    CadPipeline& operator=(const CadPipeline&) = delete;
    CadPipeline(CadPipeline&&)                 = delete;
    CadPipeline& operator=(CadPipeline&&)      = delete;

    /// Enqueue a conversion job. No-op if GLB already exists in SeaweedFS.
    void enqueue(CadJob job);

    /// Query conversion status. Falls back to SeaweedFS HEAD check on cache miss.
    [[nodiscard]] JobStatus status(const std::string& user,
                                   const std::string& repo,
                                   const std::string& sha,
                                   const std::string& file_path) const;

    /// True if this job is already tracked (queued, in progress, or done) —
    /// a cheap in-memory check callers can use before doing the work to
    /// re-enqueue a job that may simply have never been seen (e.g. after a
    /// server restart lost the original push-time enqueue).
    [[nodiscard]] bool has_tracked_job(const std::string& user,
                                       const std::string& repo,
                                       const std::string& sha,
                                       const std::string& file_path) const;

private:
    static std::string job_key(const std::string& user,
                               const std::string& repo,
                               const std::string& sha,
                               const std::string& file_path);

    void worker_loop();
    void process_job(const CadJob& job) const;

    storage::SeaweedFsClient&        seaweedfs_;
    std::deque<CadJob>               queue_{};
    mutable std::mutex               queue_mtx_{};
    std::condition_variable          cv_{};
    bool                             stopping_{false};

    mutable std::mutex               status_mtx_{};
    std::map<std::string, JobStatus> status_map_{};

    std::vector<std::thread>         workers_{};
};

} // namespace cad
