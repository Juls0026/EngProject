cmake_minimum_required(VERSION 3.10)
project(WebRTCAudioStream)

# Set the C++ standard to 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WebRTC and Boost Paths
set(WEBRTC_ROOT "/home/julsruizubago/webrtc/src")  # Set this to the path where WebRTC is located
include_directories(${WEBRTC_ROOT})  # Add WebRTC headers to include path
link_directories(${WEBRTC_ROOT}/out/Default)  # Add WebRTC build output for linking

# Find Boost libraries for WebSocket
find_package(Boost REQUIRED COMPONENTS system thread)  # Requires Boost system and thread components
include_directories(${Boost_INCLUDE_DIRS})  # Add Boost headers to include path

# Define the sources
set(SOURCE_FILES
    src/main.cpp
    signaling/signaling_client.cpp
)

# Add the executable
add_executable(WebRTCAudioStream ${SOURCE_FILES})

# Link WebRTC and Boost Libraries
target_link_libraries(WebRTCAudioStream PRIVATE
    ${WEBRTC_ROOT}/out/Default/obj/libwebrtc.a  # Link WebRTC library
    ${Boost_LIBRARIES}                          # Link Boost libraries
    pthread                                     # Threading support
    rt                                          # Real-time library for POSIX real-time extensions
    dl                                          # Dynamic linking library (used internally by WebRTC)
)
