add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
)

target_include_directories(fim-tests PRIVATE
    ../src
)

target_link_libraries(fim-tests PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
    SampleRate::samplerate
    spdlog::spdlog
)

# Disable Qt AUTOMOC for the test target — it has no Q_OBJECTs.
set_target_properties(fim-tests PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)

include(Catch)
catch_discover_tests(fim-tests)
