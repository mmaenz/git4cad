#pragma once

#include <filesystem>
#include <memory>
#include <optional>
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
    /// Opens (creating if needed) the SQLite-backed repo store. Returns
    /// nullopt if the database can't be opened.
    [[nodiscard]] static std::optional<RepoStore> open(const fs::path& db_path);

    ~RepoStore();

    RepoStore(const RepoStore&)            = delete;
    RepoStore& operator=(const RepoStore&) = delete;
    RepoStore(RepoStore&&)                 = default;
    RepoStore& operator=(RepoStore&&)      = default;

    void create_repo(const std::string& owner, const std::string& name, bool is_private);
    void delete_repo(const std::string& owner, const std::string& name);
    void set_private(const std::string& owner, const std::string& name, bool is_private);

    [[nodiscard]] bool is_private(const std::string& owner, const std::string& name) const;

    // Whether the 3D viewer should treat the Z axis as "up" (common for STEP/FreeCAD
    // data) instead of the default Y-up convention.
    void set_z_up(const std::string& owner, const std::string& name, bool z_up);

    [[nodiscard]] bool is_z_up(const std::string& owner, const std::string& name) const;

    // Whether the 3D viewer should resolve FreeCAD App::Link references and
    // merge the full linked assembly into one model, instead of showing only
    // the selected file's own shapes.
    void set_resolve_links(const std::string& owner, const std::string& name, bool resolve_links);

    [[nodiscard]] bool is_resolve_links(const std::string& owner, const std::string& name) const;

    // When resolve_links is on, whether the viewer stops tagging individual
    // parts past the first level of linked children — each direct child's
    // own sub-assemblies are still loaded and rendered, but merged into that
    // child as one clickable/selectable unit instead of each nested part
    // being selectable on its own. False (default) keeps every part in the
    // tree individually selectable.
    void set_group_child_assemblies(const std::string& owner, const std::string& name,
                                     bool group_child_assemblies);

    [[nodiscard]] bool is_group_child_assemblies(const std::string& owner,
                                                  const std::string& name) const;

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
    explicit RepoStore(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace store
