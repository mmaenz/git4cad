#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <git2.h>

namespace git {

namespace fs = std::filesystem;

// ─── RAII deleters ───────────────────────────────────────────────────────────

struct RepoDeleter    { void operator()(git_repository* p) const noexcept { git_repository_free(p); } };
struct CommitDeleter  { void operator()(git_commit*     p) const noexcept { git_commit_free(p);     } };
struct TreeDeleter    { void operator()(git_tree*       p) const noexcept { git_tree_free(p);       } };
struct BlobDeleter    { void operator()(git_blob*       p) const noexcept { git_blob_free(p);       } };
struct RevwalkDeleter { void operator()(git_revwalk*    p) const noexcept { git_revwalk_free(p);    } };
struct RefDeleter     { void operator()(git_reference*  p) const noexcept { git_reference_free(p);  } };
struct DiffDeleter    { void operator()(git_diff*       p) const noexcept { git_diff_free(p);       } };

using GitRepoPtr    = std::unique_ptr<git_repository, RepoDeleter>;
using GitCommitPtr  = std::unique_ptr<git_commit,     CommitDeleter>;
using GitTreePtr    = std::unique_ptr<git_tree,       TreeDeleter>;
using GitBlobPtr    = std::unique_ptr<git_blob,       BlobDeleter>;
using GitRevwalkPtr = std::unique_ptr<git_revwalk,    RevwalkDeleter>;
using GitRefPtr     = std::unique_ptr<git_reference,  RefDeleter>;
using GitDiffPtr    = std::unique_ptr<git_diff,       DiffDeleter>;

// ─── Data types ──────────────────────────────────────────────────────────────

struct CommitInfo {
    std::string sha{};
    std::string message{};
    std::string author{};
    std::string email{};
    int64_t     timestamp{0};
};

struct TreeEntry {
    std::string name{};
    std::string type{}; // "blob" | "tree"
    std::string sha{};
    int64_t     size{0};
};

// ─── Repository ──────────────────────────────────────────────────────────────

class Repository {
public:
    // Must be called once before any Repository is created.
    static void init();
    // Must be called once at program exit.
    static void shutdown();

    /// Open an existing bare repository. Returns nullopt on failure.
    [[nodiscard]] static std::optional<Repository> open(const fs::path& repo_path);

    /// Initialize a new bare repository. Returns nullopt on failure.
    [[nodiscard]] static std::optional<Repository> init_bare(const fs::path& repo_path);

    /// Write info/attributes to make git-lfs track CAD files by default.
    static void configure_lfs(const fs::path& repo_path);

    // Non-copyable, moveable
    Repository(const Repository&)            = delete;
    Repository& operator=(const Repository&) = delete;
    Repository(Repository&&)                 = default;
    Repository& operator=(Repository&&)      = default;

    /// List commits on ref, with pagination.
    [[nodiscard]] std::vector<CommitInfo> list_commits(const std::string& ref,
                                                       int limit,
                                                       int offset) const;

    /// Get a single commit by SHA.
    [[nodiscard]] std::optional<CommitInfo> get_commit(const std::string& sha) const;

    /// List tree entries at path (empty = root) for given ref.
    [[nodiscard]] std::vector<TreeEntry> list_tree(const std::string& ref,
                                                   const std::string& path) const;

    /// Read raw blob bytes.
    [[nodiscard]] std::optional<std::vector<uint8_t>> read_blob(const std::string& ref,
                                                                 const std::string& path) const;

    /// Snapshot all ref tip OIDs. Returns map<refname, oid_hex>.
    [[nodiscard]] std::map<std::string, std::string> snapshot_refs() const;

    /// Walk commits reachable from new_sha but not from old_sha.
    /// Calls cb(CommitInfo, vector<string> changed_file_paths) for each new commit.
    void walk_new_commits(const std::string& old_sha,
                          const std::string& new_sha,
                          const std::function<void(const CommitInfo&,
                                                   const std::vector<std::string>&)>& cb) const;

    /// Read blob data for a specific file at a commit SHA.
    [[nodiscard]] std::optional<std::vector<uint8_t>> read_blob_at_commit(
            const std::string& commit_sha,
            const std::string& file_path) const;

    /// Returns the name of the default branch (HEAD target), or empty string.
    [[nodiscard]] std::string default_branch() const;

    /// Returns true if the repository has no commits.
    [[nodiscard]] bool is_empty() const;

    /// Create an initial commit containing README.md. Called once after init_bare.
    bool create_initial_commit(const std::string& repo_name,
                               const std::string& description,
                               const std::string& owner);

    const fs::path& path() const noexcept { return path_; }

private:
    explicit Repository(GitRepoPtr repo, fs::path path)
        : repo_(std::move(repo)), path_(std::move(path)) {}

    [[nodiscard]] std::optional<GitCommitPtr> resolve_commit(const std::string& refish) const;

    GitRepoPtr repo_{};
    fs::path   path_{};
};

} // namespace git
