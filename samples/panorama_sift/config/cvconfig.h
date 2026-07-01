// This file is part of the PanoramaSift sample library.
// Stand-in for the build-generated OpenCV cvconfig.h.
//
// Must use the same include guard as OpenCV's cvconfig.h
// (OPENCV_CVCONFIG_H_INCLUDED). Our copy is placed first on the include path
// and force-included so the OpenCV build-tree cvconfig.h (which may define
// HAVE_IPP and pull in ippicv.h) is never seen when compiling PanoramaSift.
//
// OpenCV private headers gate optional features with #ifdef HAVE_*; leaving them
// undefined is safe — those code paths are simply not compiled into our TUs.
#ifndef OPENCV_CVCONFIG_H_INCLUDED
#define OPENCV_CVCONFIG_H_INCLUDED

/* Intentionally no HAVE_* toggles. */

#endif // OPENCV_CVCONFIG_H_INCLUDED
