#ifndef FUNX_TEST_SCRATCH_DIR_H
#define FUNX_TEST_SCRATCH_DIR_H

#include <filesystem>

#include <funx/clock/Clock.h>

namespace funx::test {

class ScratchDir
{
public:
    ScratchDir();

    // not in the destructor because if a test aborts
    // midway, we don't want to clean up the scratch directory
    // in case there's debug info there
    void cleanup() const {
        std::filesystem::remove_all(path_);
    }

    const std::filesystem::path path() const {
        return path_;
    };

private:
    std::filesystem::path path_;

};

ScratchDir::ScratchDir()
{
    int64_t now = funx::Clock::now_ns();
    std::filesystem::path tmp{std::filesystem::temp_directory_path()};
    auto path = tmp / std::format("funx_scratch_dir_{}", now);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove_all(path);
    }
    std::filesystem::create_directory(path);
    path_ = path;
}

} // namespace funx::test

#endif  // FUNX_TEST_SCRATCH_DIR_H

