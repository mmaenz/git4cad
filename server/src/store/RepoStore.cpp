#include "RepoStore.hpp"

#include <mutex>

#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <spdlog/spdlog.h>

namespace store {

struct RepoStore::Impl {
    mutable std::mutex mtx{};
    SQLite::Database   db;

    explicit Impl(const fs::path& path)
        : db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS repos (
                owner   TEXT NOT NULL,
                name    TEXT NOT NULL,
                private INTEGER NOT NULL DEFAULT 0,
                z_up    INTEGER NOT NULL DEFAULT 0,
                resolve_links INTEGER NOT NULL DEFAULT 0,
                group_child_assemblies INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (owner, name)
            )
        )");
        // Migrations for databases created before these columns existed.
        try {
            db.exec("ALTER TABLE repos ADD COLUMN z_up INTEGER NOT NULL DEFAULT 0");
        } catch (const SQLite::Exception&) {
            // Column already present — nothing to do.
        }
        try {
            db.exec("ALTER TABLE repos ADD COLUMN resolve_links INTEGER NOT NULL DEFAULT 0");
        } catch (const SQLite::Exception&) {
            // Column already present — nothing to do.
        }
        try {
            db.exec("ALTER TABLE repos ADD COLUMN group_child_assemblies INTEGER NOT NULL DEFAULT 0");
        } catch (const SQLite::Exception&) {
            // Column already present — nothing to do.
        }
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS repo_members (
                owner    TEXT NOT NULL,
                repo     TEXT NOT NULL,
                username TEXT NOT NULL,
                can_push INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (owner, repo, username)
            )
        )");
        db.exec("PRAGMA journal_mode=WAL");
        db.exec("PRAGMA synchronous=NORMAL");
    }
};

std::optional<RepoStore> RepoStore::open(const fs::path& db_path) {
    try {
        fs::create_directories(db_path.parent_path());
        return RepoStore(std::make_unique<Impl>(db_path));
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore: failed to open '{}': {}", db_path.string(), e.what());
        return std::nullopt;
    }
}

RepoStore::RepoStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

RepoStore::~RepoStore() = default;

void RepoStore::create_repo(const std::string& owner, const std::string& name,
                             bool is_private) {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "INSERT OR IGNORE INTO repos (owner, name, private) VALUES (?, ?, ?)"};
        q.bind(1, owner);
        q.bind(2, name);
        q.bind(3, is_private ? 1 : 0);
        q.exec();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::create_repo: {}", e.what());
    }
}

void RepoStore::delete_repo(const std::string& owner, const std::string& name) {
    std::lock_guard lk{impl_->mtx};
    try {
        {
            SQLite::Statement q{impl_->db,
                "DELETE FROM repo_members WHERE owner = ? AND repo = ?"};
            q.bind(1, owner); q.bind(2, name);
            q.exec();
        }
        {
            SQLite::Statement q{impl_->db,
                "DELETE FROM repos WHERE owner = ? AND name = ?"};
            q.bind(1, owner); q.bind(2, name);
            q.exec();
        }
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::delete_repo: {}", e.what());
    }
}

void RepoStore::set_private(const std::string& owner, const std::string& name,
                             bool is_private) {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "UPDATE repos SET private = ? WHERE owner = ? AND name = ?"};
        q.bind(1, is_private ? 1 : 0); q.bind(2, owner); q.bind(3, name);
        q.exec();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::set_private: {}", e.what());
    }
}

bool RepoStore::is_private(const std::string& owner, const std::string& name) const {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT private FROM repos WHERE owner = ? AND name = ?"};
        q.bind(1, owner); q.bind(2, name);
        if (!q.executeStep()) return false; // unknown repo → public
        return q.getColumn(0).getInt() != 0;
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::is_private: {}", e.what());
        return false;
    }
}

void RepoStore::set_z_up(const std::string& owner, const std::string& name, bool z_up) {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "UPDATE repos SET z_up = ? WHERE owner = ? AND name = ?"};
        q.bind(1, z_up ? 1 : 0); q.bind(2, owner); q.bind(3, name);
        q.exec();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::set_z_up: {}", e.what());
    }
}

bool RepoStore::is_z_up(const std::string& owner, const std::string& name) const {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT z_up FROM repos WHERE owner = ? AND name = ?"};
        q.bind(1, owner); q.bind(2, name);
        if (!q.executeStep()) return false; // unknown repo → default Y-up
        return q.getColumn(0).getInt() != 0;
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::is_z_up: {}", e.what());
        return false;
    }
}

void RepoStore::set_resolve_links(const std::string& owner, const std::string& name,
                                   bool resolve_links) {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "UPDATE repos SET resolve_links = ? WHERE owner = ? AND name = ?"};
        q.bind(1, resolve_links ? 1 : 0); q.bind(2, owner); q.bind(3, name);
        q.exec();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::set_resolve_links: {}", e.what());
    }
}

bool RepoStore::is_resolve_links(const std::string& owner, const std::string& name) const {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT resolve_links FROM repos WHERE owner = ? AND name = ?"};
        q.bind(1, owner); q.bind(2, name);
        if (!q.executeStep()) return false; // unknown repo → single-file default
        return q.getColumn(0).getInt() != 0;
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::is_resolve_links: {}", e.what());
        return false;
    }
}

void RepoStore::set_group_child_assemblies(const std::string& owner, const std::string& name,
                                            bool group_child_assemblies) {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "UPDATE repos SET group_child_assemblies = ? WHERE owner = ? AND name = ?"};
        q.bind(1, group_child_assemblies ? 1 : 0); q.bind(2, owner); q.bind(3, name);
        q.exec();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::set_group_child_assemblies: {}", e.what());
    }
}

bool RepoStore::is_group_child_assemblies(const std::string& owner, const std::string& name) const {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT group_child_assemblies FROM repos WHERE owner = ? AND name = ?"};
        q.bind(1, owner); q.bind(2, name);
        if (!q.executeStep()) return false; // unknown repo → whole-tree default
        return q.getColumn(0).getInt() != 0;
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::is_group_child_assemblies: {}", e.what());
        return false;
    }
}

bool RepoStore::can_read(const std::string& owner, const std::string& name,
                          const std::string& user) const {
    if (!user.empty() && user == owner) return true;
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT private FROM repos WHERE owner = ? AND name = ?"};
        q.bind(1, owner); q.bind(2, name);
        if (!q.executeStep()) return true; // unknown repo → treat as public

        if (q.getColumn(0).getInt() == 0) return true; // public

        // private: check membership
        if (user.empty()) return false;
        SQLite::Statement mq{impl_->db,
            "SELECT 1 FROM repo_members WHERE owner = ? AND repo = ? AND username = ?"};
        mq.bind(1, owner); mq.bind(2, name); mq.bind(3, user);
        return mq.executeStep();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::can_read: {}", e.what());
        return false;
    }
}

bool RepoStore::can_push(const std::string& owner, const std::string& name,
                          const std::string& user) const {
    if (user.empty()) return false;
    if (user == owner) return true;
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT can_push FROM repo_members WHERE owner = ? AND repo = ? AND username = ?"};
        q.bind(1, owner); q.bind(2, name); q.bind(3, user);
        if (!q.executeStep()) return false;
        return q.getColumn(0).getInt() != 0;
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::can_push: {}", e.what());
        return false;
    }
}

void RepoStore::add_member(const std::string& owner, const std::string& name,
                            const std::string& username, bool can_push) {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "INSERT INTO repo_members (owner, repo, username, can_push) VALUES (?, ?, ?, ?)"
            " ON CONFLICT(owner, repo, username) DO UPDATE SET can_push = excluded.can_push"};
        q.bind(1, owner); q.bind(2, name); q.bind(3, username); q.bind(4, can_push ? 1 : 0);
        q.exec();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::add_member: {}", e.what());
    }
}

void RepoStore::remove_member(const std::string& owner, const std::string& name,
                               const std::string& username) {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "DELETE FROM repo_members WHERE owner = ? AND repo = ? AND username = ?"};
        q.bind(1, owner); q.bind(2, name); q.bind(3, username);
        q.exec();
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::remove_member: {}", e.what());
    }
}

std::vector<MemberInfo> RepoStore::list_members(const std::string& owner,
                                                  const std::string& name) const {
    std::lock_guard lk{impl_->mtx};
    std::vector<MemberInfo> result{};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT username, can_push FROM repo_members WHERE owner = ? AND repo = ?"
            " ORDER BY username"};
        q.bind(1, owner); q.bind(2, name);
        while (q.executeStep()) {
            result.push_back({
                .username = q.getColumn(0).getText(),
                .can_push = q.getColumn(1).getInt() != 0
            });
        }
    } catch (const SQLite::Exception& e) {
        spdlog::error("RepoStore::list_members: {}", e.what());
    }
    return result;
}

} // namespace store
