cmake_minimum_required(VERSION 3.2)
PROJECT(shnet VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fcoroutines)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/await)
endif()

# Options to build demo and test
option(SHNET_BUILD_DEMO "Build the demo" OFF)
option(SHNET_BUILD_TEST "Build the test" OFF)

# ============================
# Directories
# ============================

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRC_DIR "${ROOT_DIR}/src")
set(INCLUDE_DIR "${ROOT_DIR}/include")
set(DEMO_DIR "${ROOT_DIR}/demo")
set(TEST_DIR "${ROOT_DIR}/test")
set(THIRD_PARTY_DIR "${ROOT_DIR}/3rd")
set(CONFIG_DIR "${ROOT_DIR}/config")
set(CMAKE_DIR "${ROOT_DIR}/cmake")

# ============================
# Libraries
# ============================

include(FetchContent)
FetchContent_Declare(
    shlog
    GIT_REPOSITORY https://github.com/Shane0821/shlog
    GIT_TAG b8e68f7b1606367b5a352320fad35b1817f41a9e
)
FetchContent_MakeAvailable(shlog)

FetchContent_Declare(
    shcoro
    GIT_REPOSITORY https://github.com/Shane0821/shcoro
    GIT_TAG 985645fac52979cc6ff0838c1d24d42bb9486d2a
)
FetchContent_MakeAvailable(shcoro)

# ============================
# Pakcages
# ============================


# ============================
# Files
# ============================


# ============================
# Build
# ============================

# add_subdirectory(libs)
add_subdirectory(src)

# Conditionally build demo
if (SHNET_BUILD_DEMO)
    add_subdirectory(demo)
endif()

# Conditionally build test
if (SHNET_BUILD_TEST)
    add_subdirectory(test)
endif()

# ============================
# Export
# ============================

add_library(shnet::shnet ALIAS shnet)

set(targets_export_name shnet-targets)

include(GNUInstallDirs)

# Install library and headers
install(TARGETS shnet
    EXPORT ${targets_export_name}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export targets (to build tree for development) and install tree for consumers
export(EXPORT ${targets_export_name}
    NAMESPACE shnet::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/${targets_export_name}.cmake"
)

# Generate Config and Version files
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/shnet-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_DIR}/shnet-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/shnet-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shnet
)

# Install exported targets and config files
install(EXPORT ${targets_export_name}
    NAMESPACE shnet::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shnet
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/shnet-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/shnet-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shnet
)