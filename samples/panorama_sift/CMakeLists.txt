# PanoramaSift: a standalone shared library that re-exports OpenCV's SIFT and
# PanoramaMatcher inside namespace `panoram`.
#
# This is a fully external library: it is always configured on its own and links
# an OpenCV found via find_package(OpenCV). It does NOT need to be built inside
# the OpenCV source tree. Point it at an OpenCV install or build tree with
# -DOpenCV_DIR=<path containing OpenCVConfig.cmake>.
#
# The library vendors the few generated/private headers OpenCV does not always
# install (see config/ and vendor/); those directories are placed first on the
# include path so they win over the installed copies.
cmake_minimum_required(VERSION 3.16)
project(panorama_sift LANGUAGES CXX)

# opencv2/features2d.hpp uses C++17 inline static data members, so C++17 is required.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PANORAMA_SIFT_ENABLE_AVX512 "Compile the AVX512_SKX SIFT dispatch TU" ON)
option(PANORAMA_SIFT_BUILD_EXAMPLE "Build the small usage example" ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d imgcodecs highgui)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(PANORAMA_SIFT_SOURCES
    src/sift.dispatch.cpp
    src/sift.sse4_1.cpp
    src/sift.avx2.cpp
    src/panorama_matcher.cpp
)
if(PANORAMA_SIFT_ENABLE_AVX512)
    list(APPEND PANORAMA_SIFT_SOURCES src/sift.avx512_skx.cpp)
endif()

add_library(panorama_sift SHARED ${PANORAMA_SIFT_SOURCES})

# ---------------------------------------------------------------------------
# Include directories. Order matters: our config/ and vendor/ copies of the
# generated/private OpenCV headers must be found before the installed OpenCV
# headers so the CPU dispatch machinery resolves cvconfig.h / cv_cpu_config.h
# to our copies. BEFORE prepends them on the target.
# ---------------------------------------------------------------------------
target_include_directories(panorama_sift BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(panorama_sift
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ---------------------------------------------------------------------------
# Common definitions.
#  - __OPENCV_BUILD enables the private CPU dispatch headers (cv_cpu_dispatch.h).
#  - PANORAMA_SIFT_BUILD switches export.hpp to dllexport.
#  - CV_ENABLE_INTRINSICS turns on the SIMD code paths.
# ---------------------------------------------------------------------------
target_compile_definitions(panorama_sift
    PRIVATE
        __OPENCV_BUILD
        PANORAMA_SIFT_BUILD
        CV_ENABLE_INTRINSICS
)

# Force our minimal cvconfig.h before any OpenCV header can pull in the
# build-tree cvconfig.h (HAVE_IPP -> ippicv.h, which we do not ship).
if(MSVC)
    target_compile_options(panorama_sift PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/config/cvconfig.h"
    )
else()
    target_compile_options(panorama_sift PRIVATE
        "-include" "${CMAKE_CURRENT_SOURCE_DIR}/config/cvconfig.h"
    )
endif()

target_link_libraries(panorama_sift PRIVATE ${OpenCV_LIBS})

# ---------------------------------------------------------------------------
# Per-source CPU dispatch mode + compiler flags.
# Definitions follow OpenCV's CPU_<OPT>_IMPLIES chains; flags follow
# OpenCVCompilerOptimizations.cmake. The baseline (SSE/SSE2/SSE3) comes from
# config/cv_cpu_config.h and applies to every TU.
# ---------------------------------------------------------------------------
if(MSVC)
    set(_flags_sse4_1  "")
    set(_flags_avx2    "/arch:AVX2")
    set(_flags_avx512  "/arch:AVX512")
else()
    set(_flags_sse4_1  "-msse4.1")
    set(_flags_avx2    "-mavx2 -mfma -mf16c")
    set(_flags_avx512  "-mavx512f -mavx512cd -mavx512bw -mavx512dq -mavx512vl -mfma -mf16c")
endif()

set(_defs_sse4_1
    CV_CPU_DISPATCH_MODE=SSE4_1
    CV_CPU_COMPILE_SSSE3=1
    CV_CPU_COMPILE_SSE4_1=1
)
set(_defs_avx2
    CV_CPU_DISPATCH_MODE=AVX2
    CV_CPU_COMPILE_SSSE3=1
    CV_CPU_COMPILE_SSE4_1=1
    CV_CPU_COMPILE_SSE4_2=1
    CV_CPU_COMPILE_POPCNT=1
    CV_CPU_COMPILE_FP16=1
    CV_CPU_COMPILE_AVX=1
    CV_CPU_COMPILE_FMA3=1
    CV_CPU_COMPILE_AVX2=1
)
set(_defs_avx512
    CV_CPU_DISPATCH_MODE=AVX512_SKX
    CV_CPU_COMPILE_SSSE3=1
    CV_CPU_COMPILE_SSE4_1=1
    CV_CPU_COMPILE_SSE4_2=1
    CV_CPU_COMPILE_POPCNT=1
    CV_CPU_COMPILE_FP16=1
    CV_CPU_COMPILE_AVX=1
    CV_CPU_COMPILE_FMA3=1
    CV_CPU_COMPILE_AVX2=1
    CV_CPU_COMPILE_AVX_512F=1
    CV_CPU_COMPILE_AVX512_COMMON=1
    CV_CPU_COMPILE_AVX512_SKX=1
)

# Per-source dispatch flags must stay on sift.{sse4_1,avx2,avx512_skx}.cpp only.
# Re-applied at the end of configure (DEFER) so a parent add_subdirectory() cannot
# accidentally replace them with a global target_compile_definitions(panorama_sift ...).
function(_panorama_sift_apply_cpu_dispatch_flags)
    set_source_files_properties(src/sift.sse4_1.cpp PROPERTIES
        COMPILE_DEFINITIONS "${_defs_sse4_1}"
        COMPILE_FLAGS "${_flags_sse4_1}"
    )
    set_source_files_properties(src/sift.avx2.cpp PROPERTIES
        COMPILE_DEFINITIONS "${_defs_avx2}"
        COMPILE_FLAGS "${_flags_avx2}"
    )
    if(PANORAMA_SIFT_ENABLE_AVX512)
        set_source_files_properties(src/sift.avx512_skx.cpp PROPERTIES
            COMPILE_DEFINITIONS "${_defs_avx512}"
            COMPILE_FLAGS "${_flags_avx512}"
        )
    endif()
endfunction()
_panorama_sift_apply_cpu_dispatch_flags()
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
    cmake_language(DEFER CALL _panorama_sift_apply_cpu_dispatch_flags)
endif()

# ---------------------------------------------------------------------------
# Example
# ---------------------------------------------------------------------------
if(PANORAMA_SIFT_BUILD_EXAMPLE)
    add_executable(panorama_sift_example example/main.cpp)
    target_link_libraries(panorama_sift_example PRIVATE panorama_sift ${OpenCV_LIBS})
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS panorama_sift
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
