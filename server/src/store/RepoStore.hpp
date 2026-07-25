#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace store {

namespace fs = std::filesystem;

struct MemberInfo {
    std::string username;
    bool        can_push{false};
};

class RepoStore {
public:
    explicit RepoStore(const fs::path& db_path);
    ~RepoStore();

    RepoStore(const RepoStore&)            = delete;
    RepoStore& operator=(const RepoStore&) = delete;

    void create_repo(const std::string& owner, const std::string& name, bool is_private);
    void delete_repo(const std::string& owner, const std::string& name);
    void set_private(const std::string& owner, const std::string& name, bool is_private);

    [[nodiscard]] bool is_private(const std::string& owner, const std::string& name) const;

    // Returns true if `user` may read (clone/fetch) this repo.
    // Empty user = unauthenticated: only public repos allowed.
    [[nodiscard]] bool can_read(const std::string& owner, const std::string& name,
                                const std::string& user) const;

    // Returns true if `user` may push to this repo.
    [[nodiscard]] bool can_push(const std::string& owner, const std::string& name,
                                const std::string& user) const;

    void add_member(const std::string& owner, const std::string& name,
                    const std::string& username, bool can_push);
    void remove_member(const std::string& owner, const std::string& name,
                       const std::string& username);
    [[nodiscard]] std::vector<MemberInfo> list_members(const std::string& owner,
                                                        const std::string& name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace store
