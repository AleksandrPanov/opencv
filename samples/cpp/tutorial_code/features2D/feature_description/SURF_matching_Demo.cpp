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

    cv::Ptr<SIFT> sift = SIFT::create();

    std::vector<cv::KeyPoint> kp1, kp2, kp3;
    cv::Mat desc1, desc2, desc3;
    sift->detectAndCompute(img1, cv::noArray(), kp1, desc1);
    sift->detectAndCompute(img2, cv::noArray(), kp2, desc2);

    PanoramaMatcher matcher;
    matcher.init(img1.size());

    // First call primes the previous frame, second call produces matches.
    matcher.custom_match(desc1, kp1, /*prevX=*/0);
    auto matches = matcher.custom_match(desc2, kp2, /*prevX=*/0);
    auto matches2 = matcher.custom_match(desc3, kp3, 24);
    cv::Mat img_matches, img_matches2;
    cv::drawMatches(img1, kp1, img2, kp2, matches, img_matches);
    cv::drawMatches(img2, kp2, img3, kp3, matches2, img_matches2);
    cv::imshow("panoram_Matches", img_matches);
    cv::imshow("panoram_Matches2", img_matches2);
    cv::waitKey(0);
    return 0;
}