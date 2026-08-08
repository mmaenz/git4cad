#include "UserStore.hpp"

#include <mutex>

#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>

#include "util/Hex.hpp"

namespace auth {

// ─── Impl ────────────────────────────────────────────────────────────────────

struct UserStore::Impl {
    mutable std::mutex mtx{};
    SQLite::Database   db;

    explicit Impl(const fs::path& path)
        : db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS users (
                username      TEXT PRIMARY KEY,
                password_hash TEXT NOT NULL,
                token         TEXT NOT NULL
            )
        )");
        // WAL for better concurrent read performance.
        db.exec("PRAGMA journal_mode=WAL");
        db.exec("PRAGMA synchronous=NORMAL");
    }
};

// ─── UserStore ───────────────────────────────────────────────────────────────

std::optional<UserStore> UserStore::open(fs::path path) {
    try {
        fs::create_directories(path.parent_path());
        return UserStore(std::make_unique<Impl>(path));
    } catch (const SQLite::Exception& e) {
        spdlog::error("UserStore: failed to open '{}': {}", path.string(), e.what());
        return std::nullopt;
    }
}

UserStore::UserStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

UserStore::~UserStore() = default;

// ─── Static helpers ───────────────────────────────────────────────────────────

std::string UserStore::make_salt() {
    uint8_t buf[16] = {};
    RAND_bytes(buf, sizeof(buf));
    return util::to_hex(buf, sizeof(buf));
}

std::string UserStore::make_token() {
    uint8_t buf[32] = {};
    RAND_bytes(buf, sizeof(buf));
    return util::to_hex(buf, sizeof(buf));
}

std::string UserStore::hash_password(std::string_view salt, std::string_view password) {
    const std::string salted = std::string(salt) + std::string(password);
    return util::sha256_hex(salted.data(), salted.size());
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool UserStore::user_exists(std::string_view user) const {
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT 1 FROM users WHERE username = ?"};
        q.bind(1, std::string(user));
        return q.executeStep();
    } catch (const SQLite::Exception& e) {
        spdlog::error("UserStore::user_exists: {}", e.what());
        return false;
    }
}

bool UserStore::validate_token(std::string_view user, std::string_view token) const {
    if (user.empty() || token.empty()) return false;
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT 1 FROM users WHERE username = ? AND token = ?"};
        q.bind(1, std::string(user));
        q.bind(2, std::string(token));
        return q.executeStep();
    } catch (const SQLite::Exception& e) {
        spdlog::error("UserStore::validate_token: {}", e.what());
        return false;
    }
}

bool UserStore::validate_password(std::string_view user, std::string_view password) const {
    if (user.empty() || password.empty()) return false;
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT password_hash FROM users WHERE username = ?"};
        q.bind(1, std::string(user));
        if (!q.executeStep()) return false;

        const std::string ph    = q.getColumn(0).getText();
        const std::size_t colon = ph.find(':');
        if (colon == std::string::npos) return false;
        return ph.substr(colon + 1) == hash_password(ph.substr(0, colon), password);
    } catch (const SQLite::Exception& e) {
        spdlog::error("UserStore::validate_password: {}", e.what());
        return false;
    }
}

std::optional<std::string> UserStore::create_user(std::string_view user,
                                                    std::string_view password) {
    if (user.empty() || password.empty()) return std::nullopt;
    const std::string salt  = make_salt();
    const std::string ph    = salt + ":" + hash_password(salt, password);
    const std::string token = make_token();
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "INSERT INTO users (username, password_hash, token) VALUES (?, ?, ?)"};
        q.bind(1, std::string(user));
        q.bind(2, ph);
        q.bind(3, token);
        q.exec();
        return token;
    } catch (const SQLite::Exception& e) {
        if (e.getErrorCode() == SQLITE_CONSTRAINT) return std::nullopt; // duplicate username
        spdlog::error("UserStore::create_user: {}", e.what());
        return std::nullopt;
    }
}

std::optional<std::string> UserStore::login(std::string_view user, std::string_view password) {
    if (user.empty() || password.empty()) return std::nullopt;
    std::lock_guard lk{impl_->mtx};
    try {
        SQLite::Statement q{impl_->db,
            "SELECT password_hash, token FROM users WHERE username = ?"};
        q.bind(1, std::string(user));
        if (!q.executeStep()) return std::nullopt;

        const std::string ph    = q.getColumn(0).getText();
        const std::string token = q.getColumn(1).getText();
        const std::size_t colon = ph.find(':');
        if (colon == std::string::npos) return std::nullopt;
        if (ph.substr(colon + 1) != hash_password(ph.substr(0, colon), password))
            return std::nullopt;
        return token;
    } catch (const SQLite::Exception& e) {
        spdlog::error("UserStore::login: {}", e.what());
        return std::nullopt;
    }
}

} // namespace auth
