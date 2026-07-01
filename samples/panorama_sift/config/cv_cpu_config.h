// This file is part of the PanoramaSift sample library.
// Hand-written stand-in for the build-generated OpenCV cv_cpu_config.h
// (template: cmake/templates/cv_cpu_config.h.in). It declares the x86-64
// baseline (SSE/SSE2/SSE3) and the dispatched features we compile per-ISA TU
// (SSE4_1, AVX2, AVX512_SKX). Adjust the baseline block to match the OpenCV
// build you link against if it differs.

// OpenCV CPU baseline features

#define CV_CPU_COMPILE_SSE 1
#define CV_CPU_BASELINE_COMPILE_SSE 1

#define CV_CPU_COMPILE_SSE2 1
#define CV_CPU_BASELINE_COMPILE_SSE2 1

#define CV_CPU_COMPILE_SSE3 1
#define CV_CPU_BASELINE_COMPILE_SSE3 1

#define CV_CPU_BASELINE_FEATURES 0 \
    , CV_CPU_SSE \
    , CV_CPU_SSE2 \
    , CV_CPU_SSE3 \


// OpenCV supported CPU dispatched features

#define CV_CPU_DISPATCH_COMPILE_SSE4_1 1
#define CV_CPU_DISPATCH_COMPILE_AVX2 1
#define CV_CPU_DISPATCH_COMPILE_AVX512_SKX 1


#define CV_CPU_DISPATCH_FEATURES 0 \
    , CV_CPU_SSE4_1 \
    , CV_CPU_AVX2 \
    , CV_CPU_AVX512_SKX \

