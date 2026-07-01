// This file is part of the PanoramaSift sample library.
// Minimal stand-in for the build-generated OpenCV cvconfig.h. It is required
// only because opencv2/core/private.hpp does `#include "cvconfig.h"`. Every
// OpenCV configuration macro is consumed via `#ifdef`/`#if defined`, so an empty
// configuration is safe here: features simply appear disabled to our sources.
// If you need a specific OpenCV feature toggle, copy the matching define from
// the cvconfig.h of the OpenCV build you link against.
#ifndef PANORAMA_SIFT_CVCONFIG_H
#define PANORAMA_SIFT_CVCONFIG_H

/* Define to build with the same C++ standard as the OpenCV headers expect. */
/* Intentionally left without HAVE_* toggles. */

#endif // PANORAMA_SIFT_CVCONFIG_H
