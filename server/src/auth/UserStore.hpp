#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace auth {

namespace fs = std::filesystem;

/// Thread-safe persistent user store backed by a SQLite database.
class UserStore {
public:
    explicit UserStore(fs::path path);
    ~UserStore();

    // Non-copyable, moveable
    UserStore(const UserStore&)            = delete;
    UserStore& operator=(const UserStore&) = delete;
    UserStore(UserStore&&)                 = default;
    UserStore& operator=(UserStore&&)      = default;

    /// Returns true if the bearer token is valid for the given user.
    [[nodiscard]] bool validate_token(std::string_view user, std::string_view token) const;

    /// Returns true if username+password are correct.
    [[nodiscard]] bool validate_password(std::string_view user, std::string_view password) const;

    /// Creates a new user. Returns the bearer token on success, nullopt if user already exists.
    [[nodiscard]] std::optional<std::string> create_user(std::string_view user,
                                                          std::string_view password);

    /// Validates password and returns the bearer token on success.
    [[nodiscard]] std::optional<std::string> login(std::string_view user,
                                                    std::string_view password);

    [[nodiscard]] bool user_exists(std::string_view user) const;

    // Forward-declared pimpl
    struct Impl;

private:
    [[nodiscard]] static std::string make_salt();
    [[nodiscard]] static std::string make_token();
    [[nodiscard]] static std::string hash_password(std::string_view salt,
                                                   std::string_view password);

    std::unique_ptr<Impl> impl_;
};

} // namespace auth
