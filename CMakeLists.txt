cmake_minimum_required(VERSION 3.0)

project(kdevcargo)

find_package(ECM 5.15.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)
include(ECMInstallIcons)
include(FeatureSummary)

find_package(KDevPlatform 5.0 REQUIRED)
find_package(KF5 5.15.0 REQUIRED COMPONENTS
    I18n
    ItemModels # needed because missing in KDevPlatformConfig.cmake, remove once dep on kdevplatform >=5.2.2
)
find_package(Cargo QUIET)
set_package_properties(Cargo PROPERTIES
    TYPE RUNTIME
)

if (NOT TARGET KDev::Test)
    set(BUILD_TESTING OFF)
endif ()

if (BUILD_TESTING)
    include(ECMAddTests)
    find_package(Qt5Test 5.5.0 CONFIG REQUIRED)
endif (BUILD_TESTING)

add_subdirectory(src)
add_subdirectory(icons)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
