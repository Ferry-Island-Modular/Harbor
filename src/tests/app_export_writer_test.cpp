#include <QString>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/services/export_writer.h"
#include "dr_wav.h"

namespace {

// Write a 2048-cycle bank to `dir` containing `num_pages` files named
// 1.wav .. N.wav, each of which is 64 cycles of 2048 samples (constant
// value 0.5 for verification).
void WriteFakeBank(const std::filesystem::path& dir, int num_pages = 8) {
    std::filesystem::create_directories(dir);
    constexpr std::size_t kCycleSamples = 2048;
    constexpr std::size_t kCellsPerPage = 64;
    constexpr std::uint32_t kSampleRate = 44100;

    for (int p = 1; p <= num_pages; ++p) {
        const auto path = dir / (std::to_string(p) + ".wav");
        drwav_data_format format = {};
        format.container = drwav_container_riff;
        format.format = DR_WAVE_FORMAT_PCM;
        format.channels = 1;
        format.sampleRate = kSampleRate;
        format.bitsPerSample = 16;
        drwav wav;
        REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
        std::vector<std::int16_t> samples(kCycleSamples * kCellsPerPage,
                                          static_cast<std::int16_t>(0.5f * 32767.0f));
        drwav_write_pcm_frames(&wav, samples.size(), samples.data());
        drwav_uninit(&wav);
    }
}

void WriteMarker(const std::filesystem::path& path, const std::string& value) {
    std::ofstream stream(path);
    REQUIRE(stream.good());
    stream << value;
}

std::string ReadMarker(const std::filesystem::path& path) {
    std::ifstream stream(path);
    REQUIRE(stream.good());
    std::string value;
    stream >> value;
    return value;
}

drwav_uint64 GetFrameCount(const std::filesystem::path& path) {
    drwav wav;
    REQUIRE(drwav_init_file(&wav, path.string().c_str(), nullptr));
    const drwav_uint64 frames = wav.totalPCMFrameCount;
    drwav_uninit(&wav);
    return frames;
}

}  // namespace

TEST_CASE("WriteBankToExportDir copies 2048-sample bank unchanged when target is 2048",
          "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::filesystem::path src_dir = std::filesystem::path(tmp.path().toStdString()) / "src";
    const std::filesystem::path dest_dir = std::filesystem::path(tmp.path().toStdString()) / "dest";
    WriteFakeBank(src_dir);

    const bool ok = fim::app::WriteBankToExportDir(QString::fromStdString(src_dir.string()),
                                                   QString::fromStdString(dest_dir.string()),
                                                   /*target_samples_per_cycle=*/2048);
    REQUIRE(ok);

    for (int p = 1; p <= 8; ++p) {
        const auto path = dest_dir / (std::to_string(p) + ".wav");
        REQUIRE(std::filesystem::exists(path));
        REQUIRE(GetFrameCount(path) == 2048u * 64u);
    }

    // Re-exporting to the same directory must overwrite rather than silently
    // leaving the old bank in place.
    REQUIRE(fim::app::WriteBankToExportDir(QString::fromStdString(src_dir.string()),
                                           QString::fromStdString(dest_dir.string()), 2048));
}

TEST_CASE("WriteBankToExportDir downsamples to 256 sample cycles when target is 256",
          "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::filesystem::path src_dir = std::filesystem::path(tmp.path().toStdString()) / "src";
    const std::filesystem::path dest_dir = std::filesystem::path(tmp.path().toStdString()) / "dest";
    WriteFakeBank(src_dir);

    const bool ok = fim::app::WriteBankToExportDir(QString::fromStdString(src_dir.string()),
                                                   QString::fromStdString(dest_dir.string()),
                                                   /*target_samples_per_cycle=*/256);
    REQUIRE(ok);

    for (int p = 1; p <= 8; ++p) {
        const auto path = dest_dir / (std::to_string(p) + ".wav");
        REQUIRE(std::filesystem::exists(path));
        // 64 cells * 256 samples = 16384.
        REQUIRE(GetFrameCount(path) == 256u * 64u);
    }
}

TEST_CASE("WriteBankToExportDir returns false when source dir is missing", "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString missing = tmp.filePath("does_not_exist");
    const QString dest = tmp.filePath("dest");
    const bool ok = fim::app::WriteBankToExportDir(missing, dest, 2048);
    REQUIRE_FALSE(ok);
}

TEST_CASE("PublishStagedBank replaces an existing bank directory atomically",
          "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    const auto staging = root / "cache.staging";
    const auto destination = root / "cache";
    std::filesystem::create_directories(staging);
    std::filesystem::create_directories(destination);
    WriteMarker(staging / "marker", "new");
    WriteMarker(destination / "marker", "old");

    REQUIRE(fim::app::PublishStagedBank(staging, destination));
    REQUIRE_FALSE(std::filesystem::exists(staging));
    REQUIRE(ReadMarker(destination / "marker") == "new");
}

TEST_CASE("PublishStagedBank restores the previous bank when publication fails",
          "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    const auto missing_staging = root / "missing";
    const auto destination = root / "cache";
    std::filesystem::create_directories(destination);
    WriteMarker(destination / "marker", "old");

    REQUIRE_FALSE(fim::app::PublishStagedBank(missing_staging, destination));
    REQUIRE(ReadMarker(destination / "marker") == "old");
}
