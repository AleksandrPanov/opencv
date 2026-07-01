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

option(PANORAMA_SIFT_BUILD_EXAMPLE "Build the small usage example" ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d imgcodecs highgui)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(PANORAMA_SIFT_SOURCES
    src/sift.dispatch.cpp
    src/panorama_matcher.cpp
)

set(PANORAMA_SIFT_SRC_HEADERS
    src/precomp.hpp
    src/intrin_compat.hpp
    src/sift.simd.hpp
)
set(PANORAMA_SIFT_PUBLIC_HEADERS
    include/panorama_sift/export.hpp
    include/panorama_sift/panorama_matcher.hpp
    include/panorama_sift/panorama_sift.hpp
)
set(PANORAMA_SIFT_HEADERS ${PANORAMA_SIFT_SRC_HEADERS} ${PANORAMA_SIFT_PUBLIC_HEADERS})

add_library(panorama_sift SHARED ${PANORAMA_SIFT_SOURCES} ${PANORAMA_SIFT_HEADERS})

# Show all implementation and public headers under the "src" node in VS/IDE.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX src
    FILES ${PANORAMA_SIFT_SOURCES} ${PANORAMA_SIFT_SRC_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include/panorama_sift PREFIX src
    FILES ${PANORAMA_SIFT_PUBLIC_HEADERS})

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
#  - __OPENCV_BUILD enables the private CPU headers (cv_cpu_dispatch.h) that pull
#    in the SIMD intrinsics based on config/cv_cpu_config.h.
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
# CPU features. Runtime dispatch has been removed: the target machine is assumed
# to support AVX2. AVX2 is the compile-time baseline (see config/cv_cpu_config.h),
# so the whole target is built with AVX2 code generation. There are NO per-source
# dispatch flags and no CV_CPU_DISPATCH_MODE.
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(panorama_sift PRIVATE "/arch:AVX2")
else()
    target_compile_options(panorama_sift PRIVATE -mavx2 -mfma -mf16c)
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
