// This file is part of the PanoramaSift sample library.
// Local replacement for OpenCV features2d/src/precomp.hpp. It pulls in only the
// pieces of OpenCV that the SIFT dispatch/SIMD and PanoramaMatcher sources need.
//
// __OPENCV_BUILD is defined via the build system (see CMakeLists.txt); it makes
// the OpenCV headers expose the private CPU dispatch machinery
// (cv_cpu_dispatch.h -> cv_cpu_config.h + cv_cpu_helper.h).
#ifndef PANORAMA_SIFT_PRECOMP_HPP
#define PANORAMA_SIFT_PRECOMP_HPP

#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/features2d.hpp"

#include "opencv2/core/utility.hpp"
#include "opencv2/core/hal/hal.hpp"
#include "opencv2/core/utils/tls.hpp"
#include "opencv2/core/utils/logger.hpp"

#include <algorithm>
#include <vector>

#endif // PANORAMA_SIFT_PRECOMP_HPP
