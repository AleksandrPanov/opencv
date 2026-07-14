// This file is part of the PanoramaSift sample library.
// Standalone copy of cv::PanoramaMatcher moved into namespace panoram.
#include "precomp.hpp"

#include "panorama_sift/panorama_matcher.hpp"

#include <opencv2/core/hal/hal.hpp>

#include <algorithm>
#include <cfloat>
#include <utility>
#include <vector>

namespace panoram {

using namespace cv;

PanoramaMatcher::CompatY PanoramaMatcher::compatibleDistance(const KeyPoint &next, const KeyPoint &prev, int prevX)
{
    const float dy = next.pt.y - prev.pt.y;
    if (dy >  m_maxShiftY) return CompatY::PrevBelow; // prev.y слишком мал -> двигать нижнюю границу окна
    if (dy < -m_maxShiftY) return CompatY::PrevAbove; // prev.y слишком велик -> дальше только больше по y
    return CompatY::Ok;
}

namespace {

inline int parallelStripeIndex(const cv::Range& wholeRange, const cv::Range& range, int nstripes)
{
    const int len = wholeRange.end - wholeRange.start;
    if (len <= 0 || nstripes <= 0)
        return 0;
    const int offset = range.start - wholeRange.start;
    // Inverse of OpenCV parallel_for stripe mapping (parallel.cpp):
    // r.start = wholeStart + (sr * len + nstripes/2) / nstripes
    for (int i = nstripes - 1; i >= 0; --i)
    {
        const int stripeStart = (int)(((int64_t)i * len + nstripes / 2) / nstripes);
        if (offset >= stripeStart)
            return i;
    }
    return 0;
}

}

bool PanoramaMatcher::compatiblePoints(const KeyPoint &next, const KeyPoint &prev, int prevX)
{
    //int nexOctave = next.octave & 255;
    //int prevOctave = prev.octave & 255;
    bool goodShift = false; // TODO: первое обнаружение ТС даст большой скачок по смещению m_isTracked!!! abs(prevX) <= 2
    if (m_isTracked)
    {
        float nextX = next.pt.x - prev.pt.x;
        const int coef = prevX >= 0 ? 1 : -1;
        const int absPrevX = prevX*coef;
        goodShift = absPrevX*m_minShiftDiff-m_shiftDiff <= nextX*coef && nextX*coef <= absPrevX*m_maxShiftDiff+m_shiftDiff; // TODO: допустить выход за рамки, но штрафовать ?
    }
    else // TODO добавить default ограничения на 3/4 кадра?
    {
        goodShift = true;
    }
    return std::max(next.size, prev.size)*m_sizeDiff < std::min(next.size, prev.size) && goodShift;
}

void PanoramaMatcher::init(Size2i frameSize)
{
    m_frameSize = frameSize;
    m_isTracked = false;

    Size2i numBlockInMask(std::max(1, cvRound(m_frameSize.width / m_backgroundTileSize)), std::max(1, cvRound(m_frameSize.height / m_backgroundTileSize))); //в области ~20x20 подсчитывается background фичи
    m_backMask = Mat::zeros(numBlockInMask, CV_8UC1); // TODO: работа с маской требует обработки при распараллеливании
    m_carMask = Mat::zeros(numBlockInMask, CV_8UC1);
    m_prevDescriptors.release();
    m_prevKeypoints.clear();
}

std::vector<DMatch> PanoramaMatcher::custom_match(InputArray _nextDescriptors, const std::vector<KeyPoint> &keypoints, int prevX, InputArray _mask)
{
    m_badMatches = 0;
    m_backMatches = 0;
    m_duplicateMatches = 0;

    Mat nextD = _nextDescriptors.getMat();
    if(nextD.empty() || m_prevDescriptors.empty())
    {
        m_prevDescriptors = nextD;
        m_prevKeypoints = keypoints;
        return std::vector<DMatch>();
    }

    std::vector<DMatch> matches(nextD.rows);
    std::vector<DMatch> goodMatches;
    goodMatches.reserve(std::max(100, nextD.rows / 10));
    int nvecs = m_prevDescriptors.rows;
    const int descriptorLen = nextD.cols;

    m_backMask = cv::Scalar::all(0);
    m_carMask = cv::Scalar::all(0);
    Size2i sizeBlockInMask(m_frameSize.width / m_backMask.cols + (int)(m_frameSize.width % m_backMask.cols != 0),
                           m_frameSize.height / m_backMask.rows + (int)(m_frameSize.height % m_backMask.rows != 0));
    const cv::Range wholeRange(0, nextD.rows);
    const int nstripes = std::max(1, std::min(cv::getNumThreads(), nextD.rows));
    std::vector<cv::Mat> backMasks(nstripes);
    for (auto& stripeMask : backMasks)
    {
        stripeMask = m_carMask.clone();
    }
    const float backgroundDist = m_backgroundDist*m_backgroundDist;
    // Параллельный цикл по строкам nextD
    cv::parallel_for_(wholeRange, [&](const cv::Range& range) {
        const int stripe_idx = parallelStripeIndex(wholeRange, range, nstripes);
        for (int i = range.start; i < range.end; ++i)
        {
            const float* row1 = nextD.ptr<float>(i);
            bool isBackground = false;

            // Проверяем фичи на принадлежность к фону
            for (int j = 0; j < nvecs; ++j)
            {
                float dist = (keypoints[i].pt.x - m_prevKeypoints[j].pt.x)*(keypoints[i].pt.x - m_prevKeypoints[j].pt.x) +
                             (keypoints[i].pt.y - m_prevKeypoints[j].pt.y)*(keypoints[i].pt.y - m_prevKeypoints[j].pt.y) / 6.f;
                if (dist <= backgroundDist)
                {
                    const float* row2 = m_prevDescriptors.ptr<float>(j);
                    float d = hal::normL2Sqr_(row1, row2, descriptorLen);
                    if (d < m_backgroundThr)
                    {
                        isBackground = true;
                        matches[i].distance = dist;
                        matches[i].queryIdx = j;
                        matches[i].trainIdx = i;
                        matches[i].imgIdx = -2; // TODO: обработать точки с imgIdx==-2 как background
                        break;
                    }
                }
            }
            if (isBackground)
            {
                // найдём координату фичи фона в маске фона
                Point2i pointInd((int)keypoints[i].pt.x / sizeBlockInMask.width, (int)keypoints[i].pt.y/sizeBlockInMask.height);
                backMasks[stripe_idx].at<uint8_t>(pointInd) += uint8_t{1}; // TODO: заменить at на ptr
                continue;
            }
        }
    }, nstripes);

    for (auto& mask : backMasks)
    {
        m_backMask += mask;
    }

    const int K = 2;
    std::vector<std::vector<DMatch>> goodMatchesPerStripe(nstripes);
    std::vector<int> backMatchesPerStripe(nstripes, 0);
    std::vector<int> badMatchesPerStripe(nstripes, 0);
    std::vector<int> duplicateMatchesPerStripe(nstripes, 0);
    const int goodReserve = std::max(10, nextD.rows / std::max(1, 10 * nstripes));
    for (auto& stripeMatches : goodMatchesPerStripe)
        stripeMatches.reserve(goodReserve);

    cv::parallel_for_(wholeRange, [&](const cv::Range& range) {
        const int stripe_idx = parallelStripeIndex(wholeRange, range, nstripes);
        // Нижняя граница y-окна в m_prevKeypoints. Монотонно растёт по i,
        // т.к. keypoints[i] отсортированы по pt.y. Локальная переменная на stripe.
        int lowJ = 0;
        for (int i = range.start; i < range.end; ++i)
        {
            const float* row1 = nextD.ptr<float>(i);
            Point2i pointInd((int)keypoints[i].pt.x/sizeBlockInMask.width, (int)keypoints[i].pt.y/sizeBlockInMask.height);
            bool isBackground = matches[i].imgIdx == -2 || m_backMask.at<uint8_t>(pointInd) > m_maxBackgroundFeatures; // TODO: заменить at на ptr
            if (isBackground)
            {
                backMatchesPerStripe[stripe_idx] += 1;
                matches[i].imgIdx = -2; // TODO: обработать точки с imgIdx==-2 как background
                continue;
            }
            // Временный вектор пар (расстояние, индекс)
            std::vector<std::pair<float, int>> pairs;
            pairs.reserve(nvecs);

            // Продвигаем нижнюю границу окна: точки с малым y не вернутся
            // в окно для будущих i, т.к. next.y не убывает по i.
            // ожидаем что keypoints[i] и m_prevKeypoints[j] отсортированы по pt.y
            while (lowJ < nvecs &&
                   compatibleDistance(keypoints[i], m_prevKeypoints[lowJ], prevX) == CompatY::PrevBelow)
                ++lowJ;

            // Вычисляем L2 расстояние только по точкам внутри y-окна
            for (int j = lowJ; j < nvecs; ++j) {
                CompatY c = compatibleDistance(keypoints[i], m_prevKeypoints[j], prevX);
                if (c == CompatY::PrevAbove)
                    break; // дальше только больше по y
                // c == CompatY::Ok (PrevBelow после продвижения lowJ внутри окна не встретится)
                float d = FLT_MAX;
                if (compatiblePoints(keypoints[i], m_prevKeypoints[j], prevX))
                {
                    const float* row2 = m_prevDescriptors.ptr<float>(j);
                    d = hal::normL2Sqr_(row1, row2, descriptorLen);
                }
                pairs.emplace_back(d, j);
            }

            // Находим K наименьших расстояний
            if (K < (int)pairs.size()) {
                // Частичная сортировка: после nth_element первые K элементов —
                // наименьшие (необязательно отсортированы)
                std::nth_element(pairs.begin(), pairs.begin() + K, pairs.end());
                // Сортируем первые K для упорядоченного вывода
                std::sort(pairs.begin(), pairs.begin() + K);
            } else {
                // кандидатов <= K, нужна полная сортировка
                std::sort(pairs.begin(), pairs.end());
            }

            const float d0 = pairs.empty()     ? FLT_MAX : pairs[0].first;
            const float d1 = pairs.size() < 2  ? FLT_MAX : pairs[1].first;

            if (d0 > m_siftDist)
            {
                badMatchesPerStripe[stripe_idx] += 1;
            }
            else if (d0 >= m_loweCoef*d1)
            {
                duplicateMatchesPerStripe[stripe_idx] += 1;
            }
            else
            {
                cv::DMatch tmp;
                tmp.distance = d0;
                tmp.queryIdx = pairs[0].second;
                tmp.trainIdx = i;
                tmp.imgIdx = keypoints[i].octave & 255;
                goodMatchesPerStripe[stripe_idx].emplace_back(tmp);
            }
        }
    }, nstripes);

    for (int s = 0; s < nstripes; ++s)
    {
        m_backMatches += backMatchesPerStripe[s];
        m_badMatches += badMatchesPerStripe[s];
        m_duplicateMatches += duplicateMatchesPerStripe[s];
        goodMatches.insert(goodMatches.end(),
                           std::make_move_iterator(goodMatchesPerStripe[s].begin()),
                           std::make_move_iterator(goodMatchesPerStripe[s].end()));
    }

    m_prevDescriptors = nextD;
    m_prevKeypoints = keypoints;
    return goodMatches;
}

} // namespace panoram
