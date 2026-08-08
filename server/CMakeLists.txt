cmake_minimum_required(VERSION 3.22)

# vcpkg toolchain — must be set before project()
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain")
endif()

project(git4cad-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ubuntu apt packages are built without a cmake build-type config (suffix "None").
# Map our build types so cmake resolves IMPORTED_LOCATION for all apt-installed targets.
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE    "None;Release;RelWithDebInfo;")
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG      "None;Debug;RelWithDebInfo;")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "None;MinSizeRel;RelWithDebInfo;")

# ─── OpenCASCADE ─────────────────────────────────────────────────────────────
# Supports two install paths:
#   A) vcpkg: provides OpenCASCADEConfig.cmake via the toolchain
#   B) Ubuntu apt (libocct-*-dev): provides per-module target files but no
#      top-level config file. We include them manually and set the variables.

find_package(OpenCASCADE CONFIG QUIET)

if(NOT OpenCASCADE_FOUND)
    set(_OCCT_CMAKE_DIR "/usr/lib/cmake/opencascade")
    if(IS_DIRECTORY "${_OCCT_CMAKE_DIR}")
        message(STATUS "OpenCASCADE: using system installation at ${_OCCT_CMAKE_DIR}")
        # Include order matters: Visualization provides TKService/TKV3d
        # that AppFramework and DataExchange depend on
        # Must include base + relwithdebinfo files so IMPORTED_LOCATION is set;
        # otherwise cmake falls back to -lTKxxx which fails if the path isn't
        # in the default linker search. Order: Visualization before AppFW and DataExchange.
        foreach(_tf
            OpenCASCADEFoundationClassesTargets
            OpenCASCADEModelingDataTargets
            OpenCASCADEModelingAlgorithmsTargets
            OpenCASCADEVisualizationTargets
            OpenCASCADEApplicationFrameworkTargets
            OpenCASCADEDataExchangeTargets)
            include("${_OCCT_CMAKE_DIR}/${_tf}.cmake" OPTIONAL)
            include("${_OCCT_CMAKE_DIR}/${_tf}-relwithdebinfo.cmake" OPTIONAL)
        endforeach()

        set(OpenCASCADE_INCLUDE_DIR "/usr/include/opencascade")

        # Targets needed: kernel, BRep geometry, STEP reader, XDE/XCAF, GLTF writer
        set(OpenCASCADE_LIBRARIES
            TKernel TKMath
            TKBRep TKG3d TKG2d TKGeomBase
            TKTopAlgo TKGeomAlgo TKBool TKBO TKMesh
            TKSTEP TKSTEP209 TKSTEPAttr TKSTEPBase TKXSBase TKXDESTEP
            TKCAF TKLCAF TKCDF TKXCAF TKRWMesh
            TKBinXCAF TKXmlXCAF
        )
        set(OpenCASCADE_FOUND TRUE)
    else()
        message(FATAL_ERROR
            "OpenCASCADE not found. Either set VCPKG_ROOT or install libocct-*-dev.")
    endif()
else()
    message(STATUS "OpenCASCADE: found via vcpkg/config")
endif()

# ─── Other dependencies ───────────────────────────────────────────────────────

# libgit2 v1.7.x does not install cmake config files — use pkg-config fallback
find_package(libgit2 CONFIG QUIET)
if(NOT libgit2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBGIT2 REQUIRED libgit2)
    add_library(libgit2::libgit2 INTERFACE IMPORTED)
    set_target_properties(libgit2::libgit2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LIBGIT2_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES     "${LIBGIT2_LINK_LIBRARIES}"
        INTERFACE_COMPILE_OPTIONS    "${LIBGIT2_CFLAGS_OTHER}"
    )
endif()
find_package(Crow     CONFIG REQUIRED)
find_package(libzip   CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog   CONFIG REQUIRED)
find_package(pugixml  CONFIG REQUIRED)
find_package(OpenSSL         REQUIRED)
find_package(CURL            REQUIRED)
find_package(SQLiteCpp       CONFIG REQUIRED)

# ─── Main executable ─────────────────────────────────────────────────────────

file(GLOB_RECURSE SERVER_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER SERVER_SOURCES EXCLUDE REGEX ".*/tests/.*\\.cpp$")

add_executable(git4cad-server ${SERVER_SOURCES}
        src/main.cpp)

target_include_directories(git4cad-server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${OpenCASCADE_INCLUDE_DIR}"
)

target_link_libraries(git4cad-server PRIVATE
    ${OpenCASCADE_LIBRARIES}
    libgit2::libgit2
    Crow::Crow
    libzip::zip
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    pugixml::pugixml
    OpenSSL::Crypto
    CURL::libcurl
    SQLiteCpp
)

target_compile_options(git4cad-server PRIVATE
    -Wall -Wextra -Wpedantic
)

# ─── Optional tests ──────────────────────────────────────────────────────────

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    find_package(Catch2 3 CONFIG QUIET)
    if(Catch2_FOUND)
        file(GLOB_RECURSE TEST_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        )
        add_executable(git4cad-tests ${TEST_SOURCES})
        target_include_directories(git4cad-tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${OpenCASCADE_INCLUDE_DIR}"
        )
        target_link_libraries(git4cad-tests PRIVATE
            ${OpenCASCADE_LIBRARIES}
            libgit2::libgit2
            Crow::Crow
            libzip::zip
            nlohmann_json::nlohmann_json
            spdlog::spdlog
            pugixml::pugixml
            OpenSSL::Crypto
            CURL::libcurl
            SQLiteCpp
            Catch2::Catch2WithMain
        )
        include(CTest)
        include(Catch)
        catch_discover_tests(git4cad-tests)
    endif()
endif()
