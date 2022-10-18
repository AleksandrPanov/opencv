// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Copyright (C) 2018, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.

#include "precomp.hpp"
#include "opencv2/objdetect.hpp"
#include "opencv2/calib3d.hpp"

#ifdef HAVE_QUIRC
#include "quirc.h"
#endif

#include <limits>
#include <cmath>
#include <iostream>
#include <queue>
#include <limits>
#include <map>
#include <opencv2/highgui.hpp>
#include <iostream>

namespace cv
{
using std::vector;

static bool checkQRInputImage(InputArray img, Mat& gray)
{
    CV_Assert(!img.empty());
    CV_CheckDepthEQ(img.depth(), CV_8U, "");

    if (img.cols() <= 20 || img.rows() <= 20)
    {
        return false;  // image data is not enough for providing reliable results
    }
    int incn = img.channels();
    CV_Check(incn, incn == 1 || incn == 3 || incn == 4, "");
    if (incn == 3 || incn == 4)
    {
        cvtColor(img, gray, COLOR_BGR2GRAY);
    }
    else
    {
        gray = img.getMat();
    }
    return true;
}

static void updatePointsResult(OutputArray points_, const vector<Point2f>& points)
{
    if (points_.needed())
    {
        int N = int(points.size() / 4);
        if (N > 0)
        {
            Mat m_p(N, 4, CV_32FC2, (void*)&points[0]);
            int points_type = points_.fixedType() ? points_.type() : CV_32FC2;
            m_p.reshape(2, points_.rows()).convertTo(points_, points_type);  // Mat layout: N x 4 x 2cn
        }
        else
        {
            points_.release();
        }
    }
}

static Point2f intersectionLines(Point2f a1, Point2f a2, Point2f b1, Point2f b2)
{
    const float divisor = (a1.x - a2.x) * (b1.y - b2.y) - (a1.y - a2.y) * (b1.x - b2.x);
    const float eps = 0.001f;
    if (abs(divisor) < eps)
        return a2;
    Point2f result_square_angle(
                              ((a1.x * a2.y  -  a1.y * a2.x) * (b1.x - b2.x) -
                               (b1.x * b2.y  -  b1.y * b2.x) * (a1.x - a2.x)) /
                               divisor,
                              ((a1.x * a2.y  -  a1.y * a2.x) * (b1.y - b2.y) -
                               (b1.x * b2.y  -  b1.y * b2.x) * (a1.y - a2.y)) /
                               divisor
                              );
    return result_square_angle;
}

//      / | b
//     /  |
//    /   |
//  a/    | c

static inline double getCosVectors(Point2f a, Point2f b, Point2f c)
{
    return ((a - b).x * (c - b).x + (a - b).y * (c - b).y) / (norm(a - b) * norm(c - b));
}

static bool arePointsNearest(Point2f a, Point2f b, float delta = 0.0)
{
    if ((abs(a.x - b.x) < delta) && (abs(a.y - b.y) < delta))
        return true;
    else
        return false;
}

class QRDetect
{
public:
    void init(const Mat& src, double eps_vertical_ = 0.2, double eps_horizontal_ = 0.1);
    bool localization();
    bool computeTransformationPoints();
    Mat getBinBarcode() { return bin_barcode; }
    Mat getStraightBarcode() { return straight_barcode; }
    vector<Point2f> getTransformationPoints() { return transformation_points; }
protected:
    vector<Vec3d> searchHorizontalLines();
    vector<Point2f> separateVerticalLines(const vector<Vec3d> &list_lines);
    vector<Point2f> extractVerticalLines(const vector<Vec3d> &list_lines, double eps);
    void fixationPoints(vector<Point2f> &local_point);
    vector<Point2f> getQuadrilateral(vector<Point2f> angle_list);
    bool testByPassRoute(vector<Point2f> hull, int start, int finish);

    Mat barcode, bin_barcode, resized_barcode, resized_bin_barcode, straight_barcode;
    vector<Point2f> localization_points, transformation_points;
    double eps_vertical, eps_horizontal, coeff_expansion;
    enum resize_direction { ZOOMING, SHRINKING, UNCHANGED } purpose;
};


void QRDetect::init(const Mat& src, double eps_vertical_, double eps_horizontal_)
{
    CV_TRACE_FUNCTION();
    CV_Assert(!src.empty());
    barcode = src.clone();
    const double min_side = std::min(src.size().width, src.size().height);
    if (min_side < 512.0)
    {
        purpose = ZOOMING;
        coeff_expansion = 512.0 / min_side;
        const int width  = cvRound(src.size().width  * coeff_expansion);
        const int height = cvRound(src.size().height  * coeff_expansion);
        Size new_size(width, height);
        resize(src, barcode, new_size, 0, 0, INTER_LINEAR_EXACT);
    }
    else if (min_side > 512.0)
    {
        purpose = SHRINKING;
        coeff_expansion = min_side / 512.0;
        const int width  = cvRound(src.size().width  / coeff_expansion);
        const int height = cvRound(src.size().height  / coeff_expansion);
        Size new_size(width, height);
        resize(src, resized_barcode, new_size, 0, 0, INTER_AREA);
    }
    else
    {
        purpose = UNCHANGED;
        coeff_expansion = 1.0;
    }

    eps_vertical   = eps_vertical_;
    eps_horizontal = eps_horizontal_;

    if (!barcode.empty())
        adaptiveThreshold(barcode, bin_barcode, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 83, 2);
    else
        bin_barcode.release();

    if (!resized_barcode.empty())
        adaptiveThreshold(resized_barcode, resized_bin_barcode, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 83, 2);
    else
        resized_bin_barcode.release();
}

vector<Vec3d> QRDetect::searchHorizontalLines()
{
    CV_TRACE_FUNCTION();
    vector<Vec3d> result;
    const int height_bin_barcode = bin_barcode.rows;
    const int width_bin_barcode  = bin_barcode.cols;
    const size_t test_lines_size = 5;
    double test_lines[test_lines_size];
    vector<size_t> pixels_position;

    for (int y = 0; y < height_bin_barcode; y++)
    {
        pixels_position.clear();
        const uint8_t *bin_barcode_row = bin_barcode.ptr<uint8_t>(y);

        int pos = 0;
        for (; pos < width_bin_barcode; pos++) { if (bin_barcode_row[pos] == 0) break; }
        if (pos == width_bin_barcode) { continue; }

        pixels_position.push_back(pos);
        pixels_position.push_back(pos);
        pixels_position.push_back(pos);

        uint8_t future_pixel = 255;
        for (int x = pos; x < width_bin_barcode; x++)
        {
            if (bin_barcode_row[x] == future_pixel)
            {
                future_pixel = static_cast<uint8_t>(~future_pixel);
                pixels_position.push_back(x);
            }
        }
        pixels_position.push_back(width_bin_barcode - 1);
        for (size_t i = 2; i < pixels_position.size() - 3; i+=2)
        {
            test_lines[0] = static_cast<double>(pixels_position[i - 1] - pixels_position[i - 2]);
            test_lines[1] = static_cast<double>(pixels_position[i    ] - pixels_position[i - 1]);
            test_lines[2] = static_cast<double>(pixels_position[i + 1] - pixels_position[i    ]);
            test_lines[3] = static_cast<double>(pixels_position[i + 2] - pixels_position[i + 1]);
            test_lines[4] = static_cast<double>(pixels_position[i + 3] - pixels_position[i + 2]);

            double length = 0.0, weight = 0.0;  // TODO avoid 'double' calculations

            for (size_t j = 0; j < test_lines_size; j++) { length += test_lines[j]; }

            if (length == 0) { continue; }
            for (size_t j = 0; j < test_lines_size; j++)
            {
                if (j != 2) { weight += fabs((test_lines[j] / length) - 1.0/7.0); }
                else        { weight += fabs((test_lines[j] / length) - 3.0/7.0); }
            }

            if (weight < eps_vertical)
            {
                Vec3d line;
                line[0] = static_cast<double>(pixels_position[i - 2]);
                line[1] = y;
                line[2] = length;
                result.push_back(line);
            }
        }
    }
    return result;
}

vector<Point2f> QRDetect::separateVerticalLines(const vector<Vec3d> &list_lines)
{
    CV_TRACE_FUNCTION();
    const double min_dist_between_points = 10.0;
    const double max_ratio = 1.0;
    for (int coeff_epsilon_i = 1; coeff_epsilon_i < 101; ++coeff_epsilon_i)
    {
        const float coeff_epsilon = coeff_epsilon_i * 0.1f;
        vector<Point2f> point2f_result = extractVerticalLines(list_lines, eps_horizontal * coeff_epsilon);
        if (!point2f_result.empty())
        {
            vector<Point2f> centers;
            Mat labels;
            double compactness = kmeans(
                    point2f_result, 3, labels,
                    TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 10, 0.1),
                    3, KMEANS_PP_CENTERS, centers);
            double min_dist = std::numeric_limits<double>::max();
            for (size_t i = 0; i < centers.size(); i++)
            {
                double dist = norm(centers[i] - centers[(i+1) % centers.size()]);
                if (dist < min_dist)
                {
                    min_dist = dist;
                }
            }
            if (min_dist < min_dist_between_points)
            {
                continue;
            }
            double mean_compactness = compactness / point2f_result.size();
            double ratio = mean_compactness / min_dist;

            if (ratio < max_ratio)
            {
                return point2f_result;
            }
        }
    }
    return vector<Point2f>();  // nothing
}

vector<Point2f> QRDetect::extractVerticalLines(const vector<Vec3d> &list_lines, double eps)
{
    CV_TRACE_FUNCTION();
    vector<Vec3d> result;
    vector<double> test_lines; test_lines.reserve(6);

    for (size_t pnt = 0; pnt < list_lines.size(); pnt++)
    {
        const int x = cvRound(list_lines[pnt][0] + list_lines[pnt][2] * 0.5);
        const int y = cvRound(list_lines[pnt][1]);

        // --------------- Search vertical up-lines --------------- //

        test_lines.clear();
        uint8_t future_pixel_up = 255;

        int temp_length_up = 0;
        for (int j = y; j < bin_barcode.rows - 1; j++)
        {
            uint8_t next_pixel = bin_barcode.ptr<uint8_t>(j + 1)[x];
            temp_length_up++;
            if (next_pixel == future_pixel_up)
            {
                future_pixel_up = static_cast<uint8_t>(~future_pixel_up);
                test_lines.push_back(temp_length_up);
                temp_length_up = 0;
                if (test_lines.size() == 3)
                    break;
            }
        }

        // --------------- Search vertical down-lines --------------- //

        int temp_length_down = 0;
        uint8_t future_pixel_down = 255;
        for (int j = y; j >= 1; j--)
        {
            uint8_t next_pixel = bin_barcode.ptr<uint8_t>(j - 1)[x];
            temp_length_down++;
            if (next_pixel == future_pixel_down)
            {
                future_pixel_down = static_cast<uint8_t>(~future_pixel_down);
                test_lines.push_back(temp_length_down);
                temp_length_down = 0;
                if (test_lines.size() == 6)
                    break;
            }
        }

        // --------------- Compute vertical lines --------------- //

        if (test_lines.size() == 6)
        {
            double length = 0.0, weight = 0.0;  // TODO avoid 'double' calculations

            for (size_t i = 0; i < test_lines.size(); i++)
                length += test_lines[i];

            CV_Assert(length > 0);
            for (size_t i = 0; i < test_lines.size(); i++)
            {
                if (i % 3 != 0)
                {
                    weight += fabs((test_lines[i] / length) - 1.0/ 7.0);
                }
                else
                {
                    weight += fabs((test_lines[i] / length) - 3.0/14.0);
                }
            }

            if (weight < eps)
            {
                result.push_back(list_lines[pnt]);
            }
        }
    }

    vector<Point2f> point2f_result;
    if (result.size() > 2)
    {
        for (size_t i = 0; i < result.size(); i++)
        {
            point2f_result.push_back(
                  Point2f(static_cast<float>(result[i][0] + result[i][2] * 0.5),
                          static_cast<float>(result[i][1])));
        }
    }
    return point2f_result;
}

void QRDetect::fixationPoints(vector<Point2f> &local_point)
{
    CV_TRACE_FUNCTION();
    double cos_angles[3], norm_triangl[3];

    norm_triangl[0] = norm(local_point[1] - local_point[2]);
    norm_triangl[1] = norm(local_point[0] - local_point[2]);
    norm_triangl[2] = norm(local_point[1] - local_point[0]);

    cos_angles[0] = (norm_triangl[1] * norm_triangl[1] + norm_triangl[2] * norm_triangl[2]
                  -  norm_triangl[0] * norm_triangl[0]) / (2 * norm_triangl[1] * norm_triangl[2]);
    cos_angles[1] = (norm_triangl[0] * norm_triangl[0] + norm_triangl[2] * norm_triangl[2]
                  -  norm_triangl[1] * norm_triangl[1]) / (2 * norm_triangl[0] * norm_triangl[2]);
    cos_angles[2] = (norm_triangl[0] * norm_triangl[0] + norm_triangl[1] * norm_triangl[1]
                  -  norm_triangl[2] * norm_triangl[2]) / (2 * norm_triangl[0] * norm_triangl[1]);

    const double angle_barrier = 0.85;
    if (fabs(cos_angles[0]) > angle_barrier || fabs(cos_angles[1]) > angle_barrier || fabs(cos_angles[2]) > angle_barrier)
    {
        local_point.clear();
        return;
    }

    size_t i_min_cos =
       (cos_angles[0] < cos_angles[1] && cos_angles[0] < cos_angles[2]) ? 0 :
       (cos_angles[1] < cos_angles[0] && cos_angles[1] < cos_angles[2]) ? 1 : 2;

    size_t index_max = 0;
    double max_area = std::numeric_limits<double>::min();
    for (size_t i = 0; i < local_point.size(); i++)
    {
        const size_t current_index = i % 3;
        const size_t left_index  = (i + 1) % 3;
        const size_t right_index = (i + 2) % 3;

        const Point2f current_point(local_point[current_index]),
            left_point(local_point[left_index]), right_point(local_point[right_index]),
            central_point(intersectionLines(current_point,
                              Point2f(static_cast<float>((local_point[left_index].x + local_point[right_index].x) * 0.5),
                                      static_cast<float>((local_point[left_index].y + local_point[right_index].y) * 0.5)),
                              Point2f(0, static_cast<float>(bin_barcode.rows - 1)),
                              Point2f(static_cast<float>(bin_barcode.cols - 1),
                                      static_cast<float>(bin_barcode.rows - 1))));

        vector<Point2f> list_area_pnt;
        list_area_pnt.push_back(current_point);

        vector<LineIterator> list_line_iter;
        list_line_iter.push_back(LineIterator(bin_barcode, current_point, left_point));
        list_line_iter.push_back(LineIterator(bin_barcode, current_point, central_point));
        list_line_iter.push_back(LineIterator(bin_barcode, current_point, right_point));

        for (size_t k = 0; k < list_line_iter.size(); k++)
        {
            LineIterator& li = list_line_iter[k];
            uint8_t future_pixel = 255, count_index = 0;
            for(int j = 0; j < li.count; j++, ++li)
            {
                const Point p = li.pos();
                if (p.x >= bin_barcode.cols ||
                    p.y >= bin_barcode.rows)
                {
                    break;
                }

                const uint8_t value = bin_barcode.at<uint8_t>(p);
                if (value == future_pixel)
                {
                    future_pixel = static_cast<uint8_t>(~future_pixel);
                    count_index++;
                    if (count_index == 3)
                    {
                        list_area_pnt.push_back(p);
                        break;
                    }
                }
            }
        }

        const double temp_check_area = contourArea(list_area_pnt);
        if (temp_check_area > max_area)
        {
            index_max = current_index;
            max_area = temp_check_area;
        }

    }
    if (index_max == i_min_cos) { std::swap(local_point[0], local_point[index_max]); }
    else { local_point.clear(); return; }

    const Point2f rpt = local_point[0], bpt = local_point[1], gpt = local_point[2];
    Matx22f m(rpt.x - bpt.x, rpt.y - bpt.y, gpt.x - rpt.x, gpt.y - rpt.y);
    if( determinant(m) > 0 )
    {
        std::swap(local_point[1], local_point[2]);
    }
}

bool QRDetect::localization()
{
    CV_TRACE_FUNCTION();
    Point2f begin, end;
    vector<Vec3d> list_lines_x = searchHorizontalLines();
    if( list_lines_x.empty() ) { return false; }
    vector<Point2f> list_lines_y = separateVerticalLines(list_lines_x);
    if( list_lines_y.empty() ) { return false; }

    Mat labels;
    kmeans(list_lines_y, 3, labels,
           TermCriteria( TermCriteria::EPS + TermCriteria::COUNT, 10, 0.1),
           3, KMEANS_PP_CENTERS, localization_points);

    fixationPoints(localization_points);

    bool square_flag = false, local_points_flag = false;
    double triangle_sides[3];
    double triangle_perim, square_area, img_square_area;
    if (localization_points.size() == 3)
    {
        triangle_sides[0] = norm(localization_points[0] - localization_points[1]);
        triangle_sides[1] = norm(localization_points[1] - localization_points[2]);
        triangle_sides[2] = norm(localization_points[2] - localization_points[0]);

        triangle_perim = (triangle_sides[0] + triangle_sides[1] + triangle_sides[2]) / 2;

        square_area = sqrt((triangle_perim * (triangle_perim - triangle_sides[0])
                                           * (triangle_perim - triangle_sides[1])
                                           * (triangle_perim - triangle_sides[2]))) * 2;
        img_square_area = bin_barcode.cols * bin_barcode.rows;

        if (square_area > (img_square_area * 0.2))
        {
            square_flag = true;
        }
    }
    else
    {
        local_points_flag = true;
    }
    if ((square_flag || local_points_flag) && purpose == SHRINKING)
    {
        localization_points.clear();
        bin_barcode = resized_bin_barcode.clone();
        list_lines_x = searchHorizontalLines();
        if( list_lines_x.empty() ) { return false; }
        list_lines_y = separateVerticalLines(list_lines_x);
        if( list_lines_y.empty() ) { return false; }

        kmeans(list_lines_y, 3, labels,
               TermCriteria( TermCriteria::EPS + TermCriteria::COUNT, 10, 0.1),
               3, KMEANS_PP_CENTERS, localization_points);

        fixationPoints(localization_points);
        if (localization_points.size() != 3) { return false; }

        const int width  = cvRound(bin_barcode.size().width  * coeff_expansion);
        const int height = cvRound(bin_barcode.size().height * coeff_expansion);
        Size new_size(width, height);
        Mat intermediate;
        resize(bin_barcode, intermediate, new_size, 0, 0, INTER_LINEAR_EXACT);
        bin_barcode = intermediate.clone();
        for (size_t i = 0; i < localization_points.size(); i++)
        {
            localization_points[i] *= coeff_expansion;
        }
    }
    if (purpose == ZOOMING)
    {
        const int width  = cvRound(bin_barcode.size().width  / coeff_expansion);
        const int height = cvRound(bin_barcode.size().height / coeff_expansion);
        Size new_size(width, height);
        Mat intermediate;
        resize(bin_barcode, intermediate, new_size, 0, 0, INTER_LINEAR_EXACT);
        bin_barcode = intermediate.clone();
        for (size_t i = 0; i < localization_points.size(); i++)
        {
            localization_points[i] /= coeff_expansion;
        }
    }

    for (size_t i = 0; i < localization_points.size(); i++)
    {
        for (size_t j = i + 1; j < localization_points.size(); j++)
        {
            if (norm(localization_points[i] - localization_points[j]) < 10)
            {
                return false;
            }
        }
    }
    return true;

}

bool QRDetect::computeTransformationPoints()
{
    CV_TRACE_FUNCTION();
    if (localization_points.size() != 3) { return false; }

    vector<Point> locations, non_zero_elem[3], newHull;
    vector<Point2f> new_non_zero_elem[3];
    for (size_t i = 0; i < 3; i++)
    {
        Mat mask = Mat::zeros(bin_barcode.rows + 2, bin_barcode.cols + 2, CV_8UC1);
        uint8_t next_pixel, future_pixel = 255;
        int count_test_lines = 0, index = cvRound(localization_points[i].x);
        for (; index < bin_barcode.cols - 1; index++)
        {
            next_pixel = bin_barcode.ptr<uint8_t>(cvRound(localization_points[i].y))[index + 1];
            if (next_pixel == future_pixel)
            {
                future_pixel = static_cast<uint8_t>(~future_pixel);
                count_test_lines++;
                if (count_test_lines == 2)
                {
                    floodFill(bin_barcode, mask,
                              Point(index + 1, cvRound(localization_points[i].y)), 255,
                              0, Scalar(), Scalar(), FLOODFILL_MASK_ONLY);
                    break;
                }
            }
        }
        Mat mask_roi = mask(Range(1, bin_barcode.rows - 1), Range(1, bin_barcode.cols - 1));
        findNonZero(mask_roi, non_zero_elem[i]);
        newHull.insert(newHull.end(), non_zero_elem[i].begin(), non_zero_elem[i].end());
    }
    convexHull(newHull, locations);
    for (size_t i = 0; i < locations.size(); i++)
    {
        for (size_t j = 0; j < 3; j++)
        {
            for (size_t k = 0; k < non_zero_elem[j].size(); k++)
            {
                if (locations[i] == non_zero_elem[j][k])
                {
                    new_non_zero_elem[j].push_back(locations[i]);
                }
            }
        }
    }

    double pentagon_diag_norm = -1;
    Point2f down_left_edge_point, up_right_edge_point, up_left_edge_point;
    for (size_t i = 0; i < new_non_zero_elem[1].size(); i++)
    {
        for (size_t j = 0; j < new_non_zero_elem[2].size(); j++)
        {
            double temp_norm = norm(new_non_zero_elem[1][i] - new_non_zero_elem[2][j]);
            if (temp_norm > pentagon_diag_norm)
            {
                down_left_edge_point = new_non_zero_elem[1][i];
                up_right_edge_point  = new_non_zero_elem[2][j];
                pentagon_diag_norm = temp_norm;
            }
        }
    }

    if (down_left_edge_point == Point2f(0, 0) ||
        up_right_edge_point  == Point2f(0, 0) ||
        new_non_zero_elem[0].size() == 0) { return false; }

    double max_area = -1;
    up_left_edge_point = new_non_zero_elem[0][0];

    for (size_t i = 0; i < new_non_zero_elem[0].size(); i++)
    {
        vector<Point2f> list_edge_points;
        list_edge_points.push_back(new_non_zero_elem[0][i]);
        list_edge_points.push_back(down_left_edge_point);
        list_edge_points.push_back(up_right_edge_point);

        double temp_area = fabs(contourArea(list_edge_points));
        if (max_area < temp_area)
        {
            up_left_edge_point = new_non_zero_elem[0][i];
            max_area = temp_area;
        }
    }

    Point2f down_max_delta_point, up_max_delta_point;
    double norm_down_max_delta = -1, norm_up_max_delta = -1;
    for (size_t i = 0; i < new_non_zero_elem[1].size(); i++)
    {
        double temp_norm_delta = norm(up_left_edge_point - new_non_zero_elem[1][i])
                               + norm(down_left_edge_point - new_non_zero_elem[1][i]);
        if (norm_down_max_delta < temp_norm_delta)
        {
            down_max_delta_point = new_non_zero_elem[1][i];
            norm_down_max_delta = temp_norm_delta;
        }
    }


    for (size_t i = 0; i < new_non_zero_elem[2].size(); i++)
    {
        double temp_norm_delta = norm(up_left_edge_point - new_non_zero_elem[2][i])
                               + norm(up_right_edge_point - new_non_zero_elem[2][i]);
        if (norm_up_max_delta < temp_norm_delta)
        {
            up_max_delta_point = new_non_zero_elem[2][i];
            norm_up_max_delta = temp_norm_delta;
        }
    }

    transformation_points.push_back(down_left_edge_point);
    transformation_points.push_back(up_left_edge_point);
    transformation_points.push_back(up_right_edge_point);
    Point2f down_right_edge_point = intersectionLines(down_left_edge_point, down_max_delta_point,
                          up_right_edge_point, up_max_delta_point);
    transformation_points.push_back(down_right_edge_point);
    vector<Point2f> quadrilateral = getQuadrilateral(transformation_points);
    transformation_points = quadrilateral;

    int width = bin_barcode.size().width;
    int height = bin_barcode.size().height;
    for (size_t i = 0; i < transformation_points.size(); i++)
    {
        if ((cvRound(transformation_points[i].x) > width) ||
            (cvRound(transformation_points[i].y) > height)) { return false; }
    }
    return true;
}

// test function (if true then ------> else <------ )
bool QRDetect::testByPassRoute(vector<Point2f> hull, int start, int finish)
{
    CV_TRACE_FUNCTION();
    int index_hull = start, next_index_hull, hull_size = (int)hull.size();
    double test_length[2] = { 0.0, 0.0 };
    do
    {
        next_index_hull = index_hull + 1;
        if (next_index_hull == hull_size) { next_index_hull = 0; }
        test_length[0] += norm(hull[index_hull] - hull[next_index_hull]);
        index_hull = next_index_hull;
    }
    while(index_hull != finish);

    index_hull = start;
    do
    {
        next_index_hull = index_hull - 1;
        if (next_index_hull == -1) { next_index_hull = hull_size - 1; }
        test_length[1] += norm(hull[index_hull] - hull[next_index_hull]);
        index_hull = next_index_hull;
    }
    while(index_hull != finish);

    if (test_length[0] < test_length[1]) { return true; } else { return false; }
}

vector<Point2f> QRDetect::getQuadrilateral(vector<Point2f> angle_list)
{
    CV_TRACE_FUNCTION();
    size_t angle_size = angle_list.size();
    uint8_t value, mask_value;
    Mat mask = Mat::zeros(bin_barcode.rows + 2, bin_barcode.cols + 2, CV_8UC1);
    Mat fill_bin_barcode = bin_barcode.clone();
    for (size_t i = 0; i < angle_size; i++)
    {
        LineIterator line_iter(bin_barcode, angle_list[ i      % angle_size],
                                            angle_list[(i + 1) % angle_size]);
        for(int j = 0; j < line_iter.count; j++, ++line_iter)
        {
            Point p = line_iter.pos();
            value = bin_barcode.at<uint8_t>(p);
            mask_value = mask.at<uint8_t>(p + Point(1, 1));
            if (value == 0 && mask_value == 0)
            {
                floodFill(fill_bin_barcode, mask, p, 255,
                          0, Scalar(), Scalar(), FLOODFILL_MASK_ONLY);
            }
        }
    }
    vector<Point> locations;
    Mat mask_roi = mask(Range(1, bin_barcode.rows - 1), Range(1, bin_barcode.cols - 1));

    findNonZero(mask_roi, locations);

    for (size_t i = 0; i < angle_list.size(); i++)
    {
        int x = cvRound(angle_list[i].x);
        int y = cvRound(angle_list[i].y);
        locations.push_back(Point(x, y));
    }

    vector<Point> integer_hull;
    convexHull(locations, integer_hull);
    int hull_size = (int)integer_hull.size();
    vector<Point2f> hull(hull_size);
    for (int i = 0; i < hull_size; i++)
    {
        float x = saturate_cast<float>(integer_hull[i].x);
        float y = saturate_cast<float>(integer_hull[i].y);
        hull[i] = Point2f(x, y);
    }

    const double experimental_area = fabs(contourArea(hull));

    vector<Point2f> result_hull_point(angle_size);
    double min_norm;
    for (size_t i = 0; i < angle_size; i++)
    {
        min_norm = std::numeric_limits<double>::max();
        Point closest_pnt;
        for (int j = 0; j < hull_size; j++)
        {
            double temp_norm = norm(hull[j] - angle_list[i]);
            if (min_norm > temp_norm)
            {
                min_norm = temp_norm;
                closest_pnt = hull[j];
            }
        }
        result_hull_point[i] = closest_pnt;
    }

    int start_line[2] = { 0, 0 }, finish_line[2] = { 0, 0 }, unstable_pnt = 0;
    for (int i = 0; i < hull_size; i++)
    {
        if (result_hull_point[2] == hull[i]) { start_line[0] = i; }
        if (result_hull_point[1] == hull[i]) { finish_line[0] = start_line[1] = i; }
        if (result_hull_point[0] == hull[i]) { finish_line[1] = i; }
        if (result_hull_point[3] == hull[i]) { unstable_pnt = i; }
    }

    int index_hull, extra_index_hull, next_index_hull, extra_next_index_hull;
    Point result_side_begin[4], result_side_end[4];

    bool bypass_orientation = testByPassRoute(hull, start_line[0], finish_line[0]);

    min_norm = std::numeric_limits<double>::max();
    index_hull = start_line[0];
    do
    {
        if (bypass_orientation) { next_index_hull = index_hull + 1; }
        else { next_index_hull = index_hull - 1; }

        if (next_index_hull == hull_size) { next_index_hull = 0; }
        if (next_index_hull == -1) { next_index_hull = hull_size - 1; }

        Point angle_closest_pnt =  norm(hull[index_hull] - angle_list[1]) >
        norm(hull[index_hull] - angle_list[2]) ? angle_list[2] : angle_list[1];

        Point intrsc_line_hull =
        intersectionLines(hull[index_hull], hull[next_index_hull],
                          angle_list[1], angle_list[2]);
        double temp_norm = getCosVectors(hull[index_hull], intrsc_line_hull, angle_closest_pnt);
        if (min_norm > temp_norm &&
            norm(hull[index_hull] - hull[next_index_hull]) >
            norm(angle_list[1] - angle_list[2]) * 0.1)
        {
            min_norm = temp_norm;
            result_side_begin[0] = hull[index_hull];
            result_side_end[0]   = hull[next_index_hull];
        }


        index_hull = next_index_hull;
    }
    while(index_hull != finish_line[0]);

    if (min_norm == std::numeric_limits<double>::max())
    {
        result_side_begin[0] = angle_list[1];
        result_side_end[0]   = angle_list[2];
    }

    min_norm = std::numeric_limits<double>::max();
    index_hull = start_line[1];
    bypass_orientation = testByPassRoute(hull, start_line[1], finish_line[1]);
    do
    {
        if (bypass_orientation) { next_index_hull = index_hull + 1; }
        else { next_index_hull = index_hull - 1; }

        if (next_index_hull == hull_size) { next_index_hull = 0; }
        if (next_index_hull == -1) { next_index_hull = hull_size - 1; }

        Point angle_closest_pnt =  norm(hull[index_hull] - angle_list[0]) >
        norm(hull[index_hull] - angle_list[1]) ? angle_list[1] : angle_list[0];

        Point intrsc_line_hull =
        intersectionLines(hull[index_hull], hull[next_index_hull],
                          angle_list[0], angle_list[1]);
        double temp_norm = getCosVectors(hull[index_hull], intrsc_line_hull, angle_closest_pnt);
        if (min_norm > temp_norm &&
            norm(hull[index_hull] - hull[next_index_hull]) >
            norm(angle_list[0] - angle_list[1]) * 0.05)
        {
            min_norm = temp_norm;
            result_side_begin[1] = hull[index_hull];
            result_side_end[1]   = hull[next_index_hull];
        }

        index_hull = next_index_hull;
    }
    while(index_hull != finish_line[1]);

    if (min_norm == std::numeric_limits<double>::max())
    {
        result_side_begin[1] = angle_list[0];
        result_side_end[1]   = angle_list[1];
    }

    bypass_orientation = testByPassRoute(hull, start_line[0], unstable_pnt);
    const bool extra_bypass_orientation = testByPassRoute(hull, finish_line[1], unstable_pnt);

    vector<Point2f> result_angle_list(4), test_result_angle_list(4);
    double min_diff_area = std::numeric_limits<double>::max();
    index_hull = start_line[0];
    const double standart_norm = std::max(
        norm(result_side_begin[0] - result_side_end[0]),
        norm(result_side_begin[1] - result_side_end[1]));
    do
    {
        if (bypass_orientation) { next_index_hull = index_hull + 1; }
        else { next_index_hull = index_hull - 1; }

        if (next_index_hull == hull_size) { next_index_hull = 0; }
        if (next_index_hull == -1) { next_index_hull = hull_size - 1; }

        if (norm(hull[index_hull] - hull[next_index_hull]) < standart_norm * 0.1)
        { index_hull = next_index_hull; continue; }

        extra_index_hull = finish_line[1];
        do
        {
            if (extra_bypass_orientation) { extra_next_index_hull = extra_index_hull + 1; }
            else { extra_next_index_hull = extra_index_hull - 1; }

            if (extra_next_index_hull == hull_size) { extra_next_index_hull = 0; }
            if (extra_next_index_hull == -1) { extra_next_index_hull = hull_size - 1; }

            if (norm(hull[extra_index_hull] - hull[extra_next_index_hull]) < standart_norm * 0.1)
            { extra_index_hull = extra_next_index_hull; continue; }

            test_result_angle_list[0]
            = intersectionLines(result_side_begin[0], result_side_end[0],
                                result_side_begin[1], result_side_end[1]);
            test_result_angle_list[1]
            = intersectionLines(result_side_begin[1], result_side_end[1],
                                hull[extra_index_hull], hull[extra_next_index_hull]);
            test_result_angle_list[2]
            = intersectionLines(hull[extra_index_hull], hull[extra_next_index_hull],
                                hull[index_hull], hull[next_index_hull]);
            test_result_angle_list[3]
            = intersectionLines(hull[index_hull], hull[next_index_hull],
                                result_side_begin[0], result_side_end[0]);

            const double test_diff_area
                = fabs(fabs(contourArea(test_result_angle_list)) - experimental_area);
            if (min_diff_area > test_diff_area)
            {
                min_diff_area = test_diff_area;
                for (size_t i = 0; i < test_result_angle_list.size(); i++)
                {
                    result_angle_list[i] = test_result_angle_list[i];
                }
            }

            extra_index_hull = extra_next_index_hull;
        }
        while(extra_index_hull != unstable_pnt);

        index_hull = next_index_hull;
    }
    while(index_hull != unstable_pnt);

    // check label points
    if (norm(result_angle_list[0] - angle_list[1]) > 2) { result_angle_list[0] = angle_list[1]; }
    if (norm(result_angle_list[1] - angle_list[0]) > 2) { result_angle_list[1] = angle_list[0]; }
    if (norm(result_angle_list[3] - angle_list[2]) > 2) { result_angle_list[3] = angle_list[2]; }

    // check calculation point
    if (norm(result_angle_list[2] - angle_list[3]) >
       (norm(result_angle_list[0] - result_angle_list[1]) +
        norm(result_angle_list[0] - result_angle_list[3])) * 0.5 )
    { result_angle_list[2] = angle_list[3]; }

    return result_angle_list;
}

struct QRCodeDetector::Impl
{
public:
    Impl() { epsX = 0.2; epsY = 0.1; }
    ~Impl() {}

    double epsX, epsY;
};

QRCodeDetector::QRCodeDetector() : p(new Impl) {}
QRCodeDetector::~QRCodeDetector() {}

void QRCodeDetector::setEpsX(double epsX) { p->epsX = epsX; }
void QRCodeDetector::setEpsY(double epsY) { p->epsY = epsY; }

bool QRCodeDetector::detect(InputArray in, OutputArray points) const
{
    Mat inarr;
    if (!checkQRInputImage(in, inarr))
        return false;

    QRDetect qrdet;
    qrdet.init(inarr, p->epsX, p->epsY);
    if (!qrdet.localization()) { return false; }
    if (!qrdet.computeTransformationPoints()) { return false; }
    vector<Point2f> pnts2f = qrdet.getTransformationPoints();
    updatePointsResult(points, pnts2f);
    return true;
}

class QRDecode
{
public:
    void init(const Mat &src, const vector<Point2f> &points);
    Mat getIntermediateBarcode() { return intermediate; }
    Mat getStraightBarcode() { return straight; }
    size_t getVersion() { return version; }
    std::string getDecodeInformation() { return result_info; }
    bool straightDecodingProcess();
    bool curvedDecodingProcess();
protected:
    bool updatePerspective();
    bool versionDefinition();
    bool samplingForVersion();
    bool decodingProcess();
    Mat getTransformationMatrix();
    double getNumModules(Mat &warp);
    inline double pointPosition(Point2f a, Point2f b , Point2f c);
    float distancePointToLine(Point2f a, Point2f b , Point2f c);
    void getPointsInsideQRCode(const vector<Point2f> &angle_list);
    bool computeClosestPoints(const vector<Point> &result_integer_hull);
    bool computeSidesPoints(const vector<Point> &result_integer_hull);
    vector<Point> getPointsNearUnstablePoint(const vector<Point> &side, int start, int end, int step);
    bool findAndAddStablePoint();
    bool findIndexesCurvedSides();
    bool findIncompleteIndexesCurvedSides();
    Mat getPatternsMask();
    Point findClosestZeroPoint(Point2f original_point);
    bool findPatternsContours(vector<vector<Point> > &patterns_contours);
    bool findPatternsVerticesPoints(vector<vector<Point> > &patterns_vertices_points);
    bool findTempPatternsAddingPoints(vector<std::pair<int, vector<Point> > > &temp_patterns_add_points);
    bool computePatternsAddingPoints(std::map<int, vector<Point> > &patterns_add_points);
    bool addPointsToSides();
    void completeAndSortSides();
    vector<vector<float> > computeSpline(const vector<int> &x_arr, const vector<int> &y_arr);
    bool createSpline(vector<vector<Point2f> > &spline_lines);
    bool divideIntoEvenSegments(vector<vector<Point2f> > &segments_points);
    bool straightenQRCodeInParts();
    bool preparingCurvedQRCodes();

    const static int NUM_SIDES = 2;
    Mat original, bin_barcode, no_border_intermediate, intermediate, straight, curved_to_straight, test_image;
    vector<Point2f> original_points;
    vector<Point2f> original_curved_points;
    vector<Point> qrcode_locations;
    vector<std::pair<size_t, Point> > closest_points;
    vector<vector<Point> > sides_points;
    std::pair<size_t, Point> unstable_pair;
    vector<int> curved_indexes, curved_incomplete_indexes;
    std::map<int, vector<Point> > complete_curved_sides;
    std::string result_info;
    uint8_t version, version_size;
    float test_perspective_size;
    struct sortPairAsc
    {
        bool operator()(const std::pair<size_t, double> &a,
                        const std::pair<size_t, double> &b) const
        {
            return a.second < b.second;
        }
    };
    struct sortPairDesc
    {
        bool operator()(const std::pair<size_t, double> &a,
                        const std::pair<size_t, double> &b) const
        {
            return a.second > b.second;
        }
    };
    struct sortPointsByX
    {
        bool operator()(const Point &a, const Point &b) const
        {
            return a.x < b.x;
        }
    };
    struct sortPointsByY
    {
        bool operator()(const Point &a, const Point &b) const
        {
            return a.y < b.y;
        }
    };
};

float static getMinSideLen(const vector<Point2f> &points) {
    CV_Assert(points.size() == 4ull);
    double res = norm(points[1]-points[0]);
    for (size_t i = 1ull; i < points.size(); i++) {
        res = min(res, norm(points[i]-points[(i+1ull) % points.size()]));
    }
    return static_cast<float>(res);
}

void QRDecode::init(const Mat &src, const vector<Point2f> &points)
{
    CV_TRACE_FUNCTION();
    vector<Point2f> bbox = points;
    original = src.clone();
    test_image = src.clone();
    adaptiveThreshold(original, bin_barcode, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 83, 2);
    intermediate = Mat::zeros(original.size(), CV_8UC1);
    original_points = bbox;
    version = 0;
    version_size = 0;
    test_perspective_size = max(getMinSideLen(points)+1.f, 251.f);
    result_info = "";
}

inline double QRDecode::pointPosition(Point2f a, Point2f b , Point2f c)
{
    return (a.x - b.x) * (c.y - b.y) - (c.x - b.x) * (a.y - b.y);
}

float QRDecode::distancePointToLine(Point2f a, Point2f b , Point2f c)
{
    float A, B, C, result;
    A = c.y - b.y;
    B = c.x - b.x;
    C = c.x * b.y - b.x * c.y;
    float dist = sqrt(A*A + B*B);
    if (dist == 0) return 0;
    result = abs((A * a.x - B * a.y + C)) / dist;

    return result;
}

void QRDecode::getPointsInsideQRCode(const vector<Point2f> &angle_list)
{
    CV_TRACE_FUNCTION();
    size_t angle_size = angle_list.size();
    Mat contour_mask = Mat::zeros(bin_barcode.size(), CV_8UC1);
    for (size_t i = 0; i < angle_size; i++)
    {
        LineIterator line_iter(bin_barcode, angle_list[ i      % angle_size],
                                            angle_list[(i + 1) % angle_size]);
        for(int j = 0; j < line_iter.count; j++, ++line_iter)
        {
            Point p = line_iter.pos();
            contour_mask.at<uint8_t>(p + Point(1, 1)) = 255;
        }
    }
    Point2f center_point = intersectionLines(angle_list[0], angle_list[2],
                                             angle_list[1], angle_list[3]);
    floodFill(contour_mask, center_point, 255, 0, Scalar(), Scalar(), FLOODFILL_FIXED_RANGE);

    vector<Point> locations;
    findNonZero(contour_mask, locations);

    Mat fill_bin_barcode = bin_barcode.clone();
    Mat qrcode_mask = Mat::zeros(bin_barcode.rows + 2, bin_barcode.cols + 2, CV_8UC1);
    uint8_t value, mask_value;
    for(size_t i = 0; i < locations.size(); i++)
    {
        value = bin_barcode.at<uint8_t>(locations[i]);
        mask_value = qrcode_mask.at<uint8_t>(locations[i] + Point(1, 1));
        if (value == 0 && mask_value == 0)
        {
            floodFill(fill_bin_barcode, qrcode_mask, locations[i], 255,
                      0, Scalar(), Scalar(), FLOODFILL_MASK_ONLY);
        }
    }
    Mat qrcode_mask_roi = qrcode_mask(Range(1, qrcode_mask.rows - 1), Range(1, qrcode_mask.cols - 1));
    findNonZero(qrcode_mask_roi, qrcode_locations);
}

bool QRDecode::computeClosestPoints(const vector<Point> &result_integer_hull)
{
    CV_TRACE_FUNCTION();
    double min_norm, max_norm = 0.0;
    size_t idx_min = (size_t)-1;
    for (size_t i = 0; i < original_points.size(); i++)
    {
        min_norm = std::numeric_limits<double>::max();

        Point closest_pnt;
        for (size_t j = 0; j < result_integer_hull.size(); j++)
        {
            Point integer_original_point = original_points[i];
            double temp_norm = norm(integer_original_point - result_integer_hull[j]);
            if (temp_norm < min_norm)
            {
                min_norm = temp_norm;
                closest_pnt = result_integer_hull[j];
                idx_min = j;
            }
        }
        if (min_norm > max_norm)
        {
            max_norm = min_norm;
            unstable_pair = std::pair<size_t,Point>(i, closest_pnt);
        }
        CV_Assert(idx_min != (size_t)-1);
        closest_points.push_back(std::pair<size_t,Point>(idx_min, closest_pnt));
    }

    if (closest_points.size() != 4)
    {
        return false;
    }

    return true;
}

bool QRDecode::computeSidesPoints(const vector<Point> &result_integer_hull)
{
    size_t num_closest_points = closest_points.size();
    vector<Point> points;

    for(size_t i = 0; i < num_closest_points; i++)
    {
        points.clear();
        size_t start = closest_points[i].first,
               end   = closest_points[(i + 1) % num_closest_points].first;
        if (start < end)
        {
            points.insert(points.end(),
                          result_integer_hull.begin() + start,
                          result_integer_hull.begin() + end + 1);
        }
        else
        {
            points.insert(points.end(),
                          result_integer_hull.begin() + start,
                          result_integer_hull.end());
            points.insert(points.end(),
                          result_integer_hull.begin(),
                          result_integer_hull.begin() + end + 1);
        }
        if (abs(result_integer_hull[start].x - result_integer_hull[end].x) >
            abs(result_integer_hull[start].y - result_integer_hull[end].y))
        {
            if (points.front().x > points.back().x)
            {
                reverse(points.begin(), points.end());
            }
        }
        else
        {
            if (points.front().y > points.back().y)
            {
                reverse(points.begin(), points.end());
            }
        }
        if (points.empty())
        {
            return false;
        }
        sides_points.push_back(points);
    }

    return true;
}

vector<Point> QRDecode::getPointsNearUnstablePoint(const vector<Point> &side, int start, int end, int step)
{
    vector<Point> points;
    Point p1, p2, p3;

    double max_neighbour_angle = 1.0;
    int index_max_angle = start + step;
    bool enough_points = true;

    if(side.size() < 3)
    {
        points.insert(points.end(), side.begin(), side.end());
        return points;
    }
    const double cos_angle_threshold = -0.97;
    for (int i = start + step; i != end; i+= step)
    {
        p1 = side[i + step];
        if (norm(p1 - side[i])        < 5) { continue; }
        p2 = side[i];
        if (norm(p2 - side[i - step]) < 5) { continue; }
        p3 = side[i - step];

        double neighbour_angle = getCosVectors(p1, p2, p3);
        neighbour_angle = floor(neighbour_angle*1000)/1000;

        if ((neighbour_angle <= max_neighbour_angle) && (neighbour_angle < cos_angle_threshold))
        {
            max_neighbour_angle = neighbour_angle;
            index_max_angle = i;
        }
        else if (i == end - step)
        {
            enough_points = false;
            index_max_angle = i;
        }
    }

    if (enough_points)
    {
        p1 = side[index_max_angle + step];
        p2 = side[index_max_angle];
        p3 = side[index_max_angle - step];

        points.push_back(p1);
        points.push_back(p2);
        points.push_back(p3);
    }
    else
    {
        p1 = side[index_max_angle];
        p2 = side[index_max_angle - step];

        points.push_back(p1);
        points.push_back(p2);
    }

    return points;
}

bool QRDecode::findAndAddStablePoint()
{
    size_t idx_unstable_point = unstable_pair.first;
    Point unstable_point = unstable_pair.second;

    vector<Point> current_side_points, next_side_points;
    Point a1, a2, b1, b2;
    int start_current, end_current, step_current, start_next, end_next, step_next;
    vector<Point>::iterator it_a, it_b;

    vector<Point> &current_side = sides_points[(idx_unstable_point + 3) % 4];
    vector<Point> &next_side    = sides_points[idx_unstable_point];

    if(current_side.size() < 2 || next_side.size() < 2)
    {
        return false;
    }

    if(arePointsNearest(unstable_point, current_side.front(), 3.0))
    {
        start_current = (int)current_side.size() - 1;
        end_current = 0;
        step_current = -1;
        it_a = current_side.begin();
    }
    else if(arePointsNearest(unstable_point, current_side.back(), 3.0))
    {
        start_current = 0;
        end_current = (int)current_side.size() - 1;
        step_current = 1;
        it_a = current_side.end() - 1;
    }
    else
    {
        return false;
    }
    if(arePointsNearest(unstable_point, next_side.front(), 3.0))
    {
        start_next = (int)next_side.size() - 1;
        end_next = 0;
        step_next = -1;
        it_b = next_side.begin();
    }
    else if(arePointsNearest(unstable_point, next_side.back(), 3.0))
    {
        start_next = 0;
        end_next = (int)next_side.size() - 1;
        step_next = 1;
        it_b = next_side.end() - 1;
    }
    else
    {
        return false;
    }
    current_side_points = getPointsNearUnstablePoint(current_side, start_current, end_current, step_current);
    next_side_points    = getPointsNearUnstablePoint(next_side, start_next, end_next, step_next);

    if (current_side_points.size() < 2 || next_side_points.size() < 2)
    {
        return false;
    }

    a1 = current_side_points[0];
    a2 = current_side_points[1];

    b1 = next_side_points[0];
    b2 = next_side_points[1];

    if(norm(a1 - b1) < 10 && next_side_points.size() > 2)
    {
        b1 = next_side_points[1];
        b2 = next_side_points[2];
    }

    Point stable_point = intersectionLines(a1, a2, b1, b2);

    const double max_side = std::max(bin_barcode.size().width, bin_barcode.size().height);
    if ((abs(stable_point.x) > max_side) || (abs(stable_point.y) > max_side))
    {
        return false;
    }

    while (*it_a != a1)
    {
        it_a = current_side.erase(it_a);
        if (it_a == current_side.end())
        {
            it_a -= step_current;
        }
        Point point_to_remove_from_current = *it_a;
        if (point_to_remove_from_current.x > max_side || point_to_remove_from_current.y > max_side)
        {
            break;
        }
    }
    while (*it_b != b1)
    {
        it_b = next_side.erase(it_b);
        if (it_b == next_side.end())
        {
            it_b -= step_next;
        }
        Point point_to_remove_from_next = *it_b;
        if (point_to_remove_from_next.x > max_side || point_to_remove_from_next.y > max_side)
        {
            break;
        }
    }

    bool add_stable_point = true;

    for (size_t i = 0; i < original_points.size(); i++)
    {
        if(arePointsNearest(stable_point, original_points[i], 3.0))
        {
            add_stable_point = false;
            break;
        }
    }

    if(add_stable_point)
    {
        current_side.insert(it_a, stable_point);
        next_side.insert(it_b, stable_point);
        closest_points[unstable_pair.first].second = stable_point;
    }
    else
    {
        stable_point = original_points[unstable_pair.first];
        closest_points[unstable_pair.first].second = stable_point;
        current_side.insert(it_a, stable_point);
        next_side.insert(it_b, stable_point);
    }

    return true;
}

bool QRDecode::findIndexesCurvedSides()
{
    double max_dist_to_arc_side = 0.0;
    size_t num_closest_points = closest_points.size();
    int idx_curved_current = -1, idx_curved_opposite = -1;

    for (size_t i = 0; i < num_closest_points; i++)
    {
        double dist_to_arc = 0.0;

        Point arc_start = closest_points[i].second;
        Point arc_end   = closest_points[(i + 1) % num_closest_points].second;

        for (size_t j = 0; j < sides_points[i].size(); j++)
        {
            Point arc_point = sides_points[i][j];
            double dist = distancePointToLine(arc_point, arc_start, arc_end);
            dist_to_arc += dist;
        }
        dist_to_arc /= sides_points[i].size();

        if (dist_to_arc > max_dist_to_arc_side)
        {
            max_dist_to_arc_side = dist_to_arc;
            idx_curved_current = (int)i;
            idx_curved_opposite = (int)(i + 2) % num_closest_points;
        }
    }
    if (idx_curved_current == -1 || idx_curved_opposite == -1)
    {
        return false;
    }

    curved_indexes.push_back(idx_curved_current);
    curved_indexes.push_back(idx_curved_opposite);

    return true;
}

bool QRDecode::findIncompleteIndexesCurvedSides()
{
    int num_closest_points = (int)closest_points.size();

    for (int i = 0; i < NUM_SIDES; i++)
    {
        int idx_side = curved_indexes[i];
        int side_size = (int)sides_points[idx_side].size();

        double max_norm = norm(closest_points[idx_side].second -
                               closest_points[(idx_side + 1) % num_closest_points].second);
        double real_max_norm = 0;

        for (int j = 0; j < side_size - 1; j++)
        {
            double temp_norm = norm(sides_points[idx_side][j] -
                                    sides_points[idx_side][j + 1]);
            if (temp_norm > real_max_norm)
            {
                real_max_norm = temp_norm;
            }
        }
        if (real_max_norm > (0.5 * max_norm))
        {
            curved_incomplete_indexes.push_back(curved_indexes[i]);
        }

    }

    if (curved_incomplete_indexes.size() == 0)
    {
        return false;
    }
    return true;
}

Point QRDecode::findClosestZeroPoint(Point2f original_point)
{
    int orig_x = static_cast<int>(original_point.x);
    int orig_y = static_cast<int>(original_point.y);
    uint8_t value;
    Point zero_point;

    const int step = 2;
    for (int i = orig_x - step; i >= 0 && i <= orig_x + step; i++)
    {
        for (int j = orig_y - step; j >= 0 && j <= orig_y + step; j++)
        {
            Point p(i, j);
            value = bin_barcode.at<uint8_t>(p);
            if (value == 0) zero_point = p;
        }
    }

    return zero_point;
}

Mat QRDecode::getPatternsMask()
{
    Mat mask(bin_barcode.rows + 2, bin_barcode.cols + 2, CV_8UC1, Scalar(0));
    Mat patterns_mask(bin_barcode.rows + 2, bin_barcode.cols + 2, CV_8UC1, Scalar(0));
    Mat fill_bin_barcode = bin_barcode.clone();
    for (size_t i = 0; i < original_points.size(); i++)
    {
        if (i == 2) continue;
        Point p = findClosestZeroPoint(original_points[i]);
        floodFill(fill_bin_barcode, mask, p, 255,
                        0, Scalar(), Scalar(), FLOODFILL_MASK_ONLY);
        patterns_mask += mask;
    }
    Mat mask_roi = patterns_mask(Range(1, bin_barcode.rows - 1), Range(1, bin_barcode.cols - 1));

    return mask_roi;
}

bool QRDecode::findPatternsContours(vector<vector<Point> > &patterns_contours)
{
    Mat patterns_mask = getPatternsMask();
    findContours(patterns_mask, patterns_contours, RETR_EXTERNAL, CHAIN_APPROX_NONE, Point(0, 0));
    if (patterns_contours.size() != 3) {  return false; }
    return true;
}

bool QRDecode::findPatternsVerticesPoints(vector<vector<Point> > &patterns_vertices_points)
{
    vector<vector<Point> > patterns_contours;
    if(!findPatternsContours(patterns_contours))
    {
        return false;
    }
    const int num_vertices = 4;
    for(size_t i = 0; i < patterns_contours.size(); i++)
    {
        vector<Point> convexhull_contours, new_convexhull_contours;
        convexHull(patterns_contours[i], convexhull_contours);

        size_t number_pnts_in_hull = convexhull_contours.size();
        vector<std::pair<size_t, double> > cos_angles_in_hull;
        vector<size_t> min_angle_pnts_indexes;

        for(size_t j = 1; j < number_pnts_in_hull + 1; j++)
        {
            double cos_angle = getCosVectors(convexhull_contours[(j - 1) % number_pnts_in_hull],
                                             convexhull_contours[ j      % number_pnts_in_hull],
                                             convexhull_contours[(j + 1) % number_pnts_in_hull]);
            cos_angles_in_hull.push_back(std::pair<size_t, double>(j, cos_angle));
        }

        sort(cos_angles_in_hull.begin(), cos_angles_in_hull.end(), sortPairDesc());

        for (size_t j = 0; j < cos_angles_in_hull.size(); j++)
        {
            bool add_edge = true;
            for(size_t k = 0; k < min_angle_pnts_indexes.size(); k++)
            {
                if(norm(convexhull_contours[cos_angles_in_hull[j].first % number_pnts_in_hull] -
                        convexhull_contours[min_angle_pnts_indexes[k]   % number_pnts_in_hull]) < 3)
                {
                    add_edge = false;
                }
            }
            if (add_edge)
            {
                min_angle_pnts_indexes.push_back(cos_angles_in_hull[j].first % number_pnts_in_hull);
            }
            if ((int)min_angle_pnts_indexes.size() == num_vertices) { break; }
        }
        sort(min_angle_pnts_indexes.begin(), min_angle_pnts_indexes.end());

        vector<Point> contour_vertices_points;

        for (size_t k = 0; k < min_angle_pnts_indexes.size(); k++)
        {
            contour_vertices_points.push_back(convexhull_contours[min_angle_pnts_indexes[k]]);
        }
        patterns_vertices_points.push_back(contour_vertices_points);
    }
    if (patterns_vertices_points.size() != 3)
    {
        return false;
    }

    return true;
}

bool QRDecode::findTempPatternsAddingPoints(vector<std::pair<int, vector<Point> > > &temp_patterns_add_points)
{
    vector<vector<Point> >patterns_contours, patterns_vertices_points;
    if(!findPatternsVerticesPoints(patterns_vertices_points))
    {
        return false;
    }
    if(!findPatternsContours(patterns_contours))
    {
        return false;
    }

    for (size_t i = 0; i < curved_incomplete_indexes.size(); i++)
    {
        int idx_curved_side = curved_incomplete_indexes[i];
        Point close_transform_pnt_curr = original_points[idx_curved_side];
        Point close_transform_pnt_next = original_points[(idx_curved_side + 1) % 4];

        vector<size_t> patterns_indexes;

        for (size_t j = 0; j < patterns_vertices_points.size(); j++)
        {
            for (size_t k = 0; k < patterns_vertices_points[j].size(); k++)
            {
                if (norm(close_transform_pnt_curr - patterns_vertices_points[j][k]) < 5)
                {
                    patterns_indexes.push_back(j);
                    break;
                }
                if (norm(close_transform_pnt_next - patterns_vertices_points[j][k]) < 5)
                {
                    patterns_indexes.push_back(j);
                    break;
                }
            }
        }
        for (size_t j = 0; j < patterns_indexes.size(); j++)
        {
            vector<Point> vertices = patterns_vertices_points[patterns_indexes[j]];
            vector<std::pair<int, double> > vertices_dist_pair;
            vector<Point> points;
            for (size_t k = 0; k < vertices.size(); k++)
            {
                double dist_to_side = distancePointToLine(vertices[k], close_transform_pnt_curr,
                                                                       close_transform_pnt_next);
                vertices_dist_pair.push_back(std::pair<int, double>((int)k, dist_to_side));
            }
            if (vertices_dist_pair.size() == 0)
            {
                return false;
            }
            sort(vertices_dist_pair.begin(), vertices_dist_pair.end(), sortPairAsc());
            Point p1, p2;
            int index_p1_in_vertices = 0, index_p2_in_vertices = 0;
            for (int k = 4; k > 0; k--)
            {
                if((vertices_dist_pair[0].first == k % 4) && (vertices_dist_pair[1].first == (k - 1) % 4))
                {
                    index_p1_in_vertices = vertices_dist_pair[0].first;
                    index_p2_in_vertices = vertices_dist_pair[1].first;
                }
                else if((vertices_dist_pair[1].first == k % 4) && (vertices_dist_pair[0].first == (k - 1) % 4))
                {
                    index_p1_in_vertices = vertices_dist_pair[1].first;
                    index_p2_in_vertices = vertices_dist_pair[0].first;
                }
            }
            if (index_p1_in_vertices == index_p2_in_vertices) return false;

            p1 = vertices[index_p1_in_vertices];
            p2 = vertices[index_p2_in_vertices];

            size_t index_p1_in_contour = 0, index_p2_in_contour = 0;
            vector<Point> add_points = patterns_contours[patterns_indexes[j]];

            for(size_t k = 0; k < add_points.size(); k++)
            {
                if (add_points[k] == p1)
                {
                    index_p1_in_contour = k;
                }
                if (add_points[k] == p2)
                {
                    index_p2_in_contour = k;
                }
            }

            if (index_p1_in_contour > index_p2_in_contour)
            {
                for (size_t k = index_p1_in_contour; k < add_points.size(); k++)
                {
                    points.push_back(add_points[k]);
                }
                for (size_t k = 0; k <= index_p2_in_contour; k++)
                {
                    points.push_back(add_points[k]);
                }
            }
            else if (index_p1_in_contour < index_p2_in_contour)
            {
                for (size_t k = index_p1_in_contour; k <= index_p2_in_contour; k++)
                {
                    points.push_back(add_points[k]);
                }
            }
            else
            {
                return false;
            }
            if (abs(p1.x - p2.x) > abs(p1.y - p2.y))
            {
                sort(points.begin(), points.end(), sortPointsByX());
            }
            else
            {
                sort(points.begin(), points.end(), sortPointsByY());
            }

            temp_patterns_add_points.push_back(std::pair<int, vector<Point> >(idx_curved_side,points));
        }
    }

    return true;
}

bool QRDecode::computePatternsAddingPoints(std::map<int, vector<Point> > &patterns_add_points)
{
    vector<std::pair<int, vector<Point> > > temp_patterns_add_points;
    if(!findTempPatternsAddingPoints(temp_patterns_add_points))
    {
        return false;
    }

    const int num_points_in_pattern = 3;
    for(size_t i = 0; i < temp_patterns_add_points.size(); i++)
    {
        int idx_side = temp_patterns_add_points[i].first;
        int size = (int)temp_patterns_add_points[i].second.size();

        float step = static_cast<float>(size) / num_points_in_pattern;
        vector<Point> temp_points;
        for (int j = 0; j < num_points_in_pattern; j++)
        {
            float val = j * step;
            int idx = cvRound(val) >= size ? size - 1 : cvRound(val);
            temp_points.push_back(temp_patterns_add_points[i].second[idx]);
        }
        temp_points.push_back(temp_patterns_add_points[i].second.back());
        if(patterns_add_points.count(idx_side) == 1)
        {
            patterns_add_points[idx_side].insert(patterns_add_points[idx_side].end(),
                                                temp_points.begin(), temp_points.end());
        }
        patterns_add_points.insert(std::pair<int, vector<Point> >(idx_side, temp_points));

    }
    if (patterns_add_points.size() == 0)
    {
        return false;
    }

    return true;
}

bool QRDecode::addPointsToSides()
{
    if(!computePatternsAddingPoints(complete_curved_sides))
    {
        return false;
    }
    std::map<int, vector<Point> >::iterator it;
    double mean_step = 0.0;
    size_t num_points_at_side = 0;
    for (it = complete_curved_sides.begin(); it != complete_curved_sides.end(); ++it)
    {
        int count = -1;
        const size_t num_points_at_pattern = it->second.size();
        for(size_t j = 0; j < num_points_at_pattern - 1; j++, count++)
        {
            if (count == 3) continue;
            double temp_norm = norm(it->second[j] -
                                    it->second[j + 1]);
            mean_step += temp_norm;
        }
        num_points_at_side += num_points_at_pattern;
    }
    if (num_points_at_side == 0)
    {
        return false;
    }
    mean_step /= num_points_at_side;

    const size_t num_incomplete_sides = curved_incomplete_indexes.size();
    for (size_t i = 0; i < num_incomplete_sides; i++)
    {
        int idx = curved_incomplete_indexes[i];
        vector<int> sides_points_indexes;

        const int num_points_at_side_to_add = (int)sides_points[idx].size();
        for (int j = 0; j < num_points_at_side_to_add; j++)
        {
            bool not_too_close = true;
            const size_t num_points_at_side_exist = complete_curved_sides[idx].size();
            for (size_t k = 0; k < num_points_at_side_exist; k++)
            {
                double temp_norm = norm(sides_points[idx][j] - complete_curved_sides[idx][k]);
                if (temp_norm < mean_step)
                {
                    not_too_close = false;
                    break;
                }
            }
            if (not_too_close)
            {
                sides_points_indexes.push_back(j);
            }
        }

        for (size_t j = 0; j < sides_points_indexes.size(); j++)
        {
            bool not_equal = true;
            for (size_t k = 0; k < complete_curved_sides[idx].size(); k++)
            {
                if (sides_points[idx][sides_points_indexes[j]] ==
                    complete_curved_sides[idx][k])
                {
                    not_equal = false;
                }
            }
            if (not_equal)
            {
                complete_curved_sides[idx].push_back(sides_points[idx][sides_points_indexes[j]]);
            }
        }
    }

    return true;
}

void QRDecode::completeAndSortSides()
{
    if (complete_curved_sides.size() < 2)
    {
        for (int i = 0; i < NUM_SIDES; i++)
        {
            if(complete_curved_sides.count(curved_indexes[i]) == 0)
            {
                int idx_second_cur_side = curved_indexes[i];
                complete_curved_sides.insert(std::pair<int,vector<Point> >(idx_second_cur_side, sides_points[idx_second_cur_side]));
            }
        }
    }
    std::map<int,vector<Point> >::iterator it;
    for (it = complete_curved_sides.begin(); it != complete_curved_sides.end(); ++it)
    {
        Point p1 = it->second.front();
        Point p2 = it->second.back();
        if (abs(p1.x - p2.x) > abs(p1.y - p2.y))
        {
            sort(it->second.begin(), it->second.end(), sortPointsByX());
        }
        else
        {
            sort(it->second.begin(), it->second.end(), sortPointsByY());
        }
    }
}

vector<vector<float> > QRDecode::computeSpline(const vector<int> &x_arr, const vector<int> &y_arr)
{
    const int n = (int)x_arr.size();
    vector<float> a, b(n - 1), d(n - 1), h(n - 1), alpha(n - 1), c(n), l(n), mu(n), z(n);

    for (int i = 0; i < (int)y_arr.size(); i++)
    {
        a.push_back(static_cast<float>(x_arr[i]));
    }
    for (int i = 0; i < n - 1; i++)
    {
        h[i] = static_cast<float>(y_arr[i + 1] - y_arr[i]);
    }
    for (int i = 1; i < n - 1; i++)
    {
        alpha[i] = 3 / h[i] * (a[i + 1] - a[i]) - 3 / (h[i - 1]) * (a[i] - a[i - 1]);
    }
    l[0] = 1;
    mu[0] = 0;
    z[0] = 0;

    for (int i = 1; i < n - 1; i++)
    {
        l[i] = 2 * (y_arr[i + 1] - y_arr[i - 1]) - h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }
    l[n - 1] = 1;
    z[n - 1] = 0;
    c[n - 1] = 0;

    for(int j = n - 2; j >= 0; j--)
    {
        c[j] = z[j] - mu[j] * c[j + 1];
        b[j] = (a[j + 1] - a[j]) / h[j] - (h[j] * (c[j + 1] + 2 * c[j])) / 3;
        d[j] = (c[j + 1] - c[j]) / (3 * h[j]);
    }

    vector<vector<float> > S(n - 1);
    for (int i = 0; i < n - 1; i++)
    {
        S[i].push_back(a[i]);
        S[i].push_back(b[i]);
        S[i].push_back(c[i]);
        S[i].push_back(d[i]);
    }

    return S;
}

bool QRDecode::createSpline(vector<vector<Point2f> > &spline_lines)
{
    int start, end;
    vector<vector<float> > S;

    for (int idx = 0; idx < NUM_SIDES; idx++)
    {
        int idx_curved_side = curved_indexes[idx];

        vector<Point> spline_points = complete_curved_sides.find(idx_curved_side)->second;
        vector<int> x_arr, y_arr;

        for (size_t j = 0; j < spline_points.size(); j++)
        {
            x_arr.push_back(cvRound(spline_points[j].x));
            y_arr.push_back(cvRound(spline_points[j].y));
        }

        bool horizontal_order = abs(x_arr.front() - x_arr.back()) > abs(y_arr.front() - y_arr.back());
        vector<int>& second_arr = horizontal_order ? x_arr : y_arr;
        vector<int>& first_arr  = horizontal_order ? y_arr : x_arr;

        S = computeSpline(first_arr, second_arr);

        int closest_point_first  = horizontal_order ? closest_points[idx_curved_side].second.x
                                                    : closest_points[idx_curved_side].second.y;
        int closest_point_second = horizontal_order ? closest_points[(idx_curved_side + 1) % 4].second.x
                                                    : closest_points[(idx_curved_side + 1) % 4].second.y;

        start = idx_curved_side;
        end = (idx_curved_side + 1) % 4;
        if(closest_point_first > closest_point_second)
        {
            start = (idx_curved_side + 1) % 4;
            end = idx_curved_side;
        }

        int closest_point_start = horizontal_order ? closest_points[start].second.x : closest_points[start].second.y;
        int closest_point_end   = horizontal_order ? closest_points[end].second.x   : closest_points[end].second.y;

        for (int index = closest_point_start; index <= closest_point_end; index++)
        {
            if (index == second_arr.front())
            {
                spline_lines[idx].push_back(closest_points[start].second);
            }
            for (size_t i = 0; i < second_arr.size() - 1; i++)
            {
                if ((index > second_arr[i]) && (index <= second_arr[i + 1]))
                {
                    float val = S[i][0] + S[i][1] * (index - second_arr[i]) + S[i][2] * (index - second_arr[i]) * (index - second_arr[i])
                                                                            + S[i][3] * (index - second_arr[i]) * (index - second_arr[i]) * (index - second_arr[i]);
                    spline_lines[idx].push_back(horizontal_order ? Point2f(static_cast<float>(index), val) : Point2f(val, static_cast<float>(index)));
                }
            }
        }
    }
    for (int i = 0; i < NUM_SIDES; i++)
    {
        if (spline_lines[i].size() == 0)
        {
            return false;
        }
    }
    return true;
}

bool QRDecode::divideIntoEvenSegments(vector<vector<Point2f> > &segments_points)
{
    vector<vector<Point2f> > spline_lines(NUM_SIDES);
    if (!createSpline(spline_lines))
    {
        return false;
    }
    float mean_num_points_in_line = 0.0;
    for (int i = 0; i < NUM_SIDES; i++)
    {
        mean_num_points_in_line += spline_lines[i].size();
    }
    mean_num_points_in_line /= NUM_SIDES;
    const int min_num_points = 1, max_num_points = cvRound(mean_num_points_in_line / 2.0);
    float linear_threshold = 0.5f;
    for (int num = min_num_points; num < max_num_points; num++)
    {
        for (int i = 0; i < NUM_SIDES; i++)
        {
            segments_points[i].clear();

            int size = (int)spline_lines[i].size();
            float step = static_cast<float>(size) / num;
            for (int j = 0; j < num; j++)
            {
                float val = j * step;
                int idx = cvRound(val) >= size ? size - 1 : cvRound(val);
                segments_points[i].push_back(spline_lines[i][idx]);
            }
            segments_points[i].push_back(spline_lines[i].back());
        }
        float mean_of_two_sides = 0.0;
        for (int i = 0; i < NUM_SIDES; i++)
        {
            float mean_dist_in_segment = 0.0;
            for (size_t j = 0; j < segments_points[i].size() - 1; j++)
            {
                Point2f segment_start = segments_points[i][j];
                Point2f segment_end   = segments_points[i][j + 1];
                vector<Point2f>::iterator it_start, it_end, it;
                it_start = find(spline_lines[i].begin(), spline_lines[i].end(), segment_start);
                it_end   = find(spline_lines[i].begin(), spline_lines[i].end(), segment_end);
                float max_dist_to_line = 0.0;
                for (it = it_start; it != it_end; it++)
                {
                    float temp_dist = distancePointToLine(*it, segment_start, segment_end);
                    if (temp_dist > max_dist_to_line)
                    {
                        max_dist_to_line = temp_dist;
                    }
                }
                mean_dist_in_segment += max_dist_to_line;
            }
            mean_dist_in_segment /= segments_points[i].size();
            mean_of_two_sides    += mean_dist_in_segment;
        }
        mean_of_two_sides /= NUM_SIDES;
        if (mean_of_two_sides < linear_threshold)
        {
            break;
        }
    }

    return true;
}

bool QRDecode::straightenQRCodeInParts()
{
    vector<vector<Point2f> > segments_points(NUM_SIDES);
    if (!divideIntoEvenSegments(segments_points))
    {
        return false;
    }
    vector<Point2f> current_curved_side, opposite_curved_side;

    for (int i = 0; i < NUM_SIDES; i++)
    {
        Point2f temp_point_start = segments_points[i].front();
        Point2f temp_point_end   = segments_points[i].back();
        bool horizontal_order = (abs(temp_point_start.x - temp_point_end.x) >
                                 abs(temp_point_start.y - temp_point_end.y));
        float compare_point_current  = horizontal_order ? segments_points[i].front().y
                                                        : segments_points[(i + 1) % 2].front().x;
        float compare_point_opposite = horizontal_order ? segments_points[(i + 1) % 2].front().y
                                                        : segments_points[i].front().x;

        if (compare_point_current > compare_point_opposite)
        {
            current_curved_side  = segments_points[i];
            opposite_curved_side = segments_points[(i + 1) % 2];
        }
    }
    if (current_curved_side.size() != opposite_curved_side.size())
    {
        return false;
    }
    size_t number_pnts_to_cut = current_curved_side.size();
    if (number_pnts_to_cut == 0)
    {
        return false;
    }
    float perspective_curved_size = max(getMinSideLen(original_points)+1.f, 251.f);;
    const Size temporary_size(cvRound(perspective_curved_size), cvRound(perspective_curved_size));

    float dist = perspective_curved_size / (number_pnts_to_cut - 1);
    Mat perspective_result = Mat::zeros(temporary_size, CV_8UC1);
    vector<Point2f> curved_parts_points;

    float start_cut = 0.0;
    vector<Point2f> temp_closest_points(4);

    for (size_t i = 1; i < number_pnts_to_cut; i++)
    {
        curved_parts_points.clear();
        Mat test_mask = Mat::zeros(bin_barcode.size(), CV_8UC1);

        Point2f start_point = current_curved_side[i];
        Point2f prev_start_point = current_curved_side[i - 1];
        Point2f finish_point = opposite_curved_side[i];
        Point2f prev_finish_point = opposite_curved_side[i - 1];

        for (size_t j = 0; j < qrcode_locations.size(); j++)
        {
            if ((pointPosition(start_point, finish_point, qrcode_locations[j]) >= 0) &&
                (pointPosition(prev_start_point, prev_finish_point, qrcode_locations[j]) <= 0))
            {
                test_mask.at<uint8_t>(qrcode_locations[j]) = 255;
            }
        }

        vector<Point2f> perspective_points;

        perspective_points.push_back(Point2f(0.0, start_cut));
        perspective_points.push_back(Point2f(perspective_curved_size, start_cut));

        perspective_points.push_back(Point2f(perspective_curved_size, start_cut + dist));
        perspective_points.push_back(Point2f(0.0, start_cut+dist));

        perspective_points.push_back(Point2f(perspective_curved_size * 0.5f, start_cut + dist * 0.5f));

        if (i == 1)
        {
            for (size_t j = 0; j < closest_points.size(); j++)
            {
                if (arePointsNearest(closest_points[j].second, prev_start_point, 3.0))
                {
                    temp_closest_points[j] = perspective_points[0];
                }
                else if (arePointsNearest(closest_points[j].second, prev_finish_point, 3.0))
                {
                    temp_closest_points[j] = perspective_points[1];
                }
            }
        }
        if (i == number_pnts_to_cut - 1)
        {
            for (size_t j = 0; j < closest_points.size(); j++)
            {
                if (arePointsNearest(closest_points[j].second, finish_point, 3.0))
                {
                    temp_closest_points[j] = perspective_points[2];
                }
                else if (arePointsNearest(closest_points[j].second, start_point, 3.0))
                {
                    temp_closest_points[j] = perspective_points[3];
                }
            }
        }
        start_cut += dist;

        curved_parts_points.push_back(prev_start_point);
        curved_parts_points.push_back(prev_finish_point);
        curved_parts_points.push_back(finish_point);
        curved_parts_points.push_back(start_point);

        Point2f center_point = intersectionLines(curved_parts_points[0], curved_parts_points[2],
                                                 curved_parts_points[1], curved_parts_points[3]);
        if (cvIsNaN(center_point.x) || cvIsNaN(center_point.y))
            return false;

        vector<Point2f> pts = curved_parts_points;
        pts.push_back(center_point);

        Mat H = findHomography(pts, perspective_points);
        Mat temp_intermediate(temporary_size, CV_8UC1);
        warpPerspective(test_mask, temp_intermediate, H, temporary_size, INTER_NEAREST);
        perspective_result += temp_intermediate;

    }
    Mat white_mask = Mat(temporary_size, CV_8UC1, Scalar(255));
    Mat inversion = white_mask - perspective_result;
    Mat temp_result;

    original_curved_points = temp_closest_points;

    Point2f original_center_point = intersectionLines(original_curved_points[0], original_curved_points[2],
                                                      original_curved_points[1], original_curved_points[3]);

    original_curved_points.push_back(original_center_point);

    for (size_t i = 0; i < original_curved_points.size(); i++)
    {
        if (cvIsNaN(original_curved_points[i].x) || cvIsNaN(original_curved_points[i].y))
            return false;
    }

    vector<Point2f> perspective_straight_points;
    perspective_straight_points.push_back(Point2f(0.f, 0.f));
    perspective_straight_points.push_back(Point2f(perspective_curved_size, 0.f));

    perspective_straight_points.push_back(Point2f(perspective_curved_size, perspective_curved_size));
    perspective_straight_points.push_back(Point2f(0.f, perspective_curved_size));

    perspective_straight_points.push_back(Point2f(perspective_curved_size * 0.5f, perspective_curved_size * 0.5f));

    Mat H = findHomography(original_curved_points, perspective_straight_points);
    warpPerspective(inversion, temp_result, H, temporary_size, INTER_NEAREST, BORDER_REPLICATE);

    no_border_intermediate = temp_result(Range(1, temp_result.rows), Range(1, temp_result.cols));
    const int border = cvRound(0.1 * perspective_curved_size);
    const int borderType = BORDER_CONSTANT;
    copyMakeBorder(no_border_intermediate, curved_to_straight, border, border, border, border, borderType, Scalar(255));
    intermediate = curved_to_straight;

    return true;
}

bool QRDecode::preparingCurvedQRCodes()
{
    vector<Point> result_integer_hull;
    getPointsInsideQRCode(original_points);
    if (qrcode_locations.size() == 0)
        return false;
    convexHull(qrcode_locations, result_integer_hull);
    if (!computeClosestPoints(result_integer_hull))
        return false;
    if (!computeSidesPoints(result_integer_hull))
        return false;
    if (!findAndAddStablePoint())
        return false;
    if (!findIndexesCurvedSides())
        return false;
    if (findIncompleteIndexesCurvedSides())
    {
        if(!addPointsToSides())
            return false;
    }
    completeAndSortSides();
    if (!straightenQRCodeInParts())
        return false;

    return true;
}

Mat QRDecode::getTransformationMatrix() {
    const Point2f centerPt = intersectionLines(original_points[0], original_points[2],
                                               original_points[1], original_points[3]);
    if (cvIsNaN(centerPt.x) || cvIsNaN(centerPt.y))
        return Mat();

    const Size temporary_size(cvRound(test_perspective_size), cvRound(test_perspective_size));

    vector<Point2f> perspective_points;
    perspective_points.push_back(Point2f(0.f, 0.f));
    perspective_points.push_back(Point2f(test_perspective_size, 0.f));

    perspective_points.push_back(Point2f(test_perspective_size, test_perspective_size));
    perspective_points.push_back(Point2f(0.f, test_perspective_size));

    perspective_points.push_back(Point2f(test_perspective_size * 0.5f, test_perspective_size * 0.5f));

    vector<Point2f> pts = original_points;
    pts.push_back(centerPt);

    Mat H = findHomography(pts, perspective_points);
    
    vector<Point2f> vps{original_points[0], original_points[1],
                        original_points[2], original_points[3]};
    std::cout << vps << std::endl;
    perspectiveTransform(vps, vps, H);
    std::cout << vps << std::endl;
    perspectiveTransform(vps, vps, H.inv());
    std::cout << vps << std::endl;
    std::cout << std::endl;
    return H;
}

double QRDecode::getNumModules(cv::Mat &warp) {
    //imwrite("test_bin_barcode.png", bin_barcode);
    double numModules = -1;
    vector<vector<Point>> vertices;
    bool flag = findPatternsVerticesPoints(vertices);

    if (flag) {
        vector<double> offsets;
        for (auto& v : vertices) {
            vector<Point2f> vf = {v[0], v[1], v[2], v[3]};
            // original or bin_barcode
            //cornerSubPix(bin_barcode, vf, Size(3, 3), Size(-1, -1),
            //            TermCriteria(TermCriteria::MAX_ITER | TermCriteria::EPS, 30, 0.1));
            perspectiveTransform(vf, vf, warp);
            const Size temporary_size(cvRound(test_perspective_size), cvRound(test_perspective_size));
            for (int i = 1; i < 4; i++) {
                offsets.push_back(cv::norm(vf[i] - vf[i-1]));
            }
            offsets.push_back(cv::norm(vf[3] - vf[0]));
        }
        double moduleSize = 0.;
        for (double offset : offsets)
            moduleSize += offset;
        moduleSize /= (3.*4.*7.);
        numModules = (double)test_perspective_size / moduleSize;
    }
    return numModules;
}

bool QRDecode::updatePerspective()
{
    CV_TRACE_FUNCTION();
    Mat H = getTransformationMatrix();
    if (H.empty())
        return false;

    double numModules = getNumModules(H);
    if (numModules > 0) {
        version = cvRound((numModules - 21) * .25) + 1;
        if (version >= 1 && version <= 40) {
            version_size = (version-1) * 4 + 21;
            test_perspective_size = numModules * 3;

            while (test_perspective_size < 250.f)
                test_perspective_size *= 2.f;
            test_perspective_size += 1.f;
            H = getTransformationMatrix();

            //if (version > 7) {
            //    H = getTransformationMatrix();
            //    numModules = getNumModules(H);
            //    version = cvRound((numModules - 21) * .25) + 1;
            //    version_size = (version-1) * 4 + 21;
            //    test_perspective_size = numModules * 4 + 1.f;
            //}
        }
        else
            return false;
    }
    else
        return false;

    Mat bin_original, temp_intermediate;
    adaptiveThreshold(original, bin_original, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 83, 2);
    const Size temporary_size(cvRound(test_perspective_size), cvRound(test_perspective_size));
    warpPerspective(bin_original, temp_intermediate, H, temporary_size, INTER_NEAREST);
    no_border_intermediate = temp_intermediate(Range(1, temp_intermediate.rows), Range(1, temp_intermediate.cols));

    const int border = cvRound(0.1 * test_perspective_size);
    const int borderType = BORDER_CONSTANT;
    copyMakeBorder(no_border_intermediate, intermediate, border, border, border, border, borderType, Scalar(255));
    //imwrite("test_intermediate.png", no_border_intermediate);
    return true;
}

inline Point computeOffset(const vector<Point>& v)
{
    // compute the width/height of convex hull
    Rect areaBox = boundingRect(v);

    // compute the good offset
    // the box is consisted by 7 steps
    // to pick the middle of the stripe, it needs to be 1/14 of the size
    const int cStep = 7 * 2;
    Point offset = Point(areaBox.width, areaBox.height);
    offset /= cStep;
    return offset;
}

bool QRDecode::versionDefinition()
{
    CV_TRACE_FUNCTION();
    LineIterator line_iter(intermediate, Point2f(0, 0), Point2f(test_perspective_size, test_perspective_size));
    Point black_point = Point(0, 0);
    for(int j = 0; j < line_iter.count; j++, ++line_iter)
    {
        const uint8_t value = intermediate.at<uint8_t>(line_iter.pos());
        if (value == 0)
        {
            black_point = line_iter.pos();
            break;
        }
    }

    Mat mask = Mat::zeros(intermediate.rows + 2, intermediate.cols + 2, CV_8UC1);
    floodFill(intermediate, mask, black_point, 255, 0, Scalar(), Scalar(), FLOODFILL_MASK_ONLY);

    vector<Point> locations, non_zero_elem;
    Mat mask_roi = mask(Range(1, intermediate.rows - 1), Range(1, intermediate.cols - 1));
    findNonZero(mask_roi, non_zero_elem);
    convexHull(non_zero_elem, locations);
    Point offset = computeOffset(locations);

    Point temp_remote = locations[0], remote_point;
    const Point delta_diff = offset;
    for (size_t i = 0; i < locations.size(); i++)
    {
        if (norm(black_point - temp_remote) <= norm(black_point - locations[i]))
        {
            const uint8_t value = intermediate.at<uint8_t>(temp_remote - delta_diff);
            temp_remote = locations[i];
            if (value == 0) { remote_point = temp_remote - delta_diff; }
            else { remote_point = temp_remote - (delta_diff / 2); }
        }
    }

    size_t transition_x = 0 , transition_y = 0;

    uint8_t future_pixel = 255;
    const uint8_t *intermediate_row = intermediate.ptr<uint8_t>(remote_point.y);
    for(int i = remote_point.x; i < intermediate.cols; i++)
    {
        if (intermediate_row[i] == future_pixel)
        {
            future_pixel = static_cast<uint8_t>(~future_pixel);
            transition_x++;
        }
    }

    future_pixel = 255;
    for(int j = remote_point.y; j < intermediate.rows; j++)
    {
        const uint8_t value = intermediate.at<uint8_t>(Point(j, remote_point.x));
        if (value == future_pixel)
        {
            future_pixel = static_cast<uint8_t>(~future_pixel);
            transition_y++;
        }
    }
    version = saturate_cast<uint8_t>((std::min(transition_x, transition_y) - 1) * 0.25 - 1);
    if ( !(  0 < version && version <= 40 ) ) { return false; }
    version_size = 21 + (version - 1) * 4;
    return true;
}

static inline int getQRVersion(uint8_t bs[6]) {
    int res = 0;
    for (int i = 0; i < 6; i++) {
        if (bs[i] == 0)
            res |= 1 << i;
    }
    return res;
}

bool QRDecode::samplingForVersion()
{
    CV_TRACE_FUNCTION();
    version_size = 21 + (version - 1) * 4;
    const double multiplyingFactor = (version < 3)  ? 1. :
                                     (version == 3) ? 2. :
                                     3.;
    //imwrite("no_border_intermediate.png", no_border_intermediate);
    const Size newFactorSize(
                  cvRound(no_border_intermediate.size().width  * multiplyingFactor),
                  cvRound(no_border_intermediate.size().height * multiplyingFactor));
    Mat postIntermediate(newFactorSize, CV_8UC1);
    resize(no_border_intermediate, postIntermediate, newFactorSize, 0, 0, INTER_AREA);

    const int delta_rows = cvRound((postIntermediate.rows * 1.0) / version_size);
    const int delta_cols = cvRound((postIntermediate.cols * 1.0) / version_size);
    // number of elements in the tail
    const int skipped_rows = postIntermediate.rows - delta_rows * version_size;
    const int skipped_cols = postIntermediate.cols - delta_cols * version_size;

    vector<int> deltas_rows(version_size, delta_rows);
    vector<int> deltas_cols(version_size, delta_cols);

    for (int i = 0; i < abs(skipped_rows); i++) {
        // fix deltas_rows at each skip_step
        const double skip_step = static_cast<double>(version_size)/abs(skipped_rows);
        const int corrected_index = static_cast<int>(i*skip_step + skip_step/2);
        deltas_rows[corrected_index] += skipped_rows > 0 ? 1 : -1;
    }
    for (int i = 0; i < abs(skipped_cols); i++) {
        // fix deltas_cols at each skip_step
        const double skip_step = static_cast<double>(version_size)/abs(skipped_cols);
        const int corrected_index = static_cast<int>(i*skip_step + skip_step/2);
        deltas_cols[corrected_index] += skipped_cols > 0 ? 1 : -1;
    }

    const double totalFrequencyElem = countNonZero(postIntermediate) / static_cast<double>(postIntermediate.total());
    straight = Mat(Size(version_size, version_size), CV_8UC1, Scalar(0));

    for (int r = 0, i = 0; i < version_size; r += deltas_rows[i], i++) {
        for (int c = 0, j = 0; j < version_size; c += deltas_cols[j], j++) {
            Mat tile = postIntermediate(
                           Range(r, min(r + delta_rows, postIntermediate.rows)),
                           Range(c, min(c + delta_cols, postIntermediate.cols)));
            const double frequencyElem = (countNonZero(tile) * 1.0) / tile.total();
            straight.ptr<uint8_t>(i)[j] = (frequencyElem < totalFrequencyElem) ? 0 : 255;
        }
    }
    if (version >= 7) {
        uint8_t bs[6];
        bs[5] = straight.at<uint8_t>(version_size - 9, 5);
        bs[4] = straight.at<uint8_t>(version_size - 10, 5);
        bs[3] = straight.at<uint8_t>(version_size - 11, 5);
        bs[2] = straight.at<uint8_t>(version_size - 9, 4);
        bs[1] = straight.at<uint8_t>(version_size - 10, 4);
        bs[0] = straight.at<uint8_t>(version_size - 11, 4);
        int v1 = getQRVersion(bs);

        bs[5] = straight.at<uint8_t>(5, version_size - 9);
        bs[4] = straight.at<uint8_t>(5, version_size - 10);
        bs[3] = straight.at<uint8_t>(5, version_size - 11);
        bs[2] = straight.at<uint8_t>(4, version_size - 9);
        bs[1] = straight.at<uint8_t>(4, version_size - 10);
        bs[0] = straight.at<uint8_t>(4, version_size - 11);
        int v2 = getQRVersion(bs);

        //imwrite("straight.png", straight);
        if (v1 == version || v2 == version)
            return true;
        return false;
    }
    return true;
}

bool QRDecode::decodingProcess()
{
    if (!samplingForVersion())
        return false;
#ifdef HAVE_QUIRC
    if (straight.empty()) { return false; }

    quirc_code qr_code;
    memset(&qr_code, 0, sizeof(qr_code));

    qr_code.size = straight.size().width;
    for (int x = 0; x < qr_code.size; x++)
    {
        for (int y = 0; y < qr_code.size; y++)
        {
            int position = y * qr_code.size + x;
            qr_code.cell_bitmap[position >> 3]
                |= straight.ptr<uint8_t>(y)[x] ? 0 : (1 << (position & 7));
        }
    }

    quirc_data qr_code_data;
    quirc_decode_error_t errorCode = quirc_decode(&qr_code, &qr_code_data);
    if (errorCode != 0) { return false; }

    for (int i = 0; i < qr_code_data.payload_len; i++)
    {
        result_info += qr_code_data.payload[i];
    }
    return true;
#else
    return false;
#endif

}

bool QRDecode::straightDecodingProcess()
{
#ifdef HAVE_QUIRC
    if (!updatePerspective())  { return false; }
    //if (!versionDefinition())  { return false; }
    if (!decodingProcess()) {
        int it = 1;
        const int start_version = version;
        while (it <= 1) {
            version = start_version+it;
            //updatePerspective();
            if (version <= 40 && decodingProcess()) return true;
            version = start_version-it;
            //updatePerspective();
            if (version >=1 && decodingProcess()) return true;
            it++;
        }
    }
    else return true;

    return false;
#else
    std::cout << "Library QUIRC is not linked. No decoding is performed. Take it to the OpenCV repository." << std::endl;
    return false;
#endif
}

bool QRDecode::curvedDecodingProcess()
{
#ifdef HAVE_QUIRC
    if (!preparingCurvedQRCodes()) { return false; }
    if (!versionDefinition())  { return false; }
    if (!decodingProcess())    { return false; }
    return true;
#else
    std::cout << "Library QUIRC is not linked. No decoding is performed. Take it to the OpenCV repository." << std::endl;
    return false;
#endif
}

std::string QRCodeDetector::decode(InputArray in, InputArray points,
                                   OutputArray straight_qrcode)
{
    Mat inarr;
    if (!checkQRInputImage(in, inarr))
        return std::string();

    vector<Point2f> src_points;
    points.copyTo(src_points);
    CV_Assert(src_points.size() == 4);
    CV_CheckGT(contourArea(src_points), 0.0, "Invalid QR code source points");

    QRDecode qrdec;
    qrdec.init(inarr, src_points);
    bool ok = qrdec.straightDecodingProcess();

    std::string decoded_info = qrdec.getDecodeInformation();
    if (!ok && straight_qrcode.needed())
    {
        straight_qrcode.release();
    }
    else if (straight_qrcode.needed())
    {
        qrdec.getStraightBarcode().convertTo(straight_qrcode, CV_8UC1);
    }

    return ok ? decoded_info : std::string();
}

cv::String QRCodeDetector::decodeCurved(InputArray in, InputArray points,
                                        OutputArray straight_qrcode)
{
    Mat inarr;
    if (!checkQRInputImage(in, inarr))
        return std::string();

    vector<Point2f> src_points;
    points.copyTo(src_points);
    CV_Assert(src_points.size() == 4);
    CV_CheckGT(contourArea(src_points), 0.0, "Invalid QR code source points");

    QRDecode qrdec;
    qrdec.init(inarr, src_points);
    bool ok = qrdec.curvedDecodingProcess();

    std::string decoded_info = qrdec.getDecodeInformation();

    if (!ok && straight_qrcode.needed())
    {
        straight_qrcode.release();
    }
    else if (straight_qrcode.needed())
    {
        qrdec.getStraightBarcode().convertTo(straight_qrcode, CV_8UC1);
    }

    return ok ? decoded_info : std::string();
}

std::string QRCodeDetector::detectAndDecode(InputArray in,
                                            OutputArray points_,
                                            OutputArray straight_qrcode)
{
    Mat inarr;
    if (!checkQRInputImage(in, inarr))
    {
        points_.release();
        return std::string();
    }

    vector<Point2f> points;
    bool ok = detect(inarr, points);
    if (!ok)
    {
        points_.release();
        return std::string();
    }
    updatePointsResult(points_, points);
    std::string decoded_info = decode(inarr, points, straight_qrcode);
    return decoded_info;
}

std::string QRCodeDetector::detectAndDecodeCurved(InputArray in,
                                                  OutputArray points_,
                                                  OutputArray straight_qrcode)
{
    Mat inarr;
    if (!checkQRInputImage(in, inarr))
    {
        points_.release();
        return std::string();
    }

    vector<Point2f> points;
    bool ok = detect(inarr, points);
    if (!ok)
    {
        points_.release();
        return std::string();
    }
    updatePointsResult(points_, points);
    std::string decoded_info = decodeCurved(inarr, points, straight_qrcode);
    return decoded_info;
}

class QRDetectMulti : public QRDetect
{
public:
    void init(const Mat& src, double eps_vertical_ = 0.2, double eps_horizontal_ = 0.1);
    bool localization();
    bool computeTransformationPoints(const size_t cur_ind);
    vector< vector < Point2f > > getTransformationPoints() { return transformation_points;}

protected:
    int findNumberLocalizationPoints(vector<Point2f>& tmp_localization_points);
    void findQRCodeContours(vector<Point2f>& tmp_localization_points, vector< vector< Point2f > >& true_points_group, const int& num_qrcodes);
    bool checkSets(vector<vector<Point2f> >& true_points_group, vector<vector<Point2f> >& true_points_group_copy,
                   vector<Point2f>& tmp_localization_points);
    void deleteUsedPoints(vector<vector<Point2f> >& true_points_group, vector<vector<Point2f> >& loc,
                          vector<Point2f>& tmp_localization_points);
    void fixationPoints(vector<Point2f> &local_point);
    bool checkPoints(vector<Point2f> quadrangle_points);
    bool checkPointsInsideQuadrangle(const vector<Point2f>& quadrangle_points);
    bool checkPointsInsideTriangle(const vector<Point2f>& triangle_points);

    Mat bin_barcode_fullsize, bin_barcode_temp;
    vector<Point2f> not_resized_loc_points;
    vector<Point2f> resized_loc_points;
    vector< vector< Point2f > > localization_points, transformation_points;
    struct compareDistanse_y
    {
        bool operator()(const Point2f& a, const Point2f& b) const
        {
            return a.y < b.y;
        }
    };
    struct compareSquare
    {
        const vector<Point2f>& points;
        compareSquare(const vector<Point2f>& points_) : points(points_) {}
        bool operator()(const Vec3i& a, const Vec3i& b) const;
    };
    Mat original;
    class ParallelSearch : public ParallelLoopBody
    {
    public:
        ParallelSearch(vector< vector< Point2f > >& true_points_group_,
                vector< vector< Point2f > >& loc_, int iter_, vector<int>& end_,
                vector< vector< Vec3i > >& all_points_,
                QRDetectMulti& cl_)
        :
            true_points_group(true_points_group_),
            loc(loc_),
            iter(iter_),
            end(end_),
            all_points(all_points_),
            cl(cl_)
        {
        }
        void operator()(const Range& range) const CV_OVERRIDE;
        vector< vector< Point2f > >& true_points_group;
        vector< vector< Point2f > >& loc;
        int iter;
        vector<int>& end;
        vector< vector< Vec3i > >& all_points;
        QRDetectMulti& cl;
    };
};

void QRDetectMulti::ParallelSearch::operator()(const Range& range) const
{
    for (int s = range.start; s < range.end; s++)
    {
        bool flag = false;
        for (int r = iter; r < end[s]; r++)
        {
            if (flag)
                break;

            size_t x = iter + s;
            size_t k = r - iter;
            vector<Point2f> triangle;

            for (int l = 0; l < 3; l++)
            {
                triangle.push_back(true_points_group[s][all_points[s][k][l]]);
            }

            if (cl.checkPointsInsideTriangle(triangle))
            {
                bool flag_for_break = false;
                cl.fixationPoints(triangle);
                if (triangle.size() == 3)
                {
                    cl.localization_points[x] = triangle;
                    if (cl.purpose == cl.SHRINKING)
                    {

                        for (size_t j = 0; j < 3; j++)
                        {
                            cl.localization_points[x][j] *= cl.coeff_expansion;
                        }
                    }
                    else if (cl.purpose == cl.ZOOMING)
                    {
                        for (size_t j = 0; j < 3; j++)
                        {
                            cl.localization_points[x][j] /= cl.coeff_expansion;
                        }
                    }
                    for (size_t i = 0; i < 3; i++)
                    {
                        for (size_t j = i + 1; j < 3; j++)
                        {
                            if (norm(cl.localization_points[x][i] - cl.localization_points[x][j]) < 10)
                            {
                                cl.localization_points[x].clear();
                                flag_for_break = true;
                                break;
                            }
                        }
                        if (flag_for_break)
                            break;
                    }
                    if ((!flag_for_break)
                            && (cl.localization_points[x].size() == 3)
                            && (cl.computeTransformationPoints(x))
                            && (cl.checkPointsInsideQuadrangle(cl.transformation_points[x]))
                            && (cl.checkPoints(cl.transformation_points[x])))
                    {
                        for (int l = 0; l < 3; l++)
                        {
                            loc[s][all_points[s][k][l]].x = -1;
                        }

                        flag = true;
                        break;
                    }
                }
                if (flag)
                {
                    break;
                }
                else
                {
                    cl.transformation_points[x].clear();
                    cl.localization_points[x].clear();
                }
            }
        }
    }
}

void QRDetectMulti::init(const Mat& src, double eps_vertical_, double eps_horizontal_)
{
    CV_TRACE_FUNCTION();

    CV_Assert(!src.empty());
    const double min_side = std::min(src.size().width, src.size().height);
    if (min_side < 512.0)
    {
        purpose = ZOOMING;
        coeff_expansion = 512.0 / min_side;
        const int width  = cvRound(src.size().width  * coeff_expansion);
        const int height = cvRound(src.size().height  * coeff_expansion);
        Size new_size(width, height);
        resize(src, barcode, new_size, 0, 0, INTER_LINEAR_EXACT);
    }
    else if (min_side > 512.0)
    {
        purpose = SHRINKING;
        coeff_expansion = min_side / 512.0;
        const int width  = cvRound(src.size().width  / coeff_expansion);
        const int height = cvRound(src.size().height  / coeff_expansion);
        Size new_size(width, height);
        resize(src, barcode, new_size, 0, 0, INTER_AREA);
    }
    else
    {
        purpose = UNCHANGED;
        coeff_expansion = 1.0;
        barcode = src.clone();
    }

    eps_vertical   = eps_vertical_;
    eps_horizontal = eps_horizontal_;
    adaptiveThreshold(barcode, bin_barcode, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 83, 2);
    adaptiveThreshold(src, bin_barcode_fullsize, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 83, 2);
}

void QRDetectMulti::fixationPoints(vector<Point2f> &local_point)
{
    CV_TRACE_FUNCTION();

    Point2f v0(local_point[1] - local_point[2]);
    Point2f v1(local_point[0] - local_point[2]);
    Point2f v2(local_point[1] - local_point[0]);

    double cos_angles[3], norm_triangl[3];
    norm_triangl[0] = norm(v0);
    norm_triangl[1] = norm(v1);
    norm_triangl[2] = norm(v2);

    cos_angles[0] = v2.dot(-v1) / (norm_triangl[1] * norm_triangl[2]);
    cos_angles[1] = v2.dot(v0) / (norm_triangl[0] * norm_triangl[2]);
    cos_angles[2] = v1.dot(v0) / (norm_triangl[0] * norm_triangl[1]);

    const double angle_barrier = 0.85;
    if (fabs(cos_angles[0]) > angle_barrier || fabs(cos_angles[1]) > angle_barrier || fabs(cos_angles[2]) > angle_barrier)
    {
        local_point.clear();
        return;
    }

    size_t i_min_cos =
            (cos_angles[0] < cos_angles[1] && cos_angles[0] < cos_angles[2]) ? 0 :
                    (cos_angles[1] < cos_angles[0] && cos_angles[1] < cos_angles[2]) ? 1 : 2;

    size_t index_max = 0;
    double max_area = std::numeric_limits<double>::min();
    for (size_t i = 0; i < local_point.size(); i++)
    {
        const size_t current_index = i % 3;
        const size_t left_index  = (i + 1) % 3;
        const size_t right_index = (i + 2) % 3;

        const Point2f current_point(local_point[current_index]);
        const Point2f left_point(local_point[left_index]);
        const Point2f right_point(local_point[right_index]);
        const Point2f central_point(intersectionLines(
                current_point,
                Point2f(static_cast<float>((local_point[left_index].x + local_point[right_index].x) * 0.5),
                        static_cast<float>((local_point[left_index].y + local_point[right_index].y) * 0.5)),
                Point2f(0, static_cast<float>(bin_barcode_temp.rows - 1)),
                Point2f(static_cast<float>(bin_barcode_temp.cols - 1),
                        static_cast<float>(bin_barcode_temp.rows - 1))));

        vector<Point2f> list_area_pnt;
        list_area_pnt.push_back(current_point);

        vector<LineIterator> list_line_iter;
        list_line_iter.push_back(LineIterator(bin_barcode_temp, current_point, left_point));
        list_line_iter.push_back(LineIterator(bin_barcode_temp, current_point, central_point));
        list_line_iter.push_back(LineIterator(bin_barcode_temp, current_point, right_point));

        for (size_t k = 0; k < list_line_iter.size(); k++)
        {
            LineIterator& li = list_line_iter[k];
            uint8_t future_pixel = 255, count_index = 0;
            for (int j = 0; j < li.count; j++, ++li)
            {
                Point p = li.pos();
                if (p.x >= bin_barcode_temp.cols ||
                    p.y >= bin_barcode_temp.rows)
                {
                    break;
                }

                const uint8_t value = bin_barcode_temp.at<uint8_t>(p);
                if (value == future_pixel)
                {
                    future_pixel = static_cast<uint8_t>(~future_pixel);
                    count_index++;
                    if (count_index == 3)
                    {
                        list_area_pnt.push_back(p);
                        break;
                    }
                }
            }
        }

        const double temp_check_area = contourArea(list_area_pnt);
        if (temp_check_area > max_area)
        {
            index_max = current_index;
            max_area = temp_check_area;
        }

    }
    if (index_max == i_min_cos)
    {
        std::swap(local_point[0], local_point[index_max]);
    }
    else
    {
        local_point.clear();
        return;
    }

    const Point2f rpt = local_point[0], bpt = local_point[1], gpt = local_point[2];
    Matx22f m(rpt.x - bpt.x, rpt.y - bpt.y, gpt.x - rpt.x, gpt.y - rpt.y);
    if (determinant(m) > 0)
    {
        std::swap(local_point[1], local_point[2]);
    }
}

class BWCounter
{
    size_t white;
    size_t black;
public:
    BWCounter(size_t b = 0, size_t w = 0) : white(w), black(b) {}
    BWCounter& operator+=(const BWCounter& other) { black += other.black; white += other.white; return *this; }
    void count1(uchar pixel) { if (pixel == 255) white++; else if (pixel == 0) black++; }
    double getBWFraction() const { return white == 0 ? std::numeric_limits<double>::infinity() : double(black) / double(white); }
    static BWCounter checkOnePair(const Point2f& tl, const Point2f& tr, const Point2f& bl, const Point2f& br, const Mat& img)
    {
        BWCounter res;
        LineIterator li1(tl, tr), li2(bl, br);
        for (int i = 0; i < li1.count && i < li2.count; i++, li1++, li2++)
        {
            LineIterator it(img, li1.pos(), li2.pos());
            for (int r = 0; r < it.count; r++, it++)
                res.count1(img.at<uchar>(it.pos()));
        }
        return res;
    }
};

bool QRDetectMulti::checkPoints(vector<Point2f> quadrangle)
{
    if (quadrangle.size() != 4)
        return false;
    std::sort(quadrangle.begin(), quadrangle.end(), compareDistanse_y());
    BWCounter s;
    s += BWCounter::checkOnePair(quadrangle[1], quadrangle[0], quadrangle[2], quadrangle[0], bin_barcode);
    s += BWCounter::checkOnePair(quadrangle[1], quadrangle[3], quadrangle[2], quadrangle[3], bin_barcode);
    const double frac = s.getBWFraction();
    return frac > 0.76 && frac < 1.24;
}

bool QRDetectMulti::checkPointsInsideQuadrangle(const vector<Point2f>& quadrangle_points)
{
    if (quadrangle_points.size() != 4)
        return false;

    int count = 0;
    for (size_t i = 0; i < not_resized_loc_points.size(); i++)
    {
        if (pointPolygonTest(quadrangle_points, not_resized_loc_points[i], true) > 0)
        {
            count++;
        }
    }
    if (count == 3)
        return true;
    else
        return false;
}

bool QRDetectMulti::checkPointsInsideTriangle(const vector<Point2f>& triangle_points)
{
    if (triangle_points.size() != 3)
        return false;
    double eps = 3;
    for (size_t i = 0; i < resized_loc_points.size(); i++)
    {
        if (pointPolygonTest( triangle_points, resized_loc_points[i], true ) > 0)
        {
            if ((abs(resized_loc_points[i].x - triangle_points[0].x) > eps)
                    && (abs(resized_loc_points[i].x - triangle_points[1].x) > eps)
                    && (abs(resized_loc_points[i].x - triangle_points[2].x) > eps))
            {
                return false;
            }
        }
    }
    return true;
}

bool QRDetectMulti::compareSquare::operator()(const Vec3i& a, const Vec3i& b) const
{
    Point2f a0 = points[a[0]];
    Point2f a1 = points[a[1]];
    Point2f a2 = points[a[2]];
    Point2f b0 = points[b[0]];
    Point2f b1 = points[b[1]];
    Point2f b2 = points[b[2]];
    return fabs((a1.x - a0.x) * (a2.y - a0.y) - (a2.x - a0.x) * (a1.y - a0.y)) <
           fabs((b1.x - b0.x) * (b2.y - b0.y) - (b2.x - b0.x) * (b1.y - b0.y));
}

int QRDetectMulti::findNumberLocalizationPoints(vector<Point2f>& tmp_localization_points)
{
    size_t number_possible_purpose = 1;
    if (purpose == SHRINKING)
        number_possible_purpose = 2;
    Mat tmp_shrinking = bin_barcode;
    int tmp_num_points = 0;
    int num_points = -1;
    for (eps_horizontal = 0.1; eps_horizontal < 0.4; eps_horizontal += 0.1)
    {
        tmp_num_points = 0;
        num_points = -1;
        if (purpose == SHRINKING)
            number_possible_purpose = 2;
        else
            number_possible_purpose = 1;
        for (size_t k = 0; k < number_possible_purpose; k++)
        {
            if (k == 1)
                bin_barcode = bin_barcode_fullsize;
            vector<Vec3d> list_lines_x = searchHorizontalLines();
            if (list_lines_x.empty())
            {
                if (k == 0)
                {
                    k = 1;
                    bin_barcode = bin_barcode_fullsize;
                    list_lines_x = searchHorizontalLines();
                    if (list_lines_x.empty())
                        break;
                }
                else
                    break;
            }
            vector<Point2f> list_lines_y = extractVerticalLines(list_lines_x, eps_horizontal);
            if (list_lines_y.size() < 3)
            {
                if (k == 0)
                {
                    k = 1;
                    bin_barcode = bin_barcode_fullsize;
                    list_lines_x = searchHorizontalLines();
                    if (list_lines_x.empty())
                        break;
                    list_lines_y = extractVerticalLines(list_lines_x, eps_horizontal);
                    if (list_lines_y.size() < 3)
                        break;
                }
                else
                    break;
            }
            vector<int> index_list_lines_y;
            for (size_t i = 0; i < list_lines_y.size(); i++)
                index_list_lines_y.push_back(-1);
            num_points = 0;
            for (size_t i = 0; i < list_lines_y.size() - 1; i++)
            {
                for (size_t j = i; j < list_lines_y.size(); j++ )
                {

                    double points_distance = norm(list_lines_y[i] - list_lines_y[j]);
                    if (points_distance <= 10)
                    {
                        if ((index_list_lines_y[i] == -1) && (index_list_lines_y[j] == -1))
                        {
                            index_list_lines_y[i] = num_points;
                            index_list_lines_y[j] = num_points;
                            num_points++;
                        }
                        else if (index_list_lines_y[i] != -1)
                            index_list_lines_y[j] = index_list_lines_y[i];
                        else if (index_list_lines_y[j] != -1)
                            index_list_lines_y[i] = index_list_lines_y[j];
                    }
                }
            }
            for (size_t i = 0; i < index_list_lines_y.size(); i++)
            {
                if (index_list_lines_y[i] == -1)
                {
                    index_list_lines_y[i] = num_points;
                    num_points++;
                }
            }
            if ((tmp_num_points < num_points) && (k == 1))
            {
                purpose = UNCHANGED;
                tmp_num_points = num_points;
                bin_barcode = bin_barcode_fullsize;
                coeff_expansion = 1.0;
            }
            if ((tmp_num_points < num_points) && (k == 0))
            {
                tmp_num_points = num_points;
            }
        }

        if ((tmp_num_points < 3) && (tmp_num_points >= 1))
        {
            const double min_side = std::min(bin_barcode_fullsize.size().width, bin_barcode_fullsize.size().height);
            if (min_side > 512)
            {
                bin_barcode = tmp_shrinking;
                purpose = SHRINKING;
                coeff_expansion = min_side / 512.0;
            }
            if (min_side < 512)
            {
                bin_barcode = tmp_shrinking;
                purpose = ZOOMING;
                coeff_expansion = 512 / min_side;
            }
        }
        else
            break;
    }
    if (purpose == SHRINKING)
        bin_barcode = tmp_shrinking;
    num_points = tmp_num_points;
    vector<Vec3d> list_lines_x = searchHorizontalLines();
    if (list_lines_x.empty())
        return num_points;
    vector<Point2f> list_lines_y = extractVerticalLines(list_lines_x, eps_horizontal);
    if (list_lines_y.size() < 3)
        return num_points;
    if (num_points < 3)
        return num_points;

    Mat labels;
    kmeans(list_lines_y, num_points, labels,
            TermCriteria( TermCriteria::EPS + TermCriteria::COUNT, 10, 0.1),
            num_points, KMEANS_PP_CENTERS, tmp_localization_points);
    bin_barcode_temp = bin_barcode.clone();
    if (purpose == SHRINKING)
    {
        const int width  = cvRound(bin_barcode.size().width  * coeff_expansion);
        const int height = cvRound(bin_barcode.size().height * coeff_expansion);
        Size new_size(width, height);
        Mat intermediate;
        resize(bin_barcode, intermediate, new_size, 0, 0, INTER_LINEAR_EXACT);
        bin_barcode = intermediate.clone();
    }
    else if (purpose == ZOOMING)
    {
        const int width  = cvRound(bin_barcode.size().width  / coeff_expansion);
        const int height = cvRound(bin_barcode.size().height / coeff_expansion);
        Size new_size(width, height);
        Mat intermediate;
        resize(bin_barcode, intermediate, new_size, 0, 0, INTER_LINEAR_EXACT);
        bin_barcode = intermediate.clone();
    }
    else
    {
        bin_barcode = bin_barcode_fullsize.clone();
    }
    return num_points;
}

void QRDetectMulti::findQRCodeContours(vector<Point2f>& tmp_localization_points,
                                      vector< vector< Point2f > >& true_points_group, const int& num_qrcodes)
{
    Mat gray, blur_image, threshold_output;
    Mat bar = barcode;
    const int width  = cvRound(bin_barcode.size().width);
    const int height = cvRound(bin_barcode.size().height);
    Size new_size(width, height);
    resize(bar, bar, new_size, 0, 0, INTER_LINEAR_EXACT);
    blur(bar, blur_image, Size(3, 3));
    threshold(blur_image, threshold_output, 50, 255, THRESH_BINARY);

    vector< vector< Point > > contours;
    vector<Vec4i> hierarchy;
    findContours(threshold_output, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
    vector<Point2f> all_contours_points;
    for (size_t i = 0; i < contours.size(); i++)
    {
        for (size_t j = 0; j < contours[i].size(); j++)
        {
            all_contours_points.push_back(contours[i][j]);
        }
    }
    Mat qrcode_labels;
    vector<Point2f> clustered_localization_points;
    int count_contours = num_qrcodes;
    if (all_contours_points.size() < size_t(num_qrcodes))
        count_contours = (int)all_contours_points.size();
    kmeans(all_contours_points, count_contours, qrcode_labels,
          TermCriteria( TermCriteria::EPS + TermCriteria::COUNT, 10, 0.1),
          count_contours, KMEANS_PP_CENTERS, clustered_localization_points);

    vector< vector< Point2f > > qrcode_clusters(count_contours);
    for (int i = 0; i < count_contours; i++)
        for (int j = 0; j < int(all_contours_points.size()); j++)
        {
            if (qrcode_labels.at<int>(j, 0) == i)
            {
                qrcode_clusters[i].push_back(all_contours_points[j]);
            }
        }
    vector< vector< Point2f > > hull(count_contours);
    for (size_t i = 0; i < qrcode_clusters.size(); i++)
        convexHull(Mat(qrcode_clusters[i]), hull[i]);
    not_resized_loc_points = tmp_localization_points;
    resized_loc_points = tmp_localization_points;
    if (purpose == SHRINKING)
    {
        for (size_t j = 0; j < not_resized_loc_points.size(); j++)
        {
            not_resized_loc_points[j] *= coeff_expansion;
        }
    }
    else if (purpose == ZOOMING)
    {
        for (size_t j = 0; j < not_resized_loc_points.size(); j++)
        {
            not_resized_loc_points[j] /= coeff_expansion;
        }
    }

    true_points_group.resize(hull.size());

    for (size_t j = 0; j < hull.size(); j++)
    {
        for (size_t i = 0; i < not_resized_loc_points.size(); i++)
        {
            if (pointPolygonTest(hull[j], not_resized_loc_points[i], true) > 0)
            {
                true_points_group[j].push_back(tmp_localization_points[i]);
                tmp_localization_points[i].x = -1;
            }

        }
    }
    vector<Point2f> copy;
    for (size_t j = 0; j < tmp_localization_points.size(); j++)
    {
       if (tmp_localization_points[j].x != -1)
            copy.push_back(tmp_localization_points[j]);
    }
    tmp_localization_points = copy;
}

bool QRDetectMulti::checkSets(vector<vector<Point2f> >& true_points_group, vector<vector<Point2f> >& true_points_group_copy,
                              vector<Point2f>& tmp_localization_points)
{
    for (size_t i = 0; i < true_points_group.size(); i++)
    {
        if (true_points_group[i].size() < 3)
        {
            for (size_t j = 0; j < true_points_group[i].size(); j++)
                tmp_localization_points.push_back(true_points_group[i][j]);
            true_points_group[i].clear();
        }
    }
    vector< vector< Point2f > > temp_for_copy;
    for (size_t i = 0; i < true_points_group.size(); i++)
    {
        if (true_points_group[i].size() != 0)
            temp_for_copy.push_back(true_points_group[i]);
    }
    true_points_group = temp_for_copy;
    if (true_points_group.size() == 0)
    {
        true_points_group.push_back(tmp_localization_points);
        tmp_localization_points.clear();
    }
    if (true_points_group.size() == 0)
        return false;
    if (true_points_group[0].size() < 3)
        return false;


    vector<int> set_size(true_points_group.size());
    for (size_t i = 0; i < true_points_group.size(); i++)
    {
        set_size[i] = int( (true_points_group[i].size() - 2 ) * (true_points_group[i].size() - 1) * true_points_group[i].size()) / 6;
    }

    vector< vector< Vec3i > > all_points(true_points_group.size());
    for (size_t i = 0; i < true_points_group.size(); i++)
        all_points[i].resize(set_size[i]);
    int cur_cluster = 0;
    for (size_t i = 0; i < true_points_group.size(); i++)
    {
        cur_cluster = 0;
        for (size_t l = 0; l < true_points_group[i].size() - 2; l++)
            for (size_t j = l + 1; j < true_points_group[i].size() - 1; j++)
                for (size_t k = j + 1; k < true_points_group[i].size(); k++)
                {
                    all_points[i][cur_cluster][0] = int(l);
                    all_points[i][cur_cluster][1] = int(j);
                    all_points[i][cur_cluster][2] = int(k);
                    cur_cluster++;
                }
    }

    for (size_t i = 0; i < true_points_group.size(); i++)
    {
        std::sort(all_points[i].begin(), all_points[i].end(), compareSquare(true_points_group[i]));
    }
    if (true_points_group.size() == 1)
    {
        int check_number = 35;
        if (set_size[0] > check_number)
            set_size[0] = check_number;
        all_points[0].resize(set_size[0]);
    }
    int iter = (int)localization_points.size();
    localization_points.resize(iter + true_points_group.size());
    transformation_points.resize(iter + true_points_group.size());

    true_points_group_copy = true_points_group;
    vector<int> end(true_points_group.size());
    for (size_t i = 0; i < true_points_group.size(); i++)
        end[i] = iter + set_size[i];
    ParallelSearch parallelSearch(true_points_group,
            true_points_group_copy, iter, end, all_points, *this);
    parallel_for_(Range(0, (int)true_points_group.size()), parallelSearch);

    return true;
}

void QRDetectMulti::deleteUsedPoints(vector<vector<Point2f> >& true_points_group, vector<vector<Point2f> >& loc,
                                     vector<Point2f>& tmp_localization_points)
{
    size_t iter = localization_points.size() - true_points_group.size() ;
    for (size_t s = 0; s < true_points_group.size(); s++)
    {
        if (localization_points[iter + s].empty())
            loc[s][0].x = -2;

        if (loc[s].size() == 3)
        {

            if ((true_points_group.size() > 1) || ((true_points_group.size() == 1) && (tmp_localization_points.size() != 0)) )
            {
                for (size_t j = 0; j < true_points_group[s].size(); j++)
                {
                    if (loc[s][j].x != -1)
                    {
                        loc[s][j].x = -1;
                        tmp_localization_points.push_back(true_points_group[s][j]);
                    }
                }
            }
        }
        vector<Point2f> for_copy;
        for (size_t j = 0; j < loc[s].size(); j++)
        {
            if ((loc[s][j].x != -1) && (loc[s][j].x != -2) )
            {
                for_copy.push_back(true_points_group[s][j]);
            }
            if ((loc[s][j].x == -2) && (true_points_group.size() > 1))
            {
                tmp_localization_points.push_back(true_points_group[s][j]);
            }
        }
        true_points_group[s] = for_copy;
    }

    vector< vector< Point2f > > for_copy_loc;
    vector< vector< Point2f > > for_copy_trans;


    for (size_t i = 0; i < localization_points.size(); i++)
    {
        if ((localization_points[i].size() == 3) && (transformation_points[i].size() == 4))
        {
            for_copy_loc.push_back(localization_points[i]);
            for_copy_trans.push_back(transformation_points[i]);
        }
    }
    localization_points = for_copy_loc;
    transformation_points = for_copy_trans;
}

bool QRDetectMulti::localization()
{
    CV_TRACE_FUNCTION();
    vector<Point2f> tmp_localization_points;
    int num_points = findNumberLocalizationPoints(tmp_localization_points);
    if (num_points < 3)
        return false;
    int num_qrcodes = divUp(num_points, 3);
    vector<vector<Point2f> > true_points_group;
    findQRCodeContours(tmp_localization_points, true_points_group, num_qrcodes);
    for (int q = 0; q < num_qrcodes; q++)
    {
       vector<vector<Point2f> > loc;
       size_t iter = localization_points.size();

       if (!checkSets(true_points_group, loc, tmp_localization_points))
            break;
       deleteUsedPoints(true_points_group, loc, tmp_localization_points);
       if ((localization_points.size() - iter) == 1)
           q--;
       if (((localization_points.size() - iter) == 0) && (tmp_localization_points.size() == 0) && (true_points_group.size() == 1) )
            break;
    }
    if ((transformation_points.size() == 0) || (localization_points.size() == 0))
       return false;
    return true;
}

bool static inline checkLine(const Mat& bin_qr, LineIterator lineIt1, LineIterator lineIt2, const int count) {
    Rect tmp(Point(0, 0), bin_qr.size());
    for (int i = 0; i < count && tmp.contains(lineIt1.pos()); i++) {
        const unsigned char pixel = bin_qr.at<unsigned char>(lineIt1.pos());
        if (pixel < 255)
            return false;
        lineIt1++;
    }
    for (int i = 0; i < count && tmp.contains(lineIt2.pos()); i++) {
        const unsigned char pixel = bin_qr.at<unsigned char>(lineIt2.pos());
        if (pixel < 255)
            return false;
        lineIt2++;
    }
    return true;
}

bool QRDetectMulti::computeTransformationPoints(const size_t cur_ind)
{
    CV_TRACE_FUNCTION();

    if (localization_points[cur_ind].size() != 3)
    {
        return false;
    }

    vector<Point> locations, non_zero_elem[3], newHull;
    vector<Point2f> new_non_zero_elem[3];
    for (size_t i = 0; i < 3 ; i++)
    {
        Mat mask = Mat::zeros(bin_barcode.rows + 2, bin_barcode.cols + 2, CV_8UC1);
        uint8_t next_pixel, future_pixel = 255;
        int localization_point_x = cvRound(localization_points[cur_ind][i].x);
        int localization_point_y = cvRound(localization_points[cur_ind][i].y);
        int count_test_lines = 0, index = localization_point_x;
        for (; index < bin_barcode.cols - 1; index++)
        {
            next_pixel = bin_barcode.at<uint8_t>(localization_point_y, index + 1);
            if (next_pixel == future_pixel)
            {
                future_pixel = static_cast<uint8_t>(~future_pixel);
                count_test_lines++;

                if (count_test_lines == 2)
                {
                    // TODO avoid drawing functions
                    floodFill(bin_barcode, mask,
                            Point(index + 1, localization_point_y), 255,
                            0, Scalar(), Scalar(), FLOODFILL_MASK_ONLY);
                    break;
                }
            }

        }
        Mat mask_roi = mask(Range(1, bin_barcode.rows - 1), Range(1, bin_barcode.cols - 1));
        findNonZero(mask_roi, non_zero_elem[i]);
        newHull.insert(newHull.end(), non_zero_elem[i].begin(), non_zero_elem[i].end());
    }
    convexHull(newHull, locations);
    for (size_t i = 0; i < locations.size(); i++)
    {
        for (size_t j = 0; j < 3; j++)
        {
            for (size_t k = 0; k < non_zero_elem[j].size(); k++)
            {
                if (locations[i] == non_zero_elem[j][k])
                {
                    new_non_zero_elem[j].push_back(locations[i]);
                }
            }
        }
    }

    if (new_non_zero_elem[0].size() == 0)
        return false;

    double pentagon_diag_norm = -1;
    Point2f down_left_edge_point, up_right_edge_point, up_left_edge_point;
    for (size_t i = 0; i < new_non_zero_elem[1].size(); i++)
    {
        for (size_t j = 0; j < new_non_zero_elem[2].size(); j++)
        {
            double temp_norm = norm(new_non_zero_elem[1][i] - new_non_zero_elem[2][j]);
            if (temp_norm > pentagon_diag_norm)
            {
                down_left_edge_point = new_non_zero_elem[1][i];
                up_right_edge_point  = new_non_zero_elem[2][j];
                pentagon_diag_norm = temp_norm;
            }
        }
    }

    if (down_left_edge_point == Point2f(0, 0) ||
        up_right_edge_point  == Point2f(0, 0))
    {
        return false;
    }

    double max_area = -1;
    up_left_edge_point = new_non_zero_elem[0][0];

    for (size_t i = 0; i < new_non_zero_elem[0].size(); i++)
    {
        vector<Point2f> list_edge_points;
        list_edge_points.push_back(new_non_zero_elem[0][i]);
        list_edge_points.push_back(down_left_edge_point);
        list_edge_points.push_back(up_right_edge_point);

        double temp_area = fabs(contourArea(list_edge_points));
        if (max_area < temp_area)
        {
            up_left_edge_point = new_non_zero_elem[0][i];
            max_area = temp_area;
        }
    }

    Point2f down_max_delta_point, up_max_delta_point;
    double norm_down_max_delta = -1, norm_up_max_delta = -1;
    for (size_t i = 0; i < new_non_zero_elem[1].size(); i++)
    {
        double temp_norm_delta = norm(up_left_edge_point - new_non_zero_elem[1][i]) + norm(down_left_edge_point - new_non_zero_elem[1][i]);
        if (norm_down_max_delta < temp_norm_delta)
        {
            down_max_delta_point = new_non_zero_elem[1][i];
            norm_down_max_delta = temp_norm_delta;
        }
    }


    for (size_t i = 0; i < new_non_zero_elem[2].size(); i++)
    {
        double temp_norm_delta = norm(up_left_edge_point - new_non_zero_elem[2][i]) + norm(up_right_edge_point - new_non_zero_elem[2][i]);
        if (norm_up_max_delta < temp_norm_delta)
        {
            up_max_delta_point = new_non_zero_elem[2][i];
            norm_up_max_delta = temp_norm_delta;
        }
    }
    vector<Point2f> tmp_transformation_points;
    tmp_transformation_points.push_back(down_left_edge_point);
    tmp_transformation_points.push_back(up_left_edge_point);
    tmp_transformation_points.push_back(up_right_edge_point);
    Point2f down_right_edge_point = intersectionLines(down_left_edge_point, down_max_delta_point,
                                    up_right_edge_point, up_max_delta_point);
    const Point2f offset_x = (down_max_delta_point - down_left_edge_point)/7.f;
    const Point2f offset_y = (up_max_delta_point - up_right_edge_point)/7.f;
    const Point2f offset = offset_x + offset_y;
    int iter = 0;
    int maxIter = 0;//(abs(offset.x) + abs(offset.y))*3.f; // 3 pin
    //const double coeff = .5;
    const int check_dist = maxIter * 4; // 12 pins
    //imwrite("test.png", bin_barcode);
    if (maxIter > 0) {
        LineIterator itLeft(down_right_edge_point, down_left_edge_point);
        LineIterator itTop(down_right_edge_point, up_right_edge_point);
        if (!checkLine(bin_barcode, itLeft, itTop, itLeft.count)) {
            down_right_edge_point += offset_x;
            itLeft = LineIterator(down_right_edge_point, down_left_edge_point);
            itTop = LineIterator(down_right_edge_point, up_right_edge_point);
            if (!checkLine(bin_barcode, itLeft, itTop, itLeft.count)) {
                down_right_edge_point -= offset_x;

                down_right_edge_point += offset_y;
                itLeft = LineIterator(down_right_edge_point, down_left_edge_point);
                itTop = LineIterator(down_right_edge_point, up_right_edge_point);
                if (!checkLine(bin_barcode, itLeft, itTop, itLeft.count)) {
                    down_right_edge_point += offset_x;
                    itLeft = LineIterator(down_right_edge_point, down_left_edge_point);
                    itTop = LineIterator(down_right_edge_point, up_right_edge_point);
                    if (!checkLine(bin_barcode, itLeft, itTop, itLeft.count)) {
                        down_right_edge_point -= offset;
                        maxIter = 0;
                    }
                }
            }
        }
    }
    while (iter < maxIter) {
        LineIterator itLeftNew(down_right_edge_point, down_left_edge_point);
        itLeftNew++;
        itLeftNew++;
        LineIterator itComboNew(itLeftNew.pos(), up_right_edge_point);
        itComboNew++;
        itComboNew++;
        LineIterator itTopNew(down_right_edge_point, up_right_edge_point);
        itTopNew++;
        itTopNew++;
        if (checkLine(bin_barcode,
                      LineIterator(itComboNew.pos(), down_left_edge_point),
                      LineIterator(itComboNew.pos(), up_right_edge_point), check_dist/*itComboNew.count * coeff*/)) {
            down_right_edge_point = itComboNew.pos();
        }
        else if (checkLine(bin_barcode,
                           LineIterator(itLeftNew.pos(), down_left_edge_point),
                           LineIterator(itLeftNew.pos(), up_right_edge_point),
                           check_dist/*itLeftNew.count * coeff*/)) {
            down_right_edge_point = itLeftNew.pos();
        }
        else if (checkLine(bin_barcode,
                           LineIterator(itTopNew.pos(), down_left_edge_point),
                           LineIterator(itTopNew.pos(), up_right_edge_point),
                           check_dist/*itTopNew.count * coeff*/)) {
            down_right_edge_point = itTopNew.pos();
        }
        else {
            break;
        }
        iter++;
    }
    tmp_transformation_points.push_back(down_right_edge_point);
    transformation_points[cur_ind] = tmp_transformation_points;

    vector<Point2f> quadrilateral = getQuadrilateral(transformation_points[cur_ind]);
    transformation_points[cur_ind] = quadrilateral;
    return true;
}

bool QRCodeDetector::detectMulti(InputArray in, OutputArray points) const
{
    Mat inarr;
    if (!checkQRInputImage(in, inarr))
    {
        points.release();
        return false;
    }

    QRDetectMulti qrdet;
    qrdet.init(inarr, p->epsX, p->epsY);
    if (!qrdet.localization())
    {
        points.release();
        return false;
    }
    vector< vector< Point2f > > pnts2f = qrdet.getTransformationPoints();
    vector<Point2f> trans_points;
    for(size_t i = 0; i < pnts2f.size(); i++)
        for(size_t j = 0; j < pnts2f[i].size(); j++)
            trans_points.push_back(pnts2f[i][j]);

    updatePointsResult(points, trans_points);

    return true;
}

class ParallelDecodeProcess : public ParallelLoopBody
{
public:
    ParallelDecodeProcess(Mat& inarr_, vector<QRDecode>& qrdec_, vector<std::string>& decoded_info_,
            vector<Mat>& straight_barcode_, vector< vector< Point2f > >& src_points_)
        : inarr(inarr_), qrdec(qrdec_), decoded_info(decoded_info_)
        , straight_barcode(straight_barcode_), src_points(src_points_)
    {
        // nothing
    }
    void operator()(const Range& range) const CV_OVERRIDE
    {
        for (int i = range.start; i < range.end; i++)
        {
            qrdec[i].init(inarr, src_points[i]);
            bool ok = qrdec[i].straightDecodingProcess();
            if (ok)
            {
                decoded_info[i] = qrdec[i].getDecodeInformation();
                straight_barcode[i] = qrdec[i].getStraightBarcode();
            }
            else if (std::min(inarr.size().width, inarr.size().height) > 512)
            {
                const int min_side = std::min(inarr.size().width, inarr.size().height);
                double coeff_expansion = min_side / 512;
                const int width  = cvRound(inarr.size().width  / coeff_expansion);
                const int height = cvRound(inarr.size().height / coeff_expansion);
                Size new_size(width, height);
                Mat inarr2;
                resize(inarr, inarr2, new_size, 0, 0, INTER_AREA);
                for (size_t j = 0; j < 4; j++)
                {
                    src_points[i][j] /= static_cast<float>(coeff_expansion);
                }
                qrdec[i].init(inarr2, src_points[i]);
                ok = qrdec[i].straightDecodingProcess();
                if (ok)
                {
                    decoded_info[i] = qrdec[i].getDecodeInformation();
                    straight_barcode[i] = qrdec[i].getStraightBarcode();
                }
            }
            if (decoded_info[i].empty())
                decoded_info[i] = "";
        }
    }

private:
    Mat& inarr;
    vector<QRDecode>& qrdec;
    vector<std::string>& decoded_info;
    vector<Mat>& straight_barcode;
    vector< vector< Point2f > >& src_points;

};

bool QRCodeDetector::decodeMulti(
        InputArray img,
        InputArray points,
        CV_OUT std::vector<cv::String>& decoded_info,
        OutputArrayOfArrays straight_qrcode
    ) const
{
    Mat inarr;
    if (!checkQRInputImage(img, inarr))
        return false;
    CV_Assert(points.size().width > 0);
    CV_Assert((points.size().width % 4) == 0);
    vector< vector< Point2f > > src_points ;
    Mat qr_points = points.getMat();
    qr_points = qr_points.reshape(2, 1);
    for (int i = 0; i < qr_points.size().width ; i += 4)
    {
        vector<Point2f> tempMat = qr_points.colRange(i, i + 4);
        if (contourArea(tempMat) > 0.0)
        {
            src_points.push_back(tempMat);
        }
    }
    CV_Assert(src_points.size() > 0);
    vector<QRDecode> qrdec(src_points.size());
    vector<Mat> straight_barcode(src_points.size());
    vector<std::string> info(src_points.size());
    ParallelDecodeProcess parallelDecodeProcess(inarr, qrdec, info, straight_barcode, src_points);
    parallel_for_(Range(0, int(src_points.size())), parallelDecodeProcess);
    vector<Mat> for_copy;
    for (size_t i = 0; i < straight_barcode.size(); i++)
    {
        if (!(straight_barcode[i].empty()))
            for_copy.push_back(straight_barcode[i]);
    }
    straight_barcode = for_copy;
    if (straight_qrcode.needed() && straight_barcode.size() == 0)
    {
        straight_qrcode.release();
    }
    else if (straight_qrcode.needed())
    {
        straight_qrcode.create(Size((int)straight_barcode.size(), 1), CV_8UC1);
        vector<Mat> tmp_straight_qrcodes(straight_barcode.size());
        for (size_t i = 0; i < straight_barcode.size(); i++)
        {
            straight_barcode[i].convertTo(tmp_straight_qrcodes[i], CV_8UC1);
        }
        straight_qrcode.assign(tmp_straight_qrcodes);
    }
    decoded_info.clear();
    for (size_t i = 0; i < info.size(); i++)
    {
       decoded_info.push_back(info[i]);
    }
    if (!decoded_info.empty())
        return true;
    else
        return false;
}

bool QRCodeDetector::detectAndDecodeMulti(
        InputArray img,
        CV_OUT std::vector<cv::String>& decoded_info,
        OutputArray points_,
        OutputArrayOfArrays straight_qrcode
    ) const
{
    Mat inarr;
    if (!checkQRInputImage(img, inarr))
    {
        points_.release();
        return false;
    }

    vector<Point2f> points;
    bool ok = detectMulti(inarr, points);
    if (!ok)
    {
        points_.release();
        return false;
    }
    updatePointsResult(points_, points);
    decoded_info.clear();
    ok = decodeMulti(inarr, points, decoded_info, straight_qrcode);
    return ok;
}

}  // namespace
