// Compatibility shim: provides the functional universal-intrinsics API
// (VTraits, v_add/v_sub/v_mul, v_ge/v_le/v_gt/v_lt, v_and/v_or) introduced in
// OpenCV 4.7+ on top of the operator-based API available in OpenCV < 4.7.
// This lets sift.simd.hpp (written against the newer API) build against 4.5.5.
#ifndef PANORAMA_SIFT_INTRIN_COMPAT_HPP
#define PANORAMA_SIFT_INTRIN_COMPAT_HPP

#include "opencv2/core/version.hpp"

#if (CV_VERSION_MAJOR * 1000 + CV_VERSION_MINOR) < 4007

namespace cv {

template<class R> struct VTraits {
    typedef typename R::lane_type lane_type;
    enum { max_nlanes = R::nlanes };
    static inline int vlanes() { return R::nlanes; }
};

template<class R> inline R v_add(const R& a, const R& b) { return a + b; }
template<class R> inline R v_sub(const R& a, const R& b) { return a - b; }
template<class R> inline R v_mul(const R& a, const R& b) { return a * b; }

template<class R> inline R v_ge(const R& a, const R& b) { return a >= b; }
template<class R> inline R v_le(const R& a, const R& b) { return a <= b; }
template<class R> inline R v_gt(const R& a, const R& b) { return a > b; }
template<class R> inline R v_lt(const R& a, const R& b) { return a < b; }

template<class R> inline R v_and(const R& a, const R& b) { return a & b; }
template<class R> inline R v_or(const R& a, const R& b) { return a | b; }

} // namespace cv

#endif // OpenCV < 4.7

#endif // PANORAMA_SIFT_INTRIN_COMPAT_HPP
