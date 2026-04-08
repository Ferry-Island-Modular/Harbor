add_executable(fim-tests
    smoke_test.cpp
)

target_link_libraries(fim-tests PRIVATE
    Catch2::Catch2WithMain
    fourseas_engine
    pffft
    SampleRate::samplerate
    spdlog::spdlog
)

# Disable Qt AUTOMOC for the test target — it has no Q_OBJECTs.
set_target_properties(fim-tests PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)

include(Catch)
catch_discover_tests(fim-tests)
