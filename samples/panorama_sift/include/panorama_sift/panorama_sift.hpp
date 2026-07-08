// This file is part of the PanoramaSift sample library.
// Public interface for panoram::SIFT (a self-contained copy of cv::SIFT
// living in namespace panoram, exported from the PanoramaSift shared library).
#ifndef PANORAMA_SIFT_PANORAMA_SIFT_HPP
#define PANORAMA_SIFT_PANORAMA_SIFT_HPP

#include "panorama_sift/export.hpp"

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

namespace panoram {

using namespace cv;

/** @brief Class implementing the SIFT (Scale Invariant Feature Transform) algorithm.

This is a standalone re-export of OpenCV's SIFT implementation inside the
`panoram` namespace so it can evolve independently of the OpenCV build. The API
mirrors cv::SIFT.
*/
class PANORAMA_SIFT_API SIFT : public cv::Feature2D
{
public:
    /** @brief Create SIFT with the default 32F descriptor type.
    @param nfeatures The number of best features to retain.
    @param nOctaveLayers The number of layers in each octave.
    @param contrastThreshold The contrast threshold used to filter out weak features.
    @param edgeThreshold The threshold used to filter out edge-like features.
    @param sigma The sigma of the Gaussian applied to the input image at octave #0.
    @param enable_precise_upscale Whether to enable precise upscaling in the scale pyramid.
    */
    static Ptr<SIFT> create(int nfeatures = 0, int nOctaveLayers = 3,
        double contrastThreshold = 0.04, double edgeThreshold = 10,
        double sigma = 1.6, bool enable_precise_upscale = false);

    /** @brief Create SIFT with the specified descriptorType.
    @param nfeatures The number of best features to retain.
    @param nOctaveLayers The number of layers in each octave.
    @param contrastThreshold The contrast threshold used to filter out weak features.
    @param edgeThreshold The threshold used to filter out edge-like features.
    @param sigma The sigma of the Gaussian applied to the input image at octave #0.
    @param descriptorType The type of descriptors. Only CV_32F and CV_8U are supported.
    @param enable_precise_upscale Whether to enable precise upscaling in the scale pyramid.
    */
    static Ptr<SIFT> create(int nfeatures, int nOctaveLayers,
        double contrastThreshold, double edgeThreshold,
        double sigma, int descriptorType, bool enable_precise_upscale = false);

    virtual String getDefaultName() const CV_OVERRIDE;

    virtual void setNFeatures(int maxFeatures) = 0;
    virtual int getNFeatures() const = 0;

    virtual void setNOctaveLayers(int nOctaveLayers) = 0;
    virtual int getNOctaveLayers() const = 0;

    virtual void setContrastThreshold(double contrastThreshold) = 0;
    virtual double getContrastThreshold() const = 0;

    virtual void setEdgeThreshold(double edgeThreshold) = 0;
    virtual double getEdgeThreshold() const = 0;

    virtual void setSigma(double sigma) = 0;
    virtual double getSigma() const = 0;

    // Смещение, прибавляемое к координатам ключевых точек в detectAndCompute.
    void setOffset(Point2f offset) { m_offset = offset; }
    Point2f getOffset() const { return m_offset; }

protected:
    Point2f m_offset = Point2f(0.f, 0.f);
};

typedef SIFT SiftFeatureDetector;
typedef SIFT SiftDescriptorExtractor;

} // namespace panoram

#endif // PANORAMA_SIFT_PANORAMA_SIFT_HPP
