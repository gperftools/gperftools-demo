cmake_minimum_required(VERSION 3.20)

project(gperftools-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---

# 1. Vendored CPM.cmake
include(cmake/CPM.cmake)

# 2. Threads support (required by gperftools)
find_package(Threads REQUIRED)

# 3. Abseil (fetched via CPM)
CPMAddPackage(
    NAME absl
    GITHUB_REPOSITORY abseil/abseil-cpp
    GIT_TAG 20260107.0
    OPTIONS
        "ABSL_PROPAGATE_CXX_STD ON"
)

# 4. gperftools (Autotools build)
set(GPERFTOOLS_VERSION 2.18)
set(GPERFTOOLS_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gperftools_ext")
set(GPERFTOOLS_SOURCE_DIR "${GPERFTOOLS_PREFIX}/src/gperftools_ext")
set(GPERFTOOLS_BINARY_DIR "${GPERFTOOLS_PREFIX}/src/gperftools_ext-build")

include(ExternalProject)

# NOTE(alk): so while it is nicely compact and works and I kinda like
# it, please keep in mind that it is by no means perfect or "blessed"
# way. Hopefully with time we can offer cleaner alternative.
ExternalProject_Add(gperftools_ext
    PREFIX "${GPERFTOOLS_PREFIX}"
    URL https://github.com/gperftools/gperftools/releases/download/gperftools-${GPERFTOOLS_VERSION}/gperftools-${GPERFTOOLS_VERSION}.tar.gz
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --enable-static --disable-shared --disable-libunwind
    BUILD_COMMAND make -j8 libtcmalloc.la libprofiler.la
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${GPERFTOOLS_BINARY_DIR}/.libs/libtcmalloc.a" "${GPERFTOOLS_BINARY_DIR}/.libs/libprofiler.a"
)

# Ensure the source directory exists at configuration time to satisfy existence
# checks for the IMPORTED target's INTERFACE_INCLUDE_DIRECTORIES.
file(MAKE_DIRECTORY "${GPERFTOOLS_SOURCE_DIR}/src")

# Define imported targets for gperftools components
# These point to the static libraries that will be built by ExternalProject
add_library(gperftools::tcmalloc STATIC IMPORTED GLOBAL)
set_target_properties(gperftools::tcmalloc PROPERTIES
    IMPORTED_LOCATION "${GPERFTOOLS_BINARY_DIR}/.libs/libtcmalloc.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GPERFTOOLS_SOURCE_DIR}/src"
)
add_dependencies(gperftools::tcmalloc gperftools_ext)

add_library(gperftools::profiler STATIC IMPORTED GLOBAL)
set_target_properties(gperftools::profiler PROPERTIES
    IMPORTED_LOCATION "${GPERFTOOLS_BINARY_DIR}/.libs/libprofiler.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GPERFTOOLS_SOURCE_DIR}/src"
)
add_dependencies(gperftools::profiler gperftools_ext)

# --- Targets ---
include(targets.cmake)
