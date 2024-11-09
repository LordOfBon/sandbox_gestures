#include "HandDetection.h"
#include "Tools.h"

#include <fstream>

HandDetection::HandDetection()
{
    loadDatabase();
}
HandDetection::~HandDetection()
{
    saveDatabase();
}
void HandDetection::loadDatabase()
{
    std::ifstream file(m_filename);
    if (!file.good())
    {
        return;
    }

    std::string line;
    std::getline(file, line); // Remove labels
    while (std::getline(file, line)) // Read in data
    {
        GestureData g;
        std::stringstream s(line);
        s.imbue(std::locale(","));
        s >> g.areaCB >> g.areaCH >> g.perimeterCH >> g.maxD >> g.minD >> g.averageD >> g.pointsCH >> g.averageA;
        for (int& slice : g.sliceCounts)
        {
            s >> slice;
        }
        s >> g.classLabel;
    }
}

void HandDetection::saveDatabase()
{
    std::ofstream file(m_filename);
    if (!file.good())
    {
        std::cout << "Couldn't write to file " << m_filename << std::endl;
        return;
    }

    // First line: labels
    const std::vector<std::string> names = { "AreaCB", "AreaCH", "PerimeterCH", "MaxD", "MinD", "AverageD", "PointsCH", "AverageA" };
    for (int i = 0; i < names.size(); ++i)
    {
        file << names[i] << ",";
    }
    for (int i = 0; i < 10; ++i)
    {
        file << "slice" << i << ",";
    }
    file << "class\n";

    // Data
    for (auto& g : m_dataset)
    {
        file << g.areaCB << "," << g.areaCH << "," << g.perimeterCH << "," << g.maxD << "," << g.minD << "," << g.averageD << "," << g.pointsCH << "," << g.averageA << "," ;
        for (int& slice : g.sliceCounts)
        {
            file << slice << ",";
        }
        file << g.classLabel << "\n";
    }
}

void HandDetection::transferCurrentData()
{
    for (auto& g : m_currentData)
    {
        m_dataset.push_back(g);
    }
}

void HandDetection::imgui()
{
    if (ImGui::Button("Save Dataset"))
    {
        saveDatabase();
    }
    // Ensure the input image is in the correct format (CV_32F)
    cv::Mat normalized;
    m_segmented.convertTo(normalized, CV_8U, 255.0); // Scale float [0, 1] to [0, 255]

    // Convert to RGB (SFML requires RGB format)
    cv::Mat rgb;
    cv::cvtColor(normalized, rgb, cv::COLOR_GRAY2RGBA);
    std::vector<std::vector<cv::Point>> lines;
    for (size_t i = 0; i < m_hulls.size(); i++)
    {
        cv::Scalar color = cv::Scalar(240, 0, 0, 255);
        if (m_selectedHull == i)
        {
            color = cv::Scalar(0, 250, 0, 255);
        }
        cv::drawContours(rgb, m_hulls, (int)i, color, 2);

        auto m = cv::moments(m_hulls[i]);
        cv::Point p = { (int)(m.m10 / m.m00),(int)(m.m01 / m.m00) };
        double angle = m_currentData[i].averageA;
        lines.push_back({ p, cv::Point(10.0 * sin(angle), 10.0 * cos(angle)) + p });
        cv::drawContours(rgb, lines, i, cv::Scalar(240, 0, 0, 255), 2);
    }

    // Create SFML image
    m_image.create(rgb.cols, rgb.rows, rgb.ptr());

    m_texture.loadFromImage(m_image);
    ImGui::Image(m_texture);
    
    ImGui::SliderInt("Threshold", &m_thresh, 0, 255);

    if (ImGui::CollapsingHeader("Convex Hulls"))
    {
        for (size_t i = 0; i < m_hulls.size(); i++)
        {

            ImGui::Text("Hull %d: ", i);
            ImGui::SameLine();
            if (ImGui::Button(std::format("Select##{}", i).c_str()))
            {
                m_selectedHull = i;
            }
            ImGui::SameLine();
            const static char* classLabels[] = {"None", "High Five", "OK", "Peace", "Rock", "Judgement"};
            ImGui::Combo(std::format("##clslabel{}", i).c_str(), &m_currentData[i].classLabel, classLabels, IM_ARRAYSIZE(classLabels));
        }
    }

    if (ImGui::Button("Add data to dataset"))
    {
        transferCurrentData();
    }
}

// Function that detects the area taken up by hands / arms and ignores it
void HandDetection::removeHands(const cv::Mat & input, cv::Mat & output, float maxDistance, float minDistance)
{
    if (m_previous.total() <= 0) // For the first frame
    {
        m_previous = input.clone();
        output = input;
        return;
    }
    // Normalize
    cv::Mat normalized;
    normalized = 1.f - (input - minDistance) / (maxDistance - minDistance);

    // Binarize
    cv::Mat binarized;
    normalized.convertTo(binarized, CV_8U, 255.0);
    cv::threshold(binarized, m_segmented, m_thresh, 255, cv::THRESH_BINARY);
    cv::Mat mask = m_segmented == 255;
    cv::Mat in = input.clone();
    cv::Mat prev = m_previous.clone();
    in.setTo(0.0, mask);
    prev.setTo(0.0, 1 - mask);
    output = in + prev;
    m_previous = output.clone();
}

void HandDetection::identifyGestures(std::vector<cv::Point> & box)
{
    m_gestures.clear();
    if (m_segmented.total() <= 0)
    {
        return;
    }

    double boxArea = cv::contourArea(box, true);

    // Make mask
    cv::Mat mask = cv::Mat::ones(m_segmented.size(), CV_8U);
    cv::fillConvexPoly(mask, box, cv::Scalar(0));

    m_segmented.setTo(0.0, mask);

    m_contours = std::vector<std::vector<cv::Point>>();
    // Find Contours
    cv::findContours(m_segmented, m_contours, cv::RETR_TREE, cv::CHAIN_APPROX_NONE);

    // Find Convex Hulls
    m_hulls = std::vector<std::vector<cv::Point>>(m_contours.size());

    m_currentData = std::vector<GestureData>(m_contours.size());
    for (size_t i = 0; i < m_contours.size(); i++)
    {
        cv::convexHull(m_contours[i], m_hulls[i]);
        auto m = cv::moments(m_hulls[i]);
        int cx = (int)(m.m10 / m.m00);
        int cy = (int)(m.m01 / m.m00);

        double hullArea = cv::contourArea(m_hulls[i], true);
        double hullPerimeter = cv::arcLength(m_hulls[i], true);
        double contourArea = cv::contourArea(m_contours[i], true);
        double contourPerimeter = cv::arcLength(m_contours[i], true);

        cv::Vec2d normalizedSum;
        std::vector<double> angles (m_contours[i].size());
        auto& g = m_currentData[i];
        for (size_t j = 0; j < m_contours[i].size(); j++)
        {
            cv::Point p = m_contours[i][j];

            cv::Vec2d dif(p.x - cx, p.y - cy);
            angles[j] = atan2(dif[1], dif[0]);
            normalizedSum += cv::normalize(dif);

            double distance = sqrt(pow(cx - (double)p.x, 2) + pow(cy - (double)p.y, 2));
            g.averageD += distance;
            if (j == 0)
            {
                g.maxD = distance;
                g.minD = distance;
                continue;
            }

            if (distance > g.maxD)
            {
                g.maxD = distance;
            }

            if (distance < g.minD)
            {
                g.minD = distance;
            }
        }
        g.averageD /= (double)m_contours[i].size();
        g.averageA = atan2(normalizedSum[0], normalizedSum[1]);

        // Find slice densities
        const int slices = 10;
        double offset = CV_2PI - g.averageA;
        const double sliceSize = CV_2PI / (double)slices;
        for (double a : angles)
        {
            int s = (int)(fmod((a + offset), CV_2PI) / sliceSize);
            g.sliceCounts[s]++;
        }

        g.areaCB = contourArea / boxArea;
        g.areaCH = contourArea / hullArea;
        g.perimeterCH = contourPerimeter / hullPerimeter;
        g.pointsCH = (double)m_hulls[i].size() / (double)m_contours[i].size();
    }
}


