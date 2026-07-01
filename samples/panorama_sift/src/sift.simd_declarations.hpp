// This file is part of the PanoramaSift sample library.
// Hand-written equivalent of the file OpenCV normally generates from
// CMakeLists.txt content (see OpenCVCompilerOptimizations.cmake). It declares
// the dispatched SIFT SIMD entry points for every compiled ISA and defines
// CV_CPU_DISPATCH_MODES_ALL used by CV_CPU_DISPATCH(...) in sift.dispatch.cpp.
#define CV_CPU_SIMD_FILENAME "sift.simd.hpp"
#define CV_CPU_DISPATCH_MODE AVX512_SKX
#include "opencv2/core/private/cv_cpu_include_simd_declarations.hpp"
#define CV_CPU_DISPATCH_MODE AVX2
#include "opencv2/core/private/cv_cpu_include_simd_declarations.hpp"
#define CV_CPU_DISPATCH_MODE SSE4_1
#include "opencv2/core/private/cv_cpu_include_simd_declarations.hpp"
#define CV_CPU_DISPATCH_MODES_ALL AVX512_SKX, AVX2, SSE4_1, BASELINE
#undef CV_CPU_SIMD_FILENAME
