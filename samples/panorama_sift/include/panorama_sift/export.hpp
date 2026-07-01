// This file is part of the PanoramaSift sample library.
// Provides the PANORAMA_SIFT_API import/export macro.
#ifndef PANORAMA_SIFT_EXPORT_HPP
#define PANORAMA_SIFT_EXPORT_HPP

#if defined(_WIN32)
#  ifdef PANORAMA_SIFT_BUILD
#    define PANORAMA_SIFT_API __declspec(dllexport)
#  else
#    define PANORAMA_SIFT_API __declspec(dllimport)
#  endif
#else
#  define PANORAMA_SIFT_API __attribute__((visibility("default")))
#endif

#endif // PANORAMA_SIFT_EXPORT_HPP
