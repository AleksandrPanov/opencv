// This file is part of the PanoramaSift sample library.
// Minimal usage example: detect SIFT features on two frames and match them with
// PanoramaMatcher. Build with -DPANORAMA_SIFT_BUILD_EXAMPLE=ON.
#include "panorama_sift/panorama_sift.hpp"
#include "panorama_sift/panorama_matcher.hpp"

#include <opencv2/imgcodecs.hpp>

#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <frame1> <frame2>\n";
        return 1;
    }

    cv::Mat img1 = cv::imread(argv[1], cv::IMREAD_GRAYSCALE);
    cv::Mat img2 = cv::imread(argv[2], cv::IMREAD_GRAYSCALE);
    if (img1.empty() || img2.empty())
    {
        std::cerr << "Failed to load input images\n";
        return 1;
    }

    cv::Ptr<panoram::SIFT> sift = panoram::SIFT::create();

    std::vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desc1, desc2;
    sift->detectAndCompute(img1, cv::noArray(), kp1, desc1);
    sift->detectAndCompute(img2, cv::noArray(), kp2, desc2);

    panoram::PanoramaMatcher matcher;
    matcher.init(img1.size());

    // First call primes the previous frame, second call produces matches.
    matcher.custom_match(desc1, kp1, /*prevX=*/0);
    std::vector<cv::DMatch> matches = matcher.custom_match(desc2, kp2, /*prevX=*/0);

    std::cout << "keypoints: " << kp1.size() << " / " << kp2.size() << "\n";
    std::cout << "good matches: " << matches.size()
              << " (bad=" << matcher.m_badMatches
              << ", background=" << matcher.m_backMatches
              << ", duplicate=" << matcher.m_duplicateMatches << ")\n";
    return 0;
}
