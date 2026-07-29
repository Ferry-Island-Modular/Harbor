#include "app/settings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

namespace {

// Catch2 test fixture: constructed fresh before each TEST_CASE_METHOD that
// uses it, destroyed after. Forces QSettings to use a temporary INI file
// rather than the developer's real settings, and wipes the in-memory store
// between tests.
//
// QSettings keeps a process-wide cache shared across instances; deleting the
// underlying file alone does NOT reset state between tests, hence the
// explicit clear() calls.
class SettingsFixture {
public:
    SettingsFixture() {
        path_ = std::filesystem::temp_directory_path() / "fim_settings_test.ini";
        std::filesystem::remove(path_);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QString::fromStdString(path_.parent_path().string()));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName("FIM Test");
        QCoreApplication::setApplicationName("fim_settings_test");
        QSettings().clear();
        QSettings().sync();
    }

    ~SettingsFixture() {
        QSettings().clear();
        QSettings().sync();
        std::filesystem::remove(path_);
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE_METHOD(SettingsFixture, "Settings round-trips output_dir", "[settings]") {
    fim::app::Settings settings;
    settings.SetOutputDir("/tmp/fim_output");
    REQUIRE(settings.OutputDir() == "/tmp/fim_output");
}

TEST_CASE_METHOD(SettingsFixture, "Settings round-trips samples_per_frame", "[settings]") {
    fim::app::Settings settings;
    settings.SetSamplesPerFrame(2048);
    REQUIRE(settings.SamplesPerFrame() == 2048);
    settings.SetSamplesPerFrame(256);
    REQUIRE(settings.SamplesPerFrame() == 256);
}

TEST_CASE_METHOD(SettingsFixture, "Settings has sensible defaults", "[settings]") {
    fim::app::Settings settings;
    REQUIRE(settings.SamplesPerFrame() == 2048);
    REQUIRE(settings.PreviewVolume() == 60);
    REQUIRE_FALSE(settings.OutputDir().empty());  // defaults to "output_waves"
}

TEST_CASE_METHOD(SettingsFixture, "Settings persists across instances", "[settings]") {
    {
        fim::app::Settings a;
        a.SetSamplesPerFrame(256);
        a.SetPreviewVolume(75);
    }
    {
        fim::app::Settings b;
        REQUIRE(b.SamplesPerFrame() == 256);
        REQUIRE(b.PreviewVolume() == 75);
    }
}

TEST_CASE_METHOD(SettingsFixture, "Settings persists any-wav source treatment", "[settings]") {
    fim::app::Settings settings;
    REQUIRE(settings.AnyWavSourceMode() == 0);

    settings.SetAnyWavSourceMode(1);
    REQUIRE(settings.AnyWavSourceMode() == 1);
}

TEST_CASE_METHOD(SettingsFixture, "Settings round-trips serum_y_morph", "[settings]") {
    fim::app::Settings settings;
    settings.SetSerumYMorph(2);
    REQUIRE(settings.SerumYMorph() == 2);
    settings.SetSerumYMorph(0);
    REQUIRE(settings.SerumYMorph() == 0);
}

TEST_CASE_METHOD(SettingsFixture, "Settings round-trips serum_z_morph", "[settings]") {
    fim::app::Settings settings;
    settings.SetSerumZMorph(3);
    REQUIRE(settings.SerumZMorph() == 3);
    settings.SetSerumZMorph(1);
    REQUIRE(settings.SerumZMorph() == 1);

    settings.SetThreeWavZMorph(3);
    REQUIRE(settings.ThreeWavZMorph() == 3);
}

TEST_CASE_METHOD(SettingsFixture, "Settings serum morph defaults are 0 and 1", "[settings]") {
    // Defaults match Python's SerumWavetableConverter:
    //   y_morph_type = MorphType.FORMANT_SCALE  (index 0)
    //   z_morph_type = MorphType.PHASE_DISPERSE (index 1)
    fim::app::Settings settings;
    REQUIRE(settings.SerumYMorph() == 0);
    REQUIRE(settings.SerumZMorph() == 1);
}
