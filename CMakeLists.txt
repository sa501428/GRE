cmake_minimum_required(VERSION 3.24)

project(gre VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

option(GRE_BUILD_TESTS "Build GRE tests" ${BUILD_TESTING})
option(GRE_BUILD_TOOLS "Build the gre_plot command-line tool" ON)

set(GRE_STRAW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../straw/C++"
    CACHE PATH "Path to the straw C++ sources (Hi-C matrix access)")
set(GRE_IGV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../igv-cpp"
    CACHE PATH "Path to the igv-cpp sources (1D genomic data access)")

find_package(ZLIB REQUIRED)

# --------------------------------------------------------------------------
# Sibling data libraries.
#
# Both are added with EXCLUDE_FROM_ALL so only the targets GRE actually links
# get built, and with BUILD_TESTING temporarily off so their test suites do not
# attach themselves to this project's CTest run.
# --------------------------------------------------------------------------
set(GRE_OUTER_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF)

set(STRAW_BUILD_C_API OFF CACHE BOOL "" FORCE)
set(IGVCPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(IGVCPP_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

if(NOT EXISTS "${GRE_STRAW_DIR}/straw.h")
    message(FATAL_ERROR "straw sources not found at ${GRE_STRAW_DIR}; set -DGRE_STRAW_DIR=...")
endif()
if(NOT EXISTS "${GRE_IGV_DIR}/include/igv/igv.hpp")
    message(FATAL_ERROR "igv-cpp sources not found at ${GRE_IGV_DIR}; set -DGRE_IGV_DIR=...")
endif()

add_subdirectory("${GRE_STRAW_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/straw" EXCLUDE_FROM_ALL)
add_subdirectory("${GRE_IGV_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/igv-cpp" EXCLUDE_FROM_ALL)

set(BUILD_TESTING ${GRE_OUTER_BUILD_TESTING})

# --------------------------------------------------------------------------
# The library
# --------------------------------------------------------------------------
add_library(gre
    src/core/color.cpp
    src/core/scale.cpp
    src/render/truetype.cpp
    src/render/text.cpp
    src/render/rasterizer.cpp
    src/render/image.cpp
    src/render/canvas.cpp
    src/render/raster_canvas.cpp
    src/render/rotated_canvas.cpp
    src/render/pdf_canvas.cpp
    src/render/svg_canvas.cpp
    src/figure/theme.cpp
    src/figure/track.cpp
    src/figure/panel.cpp
    src/figure/figure.cpp
    src/tracks/axis_track.cpp
    src/tracks/signal_track.cpp
    src/tracks/heatmap_track.cpp
    src/tracks/gene_track.cpp
    src/tracks/interval_track.cpp
    src/tracks/legend_track.cpp
    src/data/memory_sources.cpp
    src/data/straw_matrix_source.cpp
    src/data/igv_sources.cpp
    src/export/png.cpp
    src/export/pdf_writer.cpp
)
add_library(gre::gre ALIAS gre)

target_compile_features(gre PUBLIC cxx_std_20)
target_include_directories(gre
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(gre
    PUBLIC
        igv::igv
    PRIVATE
        straw_core
        ZLIB::ZLIB
)
target_compile_options(gre PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)

set_target_properties(gre PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME gre
    POSITION_INDEPENDENT_CODE ON
)

if(GRE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(GRE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# GRE links straw and igv-cpp from their source trees, and neither is installed
# by this project, so an installed libgre would not be self-contained.  The
# supported integration is add_subdirectory(); install() is provided only for
# staging the artefacts.
install(TARGETS gre)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
