#include "app/services/stub_bank_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "engine/wav_loader.h"

namespace {

// Per-test temp directory fixture. Uses a unique subdirectory per test case
// so parallel ctest runs and aborted runs don't fight over shared paths.
class StubBankDirFixture {
public:
    explicit StubBankDirFixture(const char* name) {
        dir_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(dir_);
    }
    ~StubBankDirFixture() { std::filesystem::remove_all(dir_); }
    const std::filesystem::path& dir() const { return dir_; }

private:
    std::filesystem::path dir_;
};

}  // namespace

TEST_CASE("StubBankWriter writes 8 numbered WAV files", "[stub_bank_writer]") {
    StubBankDirFixture f("fim_stub_bank_test_files");

    REQUIRE(fim::app::StubBankWriter::WriteSineBank(f.dir()));

    for (int i = 1; i <= 8; ++i) {
        const auto path = f.dir() / (std::to_string(i) + ".wav");
        REQUIRE(std::filesystem::exists(path));
    }
}

TEST_CASE("StubBankWriter pages have the expected sample count", "[stub_bank_writer]") {
    StubBankDirFixture f("fim_stub_bank_test_count");

    REQUIRE(fim::app::StubBankWriter::WriteSineBank(f.dir()));

    // Each page is 64 waves * 2048 samples = 131072 samples.
    auto loaded = fim::engine::LoadWavMono((f.dir() / "1.wav").string());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 64 * 2048);
}

TEST_CASE("StubBankWriter pages contain non-zero samples", "[stub_bank_writer]") {
    StubBankDirFixture f("fim_stub_bank_test_nonzero");

    REQUIRE(fim::app::StubBankWriter::WriteSineBank(f.dir()));

    auto loaded = fim::engine::LoadWavMono((f.dir() / "1.wav").string());
    REQUIRE(loaded.has_value());

    bool any_nonzero = false;
    for (float s : *loaded) {
        if (std::abs(s) > 0.01f) {
            any_nonzero = true;
            break;
        }
    }
    REQUIRE(any_nonzero);
}
