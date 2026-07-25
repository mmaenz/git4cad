#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace g4c {

namespace fs = std::filesystem;

struct Config {
    std::string host                    = "0.0.0.0";
    int         port                    = 3000;
    fs::path    data_dir                = "./data";
    int         cad_workers             = 4;
    bool        debug                   = false;
    std::string seaweedfs_filer_url     = "http://seaweedfs:8888";
    std::string seaweedfs_public_prefix = "/seaweed";
    std::string public_url              = "";

    [[nodiscard]] fs::path repos_dir()  const { return data_dir / "repos"; }
    [[nodiscard]] fs::path users_db()   const { return data_dir / "users.db"; }
    [[nodiscard]] fs::path repos_db()   const { return data_dir / "repos.db"; }

    [[nodiscard]] static Config from_env() {
        Config cfg{};

        if (const char* v = std::getenv("G4C_HOST");     v) { cfg.host        = v; }
        if (const char* v = std::getenv("G4C_PORT");     v) { cfg.port        = std::stoi(v); }
        if (const char* v = std::getenv("G4C_DATA_DIR"); v) { cfg.data_dir    = v; }
        if (const char* v = std::getenv("G4C_CAD_WORKERS"); v) { cfg.cad_workers = std::stoi(v); }
        if (const char* v = std::getenv("G4C_DEBUG"); v) {
            const std::string s{v};
            cfg.debug = (s == "1" || s == "true" || s == "yes");
        }
        if (const char* v = std::getenv("G4C_SEAWEEDFS_FILER_URL");     v) {
            cfg.seaweedfs_filer_url = v;
        }
        if (const char* v = std::getenv("G4C_SEAWEEDFS_PUBLIC_PREFIX"); v) {
            cfg.seaweedfs_public_prefix = v;
        }
        if (const char* v = std::getenv("G4C_PUBLIC_URL"); v) {
            cfg.public_url = v;
        }

        return cfg;
    }
};

} // namespace g4c
