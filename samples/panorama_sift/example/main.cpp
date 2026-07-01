// This file is part of the PanoramaSift sample library.
// Minimal usage example: detect SIFT features on two frames and match them with
// PanoramaMatcher. Build with -DPANORAMA_SIFT_BUILD_EXAMPLE=ON.
#include "panorama_sift/panorama_sift.hpp"
#include "panorama_sift/panorama_matcher.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
    //if (argc < 3)
    //{
    //    std::cerr << "Usage: " << argv[0] << " <frame1> <frame2>\n";
    //    return 1;
    //}
    //cv::Mat img1 = cv::imread(argv[1], cv::IMREAD_GRAYSCALE);
    //cv::Mat img2 = cv::imread(argv[2], cv::IMREAD_GRAYSCALE);
    //if (img1.empty() || img2.empty())
    //{
    //    std::cerr << "Failed to load input images\n";
    //    return 1;
    //}

    std::string path = "C:/Users/a-panov/PycharmProjects/data_fg_bg/test/11-09-2025-19_12_05_323/";
    cv::Mat img1 = cv::imread(path + "125.jpg", cv::IMREAD_GRAYSCALE);
    cv::Mat img2 = cv::imread(path + "126.jpg", cv::IMREAD_GRAYSCALE);
    cv::Mat img3 = cv::imread(path + "127.jpg", cv::IMREAD_GRAYSCALE);
    if ( img1.empty() || img2.empty() )
    {
        std::cout << "Could not open or find the image!\n" << std::endl;
        return -1;
    }

    cv::Ptr<panoram::SIFT> sift = panoram::SIFT::create();

    std::vector<cv::KeyPoint> kp1, kp2, kp3;
    cv::Mat desc1, desc2, desc3;
    sift->detectAndCompute(img1, cv::noArray(), kp1, desc1);
    sift->detectAndCompute(img2, cv::noArray(), kp2, desc2);

    panoram::PanoramaMatcher matcher;
    matcher.init(img1.size());

    // First call primes the previous frame, second call produces matches.
    matcher.custom_match(desc1, kp1, /*prevX=*/0);
    std::vector<cv::DMatch> matches = matcher.custom_match(desc2, kp2, /*prevX=*/0);
    auto matches2 = matcher.custom_match(desc3, kp3, 24);
    cv::Mat img_matches, img_matches2;
    cv::drawMatches(img1, kp1, img2, kp2, matches, img_matches);
    cv::drawMatches(img2, kp2, img3, kp3, matches2, img_matches2);
    cv::imshow("panoram_Matches", img_matches);
    cv::imshow("panoram_Matches2", img_matches2);
    cv::waitKey(0);


    std::cout << "keypoints: " << kp1.size() << " / " << kp2.size() << "\n";
    std::cout << "good matches: " << matches.size()
              << " (bad=" << matcher.m_badMatches
              << ", background=" << matcher.m_backMatches
              << ", duplicate=" << matcher.m_duplicateMatches << ")\n";
    return 0;
}
