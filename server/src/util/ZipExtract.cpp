#include "ZipExtract.hpp"

#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include <zip.h>
#include <spdlog/spdlog.h>

namespace util {

namespace {

struct ZipSourceDeleter {
    void operator()(zip_source_t* s) const noexcept { zip_source_free(s); }
};

struct ZipDeleter {
    void operator()(zip_t* z) const noexcept { zip_close(z); }
};

using ZipSourcePtr = std::unique_ptr<zip_source_t, ZipSourceDeleter>;
using ZipPtr       = std::unique_ptr<zip_t,        ZipDeleter>;

bool write_entry(zip_t* archive, zip_uint64_t index, const fs::path& dest) {
    zip_stat_t st{};
    if (zip_stat_index(archive, index, 0, &st) != 0) {
        spdlog::warn("zip_stat_index failed for entry {}", index);
        return false;
    }

    const std::string name{st.name};
    if (name.empty()) return true;                    // skip root entry
    if (name.back() == '/') {                         // directory entry
        fs::create_directories(dest / name);
        return true;
    }

    const fs::path out_path = dest / name;
    fs::create_directories(out_path.parent_path());

    zip_file_t* zf = zip_fopen_index(archive, index, 0);
    if (!zf) {
        spdlog::warn("zip_fopen_index failed for '{}'", name);
        return false;
    }

    std::ofstream ofs(out_path, std::ios::binary);
    if (!ofs) {
        zip_fclose(zf);
        spdlog::warn("Cannot open output file '{}'", out_path.string());
        return false;
    }

    constexpr std::size_t kBufSize = 65536;
    std::vector<char> buf(kBufSize);
    zip_int64_t n = 0;
    while ((n = zip_fread(zf, buf.data(), kBufSize)) > 0) {
        ofs.write(buf.data(), n);
    }
    zip_fclose(zf);

    return n >= 0;
}

} // namespace

bool extract_zip(const std::vector<uint8_t>& data, const fs::path& dest_dir) {
    zip_error_t err{};
    zip_error_init(&err);

    // zip_source_buffer_create requires the buffer to stay alive for the archive lifetime.
    zip_source_t* raw_src = zip_source_buffer_create(data.data(), data.size(), 0, &err);
    if (!raw_src) {
        spdlog::error("zip_source_buffer_create: {}", zip_error_strerror(&err));
        zip_error_fini(&err);
        return false;
    }
    // We take ownership via ZipSourcePtr, but open_from_source will steal it.
    ZipSourcePtr src_guard(raw_src);

    zip_t* raw_zip = zip_open_from_source(raw_src, ZIP_RDONLY, &err);
    if (!raw_zip) {
        spdlog::error("zip_open_from_source: {}", zip_error_strerror(&err));
        zip_error_fini(&err);
        return false;
    }
    // zip_open_from_source takes ownership of the source on success.
    src_guard.release();
    ZipPtr archive(raw_zip);

    zip_error_fini(&err);

    fs::create_directories(dest_dir);
    const zip_int64_t num_entries = zip_get_num_entries(archive.get(), 0);

    for (zip_int64_t i = 0; i < num_entries; ++i) {
        if (!write_entry(archive.get(), static_cast<zip_uint64_t>(i), dest_dir)) {
            return false;
        }
    }

    return true;
}

} // namespace util
