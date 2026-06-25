#include <iostream>
#include "opencv2/core.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/features2d.hpp"
//#include "opencv2/xfeatures2d.hpp"

using namespace cv;
//using namespace cv::xfeatures2d;
using std::cout;
using std::endl;

const char* keys =
    "{ help h |                  | Print help message. }"
    "{ input1 | box.png          | Path to input image 1. }"
    "{ input2 | box_in_scene.png | Path to input image 2. }";

int main( int argc, char* argv[] )
{
    CommandLineParser parser( argc, argv, keys );
    std::string path = "C:/Users/a-panov/PycharmProjects/data_fg_bg/test/11-09-2025-19_12_05_323/";
    Mat img1 = imread(path + "125.jpg", IMREAD_GRAYSCALE);
    Mat img2 = imread(path + "126.jpg", IMREAD_GRAYSCALE);
    Mat img3 = imread(path + "127.jpg", IMREAD_GRAYSCALE);
    if ( img1.empty() || img2.empty() )
    {
        cout << "Could not open or find the image!\n" << endl;
        parser.printMessage();
        return -1;
    }

    //-- Step 1: Detect the keypoints using SURF Detector, compute the descriptors
    Ptr<SIFT> detector = SIFT::create();
    std::vector<KeyPoint> keypoints1, keypoints2, keypoints3;
    Mat descriptors1, descriptors2, descriptors3;
    detector->detectAndCompute( img1, noArray(), keypoints1, descriptors1 );
    detector->detectAndCompute( img2, noArray(), keypoints2, descriptors2 );
    detector->detectAndCompute( img3, noArray(), keypoints3, descriptors3 );

    //-- Step 2: Matching descriptor vectors with a brute force matcher
    // Since SURF is a floating-point descriptor NORM_L2 is used
    std::vector< DMatch > matches;
    //Ptr<BFMatcher> matcher = BFMatcher::create();

    //matcher->match( descriptors1, descriptors2, matches );
    //matcher->match( descriptors2, descriptors1, matches );

    cv::PanoramaMatcher panoramaMatcher;
    std::cout << img1.size();
    panoramaMatcher.init(img1.size());
    panoramaMatcher.m_maxBackgroundFeatures = -1;
    panoramaMatcher.custom_match(descriptors1, keypoints1, 0);
    auto matches1 = panoramaMatcher.custom_match(descriptors2, keypoints2, 0);
    auto matches2 = panoramaMatcher.custom_match(descriptors3, keypoints3, 24);

    //-- Draw matches
    Mat img_matches;
    drawMatches(img1, keypoints1, img2, keypoints2, matches1, img_matches );
     //-- Show detected matches
    imshow("Matches", img_matches);
    waitKey(0);
    drawMatches(img2, keypoints2, img3, keypoints3, matches2, img_matches );
    imshow("Matches", img_matches);
    waitKey(0);
    return 0;
}