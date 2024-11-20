#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    cv::Mat image = cv::imread("test.jpg");
    if (image.empty()) {
        std::cerr << "Failed to load image.\n";
        return -1;
    }
    cv::imshow("Test Image", image);
    cv::waitKey(0);
    return 0;
}

