## KDevelop Plugin
set(test_cargo_SRCS
    test_cargo.cpp

    ../cargoplugin.cpp
    ../cargobuildjob.cpp
    ../cargoexecutionconfig.cpp
    ../cargofindtestsjob.cpp
    ${cargo_LOG_SRCS}
)

include_directories(
    ..
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

configure_file("paths.h.cmake" "cargo-test-paths.h" ESCAPE_QUOTES)

ki18n_wrap_ui(test_cargo_SRCS ../cargoexecutionconfig.ui)

ecm_add_test(
    ${test_cargo_SRCS}

    TEST_NAME test_cargo
    LINK_LIBRARIES Qt5::Test KDev::Tests
)
