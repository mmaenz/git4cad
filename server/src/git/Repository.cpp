#include "Repository.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>

#include <spdlog/spdlog.h>

namespace git {

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void Repository::init()     { git_libgit2_init(); }
void Repository::shutdown() { git_libgit2_shutdown(); }

// ─── Factory methods ─────────────────────────────────────────────────────────

std::optional<Repository> Repository::open(const fs::path& repo_path) {
    git_repository* raw = nullptr;
    if (git_repository_open_bare(&raw, repo_path.string().c_str()) != 0) {
        spdlog::debug("git_repository_open_bare failed for '{}'", repo_path.string());
        return std::nullopt;
    }
    return Repository{GitRepoPtr{raw}, repo_path};
}

std::optional<Repository> Repository::init_bare(const fs::path& repo_path) {
    git_repository* raw = nullptr;
    if (git_repository_init(&raw, repo_path.string().c_str(), /*bare=*/1) != 0) {
        spdlog::warn("git_repository_init failed for '{}'", repo_path.string());
        return std::nullopt;
    }
    return Repository{GitRepoPtr{raw}, repo_path};
}

bool Repository::create_initial_commit(const std::string& repo_name,
                                       const std::string& description,
                                       const std::string& owner)
{
    // Build README.md content
    std::string readme = "# " + repo_name + "\n";
    if (!description.empty()) readme += "\n" + description + "\n";

    // Blob
    git_oid blob_oid{};
    if (git_blob_create_from_buffer(&blob_oid, repo_.get(),
                                    readme.data(), readme.size()) != 0) {
        spdlog::warn("create_initial_commit: blob creation failed");
        return false;
    }

    // Tree
    git_treebuilder* tb_raw = nullptr;
    if (git_treebuilder_new(&tb_raw, repo_.get(), nullptr) != 0) return false;
    std::unique_ptr<git_treebuilder, decltype(&git_treebuilder_free)>
        tb{tb_raw, git_treebuilder_free};

    if (git_treebuilder_insert(nullptr, tb.get(), "README.md",
                               &blob_oid, GIT_FILEMODE_BLOB) != 0) return false;

    git_oid tree_oid{};
    if (git_treebuilder_write(&tree_oid, tb.get()) != 0) return false;

    git_tree* tree_raw = nullptr;
    if (git_tree_lookup(&tree_raw, repo_.get(), &tree_oid) != 0) return false;
    GitTreePtr tree{tree_raw};

    // Signature
    const std::string email = owner + "@git4cad.local";
    git_signature* sig_raw = nullptr;
    if (git_signature_now(&sig_raw, owner.c_str(), email.c_str()) != 0) return false;
    std::unique_ptr<git_signature, decltype(&git_signature_free)>
        sig{sig_raw, git_signature_free};

    // Commit (no parents — initial commit; "HEAD" creates refs/heads/main)
    git_oid commit_oid{};
    if (git_commit_create(&commit_oid, repo_.get(), "HEAD",
                          sig.get(), sig.get(),
                          nullptr, "Initial commit",
                          tree.get(), 0, nullptr) != 0) {
        spdlog::warn("create_initial_commit: git_commit_create failed");
        return false;
    }
    return true;
}

void Repository::configure_lfs(const fs::path& repo_path) {
    const fs::path info_dir = repo_path / "info";
    std::error_code ec{};
    fs::create_directories(info_dir, ec);

    std::ofstream f(info_dir / "attributes");
    if (!f) {
        spdlog::error("Repository::configure_lfs: cannot write info/attributes in '{}'",
                      repo_path.string());
        return;
    }
    // Track all common CAD file formats with LFS.
    f << "*.step  filter=lfs diff=lfs merge=lfs -text\n"
      << "*.stp   filter=lfs diff=lfs merge=lfs -text\n"
      << "*.STEP  filter=lfs diff=lfs merge=lfs -text\n"
      << "*.STP   filter=lfs diff=lfs merge=lfs -text\n"
      << "*.fcstd filter=lfs diff=lfs merge=lfs -text\n"
      << "*.FCStd filter=lfs diff=lfs merge=lfs -text\n"
      << "*.iges  filter=lfs diff=lfs merge=lfs -text\n"
      << "*.igs   filter=lfs diff=lfs merge=lfs -text\n"
      << "*.IGES  filter=lfs diff=lfs merge=lfs -text\n"
      << "*.IGS   filter=lfs diff=lfs merge=lfs -text\n"
      << "*.stl   filter=lfs diff=lfs merge=lfs -text\n"
      << "*.STL   filter=lfs diff=lfs merge=lfs -text\n";
    spdlog::debug("Repository::configure_lfs: wrote info/attributes for '{}'",
                  repo_path.string());
}

// ─── Private helpers ─────────────────────────────────────────────────────────

std::optional<GitCommitPtr> Repository::resolve_commit(const std::string& refish) const {
    git_object* obj = nullptr;
    if (git_revparse_single(&obj, repo_.get(), refish.c_str()) != 0) return std::nullopt;

    git_commit* commit = nullptr;
    int rc = git_object_peel(reinterpret_cast<git_object**>(&commit), obj,
                             GIT_OBJECT_COMMIT);
    git_object_free(obj);
    if (rc != 0) return std::nullopt;
    return GitCommitPtr{commit};
}

static CommitInfo commit_to_info(const git_commit* c) {
    CommitInfo info{};
    char oid_str[GIT_OID_HEXSZ + 1] = {};
    git_oid_tostr(oid_str, sizeof(oid_str), git_commit_id(c));
    info.sha       = oid_str;
    info.message   = git_commit_message(c) ? git_commit_message(c) : "";
    if (const git_signature* sig = git_commit_author(c)) {
        info.author    = sig->name  ? sig->name  : "";
        info.email     = sig->email ? sig->email : "";
        info.timestamp = static_cast<int64_t>(sig->when.time);
    }
    return info;
}

// ─── Public API ──────────────────────────────────────────────────────────────

std::vector<CommitInfo> Repository::list_commits(const std::string& ref,
                                                  int limit,
                                                  int offset) const {
    std::vector<CommitInfo> results{};

    auto maybe_commit = resolve_commit(ref);
    if (!maybe_commit) return results;

    git_revwalk* walk_raw = nullptr;
    if (git_revwalk_new(&walk_raw, repo_.get()) != 0) return results;
    GitRevwalkPtr walk{walk_raw};

    git_revwalk_sorting(walk.get(), GIT_SORT_TIME);
    git_revwalk_push(walk.get(), git_commit_id(maybe_commit->get()));

    int skipped = 0;
    int collected = 0;
    git_oid oid{};

    while (git_revwalk_next(&oid, walk.get()) == 0) {
        if (skipped++ < offset) continue;
        if (limit > 0 && collected >= limit) break;

        git_commit* raw = nullptr;
        if (git_commit_lookup(&raw, repo_.get(), &oid) != 0) continue;
        results.push_back(commit_to_info(raw));
        git_commit_free(raw);
        ++collected;
    }

    return results;
}

std::optional<CommitInfo> Repository::get_commit(const std::string& sha) const {
    git_oid oid{};
    if (git_oid_fromstr(&oid, sha.c_str()) != 0) return std::nullopt;

    git_commit* raw = nullptr;
    if (git_commit_lookup(&raw, repo_.get(), &oid) != 0) return std::nullopt;
    GitCommitPtr commit{raw};
    return commit_to_info(commit.get());
}

std::vector<TreeEntry> Repository::list_tree(const std::string& ref,
                                              const std::string& path) const {
    std::vector<TreeEntry> results{};

    auto maybe_commit = resolve_commit(ref);
    if (!maybe_commit) return results;

    git_tree* root_tree_raw = nullptr;
    if (git_commit_tree(&root_tree_raw, maybe_commit->get()) != 0) return results;
    GitTreePtr root_tree{root_tree_raw};

    git_tree* target_tree_raw = nullptr;
    if (path.empty()) {
        target_tree_raw = root_tree.release();
    } else {
        git_tree_entry* entry_raw = nullptr;
        if (git_tree_entry_bypath(&entry_raw, root_tree.get(), path.c_str()) != 0)
            return results;
        const std::unique_ptr<git_tree_entry, decltype(&git_tree_entry_free)>
            entry{entry_raw, git_tree_entry_free};

        git_object* obj = nullptr;
        if (git_tree_entry_to_object(&obj, repo_.get(), entry.get()) != 0) return results;
        if (git_object_type(obj) != GIT_OBJECT_TREE) { git_object_free(obj); return results; }
        target_tree_raw = reinterpret_cast<git_tree*>(obj);
    }
    GitTreePtr target_tree{target_tree_raw};

    const std::size_t count = git_tree_entrycount(target_tree.get());
    for (std::size_t i = 0; i < count; ++i) {
        const git_tree_entry* e = git_tree_entry_byindex(target_tree.get(), i);
        TreeEntry te{};
        te.name = git_tree_entry_name(e);
        te.sha  = []( const git_oid* oid) {
            char buf[GIT_OID_HEXSZ + 1] = {};
            git_oid_tostr(buf, sizeof(buf), oid);
            return std::string{buf};
        }(git_tree_entry_id(e));
        const git_filemode_t mode = git_tree_entry_filemode(e);
        if (mode == GIT_FILEMODE_TREE) {
            te.type = "tree";
        } else {
            te.type = "blob";
            // Get blob size
            git_blob* blob_raw = nullptr;
            if (git_blob_lookup(&blob_raw, repo_.get(), git_tree_entry_id(e)) == 0) {
                te.size = static_cast<int64_t>(git_blob_rawsize(blob_raw));
                git_blob_free(blob_raw);
            }
        }
        results.push_back(std::move(te));
    }

    return results;
}

std::optional<std::vector<uint8_t>> Repository::read_blob(const std::string& ref,
                                                            const std::string& path) const {
    auto maybe_commit = resolve_commit(ref);
    if (!maybe_commit) return std::nullopt;

    git_tree* tree_raw = nullptr;
    if (git_commit_tree(&tree_raw, maybe_commit->get()) != 0) return std::nullopt;
    GitTreePtr tree{tree_raw};

    git_tree_entry* entry_raw = nullptr;
    if (git_tree_entry_bypath(&entry_raw, tree.get(), path.c_str()) != 0) return std::nullopt;
    const std::unique_ptr<git_tree_entry, decltype(&git_tree_entry_free)>
        entry{entry_raw, git_tree_entry_free};

    git_blob* blob_raw = nullptr;
    if (git_blob_lookup(&blob_raw, repo_.get(), git_tree_entry_id(entry.get())) != 0)
        return std::nullopt;
    GitBlobPtr blob{blob_raw};

    const void*   raw  = git_blob_rawcontent(blob.get());
    const int64_t size = git_blob_rawsize(blob.get());
    if (!raw || size <= 0) return std::vector<uint8_t>{};

    const auto* p = static_cast<const uint8_t*>(raw);
    return std::vector<uint8_t>{p, p + size};
}

std::optional<std::vector<uint8_t>> Repository::read_blob_at_commit(
        const std::string& commit_sha,
        const std::string& file_path) const {
    return read_blob(commit_sha, file_path);
}

std::map<std::string, std::string> Repository::snapshot_refs() const {
    std::map<std::string, std::string> result{};

    git_reference_foreach(repo_.get(), [](git_reference* ref, void* payload) -> int {
        auto* m = static_cast<std::map<std::string, std::string>*>(payload);
        const git_oid* oid = git_reference_target(ref);
        if (!oid) {
            // Symbolic ref — peel
            git_reference* resolved = nullptr;
            if (git_reference_resolve(&resolved, ref) == 0) {
                oid = git_reference_target(resolved);
                if (oid) {
                    char buf[GIT_OID_HEXSZ + 1] = {};
                    git_oid_tostr(buf, sizeof(buf), oid);
                    (*m)[git_reference_name(ref)] = buf;
                }
                git_reference_free(resolved);
            }
        } else {
            char buf[GIT_OID_HEXSZ + 1] = {};
            git_oid_tostr(buf, sizeof(buf), oid);
            (*m)[git_reference_name(ref)] = buf;
        }
        return 0;
    }, &result);

    return result;
}

// Returns true if the filename has a CAD extension (case-insensitive).
static bool is_cad_file(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return lower.ends_with(".step") || lower.ends_with(".stp") || lower.ends_with(".fcstd");
}

// Collect CAD file paths changed in a commit vs its first parent.
static std::vector<std::string> cad_files_in_commit(git_repository*   repo,
                                                     const git_commit* commit) {
    std::vector<std::string> result{};

    git_tree* new_tree_raw = nullptr;
    if (git_commit_tree(&new_tree_raw, commit) != 0) return result;
    GitTreePtr new_tree{new_tree_raw};

    git_tree* old_tree_raw = nullptr;
    if (git_commit_parentcount(commit) > 0) {
        git_commit* parent_raw = nullptr;
        if (git_commit_parent(&parent_raw, commit, 0) == 0) {
            GitCommitPtr parent{parent_raw};
            git_commit_tree(&old_tree_raw, parent.get());
        }
    }
    GitTreePtr old_tree{old_tree_raw};

    git_diff* diff_raw = nullptr;
    git_diff_tree_to_tree(&diff_raw, repo, old_tree.get(), new_tree.get(), nullptr);
    if (!diff_raw) return result;
    GitDiffPtr diff{diff_raw};

    const std::size_t num_deltas = git_diff_num_deltas(diff.get());
    for (std::size_t d = 0; d < num_deltas; ++d) {
        const git_diff_delta* delta = git_diff_get_delta(diff.get(), d);
        if (!delta || !delta->new_file.path) continue;
        std::string fp{delta->new_file.path};
        if (is_cad_file(fp)) result.push_back(std::move(fp));
    }
    return result;
}

void Repository::walk_new_commits(
        const std::string& old_sha,
        const std::string& new_sha,
        const std::function<void(const CommitInfo&, const std::vector<std::string>&)>& cb) const {

    git_revwalk* walk_raw = nullptr;
    if (git_revwalk_new(&walk_raw, repo_.get()) != 0) return;
    GitRevwalkPtr walk{walk_raw};

    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE);

    git_oid new_oid{};
    if (git_oid_fromstr(&new_oid, new_sha.c_str()) != 0) return;
    git_revwalk_push(walk.get(), &new_oid);

    if (!old_sha.empty() && old_sha != "0000000000000000000000000000000000000000") {
        git_oid old_oid{};
        if (git_oid_fromstr(&old_oid, old_sha.c_str()) == 0) {
            git_revwalk_hide(walk.get(), &old_oid);
        }
    }

    git_oid oid{};
    while (git_revwalk_next(&oid, walk.get()) == 0) {
        git_commit* raw = nullptr;
        if (git_commit_lookup(&raw, repo_.get(), &oid) != 0) continue;
        GitCommitPtr commit{raw};

        const CommitInfo             info    = commit_to_info(commit.get());
        const std::vector<std::string> files = cad_files_in_commit(repo_.get(), commit.get());
        if (!files.empty()) cb(info, files);
    }
}

std::string Repository::default_branch() const {
    git_reference* head_raw = nullptr;
    if (git_repository_head(&head_raw, repo_.get()) != 0) return "main";
    GitRefPtr head{head_raw};
    const char* name = git_reference_shorthand(head.get());
    return name ? name : "main";
}

bool Repository::is_empty() const {
    return git_repository_is_empty(repo_.get()) == 1;
}

} // namespace git
