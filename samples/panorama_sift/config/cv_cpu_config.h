// This file is part of the PanoramaSift sample library.
// Hand-written stand-in for the build-generated OpenCV cv_cpu_config.h
// (template: cmake/templates/cv_cpu_config.h.in).
//
// The runtime CPU dispatch mechanism has been removed: the target machine is
// assumed to support AVX2. AVX2 (and the whole implied SSE..FMA3 chain) is
// therefore declared as the compile-time BASELINE, and there are no dispatched
// features. The whole library must be compiled with /arch:AVX2 (MSVC) or
// -mavx2 -mfma -mf16c (GCC/Clang); see CMakeLists.txt.

// OpenCV CPU baseline features (AVX2 and everything it implies)

#define CV_CPU_COMPILE_SSE 1
#define CV_CPU_BASELINE_COMPILE_SSE 1

#define CV_CPU_COMPILE_SSE2 1
#define CV_CPU_BASELINE_COMPILE_SSE2 1

#define CV_CPU_COMPILE_SSE3 1
#define CV_CPU_BASELINE_COMPILE_SSE3 1

#define CV_CPU_COMPILE_SSSE3 1
#define CV_CPU_BASELINE_COMPILE_SSSE3 1

#define CV_CPU_COMPILE_SSE4_1 1
#define CV_CPU_BASELINE_COMPILE_SSE4_1 1

#define CV_CPU_COMPILE_SSE4_2 1
#define CV_CPU_BASELINE_COMPILE_SSE4_2 1

#define CV_CPU_COMPILE_POPCNT 1
#define CV_CPU_BASELINE_COMPILE_POPCNT 1

#define CV_CPU_COMPILE_AVX 1
#define CV_CPU_BASELINE_COMPILE_AVX 1

#define CV_CPU_COMPILE_FP16 1
#define CV_CPU_BASELINE_COMPILE_FP16 1

#define CV_CPU_COMPILE_FMA3 1
#define CV_CPU_BASELINE_COMPILE_FMA3 1

#define CV_CPU_COMPILE_AVX2 1
#define CV_CPU_BASELINE_COMPILE_AVX2 1

#define CV_CPU_BASELINE_FEATURES 0 \
    , CV_CPU_SSE \
    , CV_CPU_SSE2 \
    , CV_CPU_SSE3 \
    , CV_CPU_SSSE3 \
    , CV_CPU_SSE4_1 \
    , CV_CPU_SSE4_2 \
    , CV_CPU_POPCNT \
    , CV_CPU_AVX \
    , CV_CPU_FP16 \
    , CV_CPU_FMA3 \
    , CV_CPU_AVX2 \


// No dispatched features: runtime CPU dispatch is disabled.

#define CV_CPU_DISPATCH_FEATURES 0 \

