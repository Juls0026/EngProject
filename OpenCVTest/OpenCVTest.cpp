#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // Open the default webcam (device ID 0)
    cv::VideoCapture cap(0);

    // Check if the webcam is opened successfully
    if (!cap.isOpened()) {
        std::cerr << "Error: Unable to open the webcam\n";
        return -1;
    }

    // Get video properties (optional, for debugging)
    int frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    std::cout << "Frame size: " << frame_width << "x" << frame_height << std::endl;

    // Set up the video writer to save the output (optional)
    cv::VideoWriter writer("output.avi", 
                           cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 
                           30, 
                           cv::Size(frame_width, frame_height));

    if (!writer.isOpened()) {
        std::cerr << "Error: Unable to open video writer\n";
        return -1;
    }

    // Main loop to capture and display video
    cv::Mat frame;
    while (true) {
        // Capture a frame from the webcam
        cap >> frame;

        // Check if the frame is empty (end of stream or error)
        if (frame.empty()) {
            std::cerr << "Error: Empty frame captured\n";
            break;
        }

        // Write the frame to the output file (optional)
        writer.write(frame);

        // Display the frame in a window
        cv::imshow("Webcam - Real-Time Video", frame);

        // Exit the loop if the user presses 'q' or 'Esc'
        char key = static_cast<char>(cv::waitKey(1));
        if (key == 27 || key == 'q') { // Esc or 'q'
            break;
        }
    }

    // Release resources
    cap.release();
    writer.release();
    cv::destroyAllWindows();

    return 0;
}

