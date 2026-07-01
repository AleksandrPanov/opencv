// This file is part of the PanoramaSift sample library.
// Public interface for panoram::PanoramaMatcher (a self-contained copy of
// cv::PanoramaMatcher living in namespace panoram).
#ifndef PANORAMA_SIFT_PANORAMA_MATCHER_HPP
#define PANORAMA_SIFT_PANORAMA_MATCHER_HPP

#include "panorama_sift/export.hpp"

#include <opencv2/core.hpp>
#include <vector>

namespace panoram {

using namespace cv;

class PANORAMA_SIFT_API PanoramaMatcher
{
    cv::Mat m_prevDescriptors;
    std::vector<KeyPoint> m_prevKeypoints;

    // Результат сравнения положения prev относительно окна по y вокруг next.
    enum class CompatY { PrevBelow = -1, Ok = 0, PrevAbove = 1 };

    CompatY compatibleDistance(const KeyPoint& next, const KeyPoint& prev, int prevX);
    bool compatiblePoints(const KeyPoint& next, const KeyPoint& prev, int prevX);
public:
    // init block
    Mat m_backMask;
    Mat m_carMask;
    Size2i m_frameSize;
    bool m_isTracked;

    // match params
    float m_siftDist;
    float m_loweCoef;


    int m_maxShiftY;
    float m_backgroundTileSize;
    int m_maxBackgroundFeatures;
    float m_backgroundThr;
    float m_backgroundDist;
    float m_maxShiftDiff;
    float m_minShiftDiff;
    float m_shiftDiff;
    float m_sizeDiff; // acceptable feature size difference: min(size1, size2) > m_sizeDiff*max(size1, size2)

    int m_badMatches;
    int m_backMatches;
    int m_duplicateMatches;

    PanoramaMatcher()
    {
        m_isTracked = false;

        m_siftDist = 0.18f;
        m_loweCoef = 0.8f;

        m_maxShiftY = 5;
        m_backgroundTileSize = 20.f;
        m_maxBackgroundFeatures = 4;
        m_backgroundThr = 0.15f;
        m_backgroundDist = 1.f;
        m_maxShiftDiff = 2.f;
        m_minShiftDiff = .5f;
        m_shiftDiff = 2.f;
        m_sizeDiff = 0.8f;

        m_badMatches = 0;
        m_backMatches = 0;
        m_duplicateMatches = 0;
    }
    void init(Size2i _frameSize);
    std::vector<DMatch> custom_match(InputArray nextDescriptors, const std::vector<KeyPoint>& keypoints, int prevX, InputArray mask=noArray());
};

} // namespace panoram

#endif // PANORAMA_SIFT_PANORAMA_MATCHER_HPP
