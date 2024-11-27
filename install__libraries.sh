#!/bin/bash

echo "Starting dependency installation for Fedora..."

# Update system packages
echo "Updating package repository..."
sudo dnf check-update || { echo "Failed to update package repository. Check your internet connection."; exit 1; }

# Install required development tools
echo "Installing development tools and libraries..."
sudo dnf install -y gcc gcc-c++ cmake pkg-config git make automake autoconf || { echo "Failed to install basic development tools."; exit 1; }

# Install ALSA development library
echo "Installing ALSA library..."
sudo dnf install -y alsa-lib alsa-utils alsa-lib-devel || { echo "Failed to install ALSA library."; exit 1; }

# Install OpenCV development libraries and dependencies
echo "Installing OpenCV and dependencies..."
sudo dnf install -y opencv opencv-devel gtk2-devel gtk3-devel ffmpeg ffmpeg-devel libpng-devel libjpeg-turbo-devel libtiff-devel openexr-devel gstreamer1-devel gstreamer1-plugins-base-devel || { echo "Failed to install OpenCV or its dependencies."; exit 1; }

# Install additional networking tools (for socket programming)
echo "Installing networking libraries..."
sudo dnf install -y glibc-devel glibc-static || { echo "Failed to install networking libraries."; exit 1; }

# Install threading support and synchronization (pthreads)
echo "Installing pthreads..."
sudo dnf install -y libpthread-stubs0-devel || { echo "Failed to install pthreads library."; exit 1; }

# Install OpenCV optional dependencies
echo "Installing optional OpenCV dependencies..."
sudo dnf install -y tbb-devel eigen3-devel python3 python3-pip || { echo "Failed to install optional OpenCV dependencies."; exit 1; }

# Install Video4Linux utilities for webcam support
echo "Installing Video4Linux utilities..."
sudo dnf install -y v4l-utils || { echo "Failed to install Video4Linux utilities."; exit 1; }

# Verify ALSA installation
echo "Verifying ALSA installation..."
if pkg-config --modversion alsa > /dev/null 2>&1; then
    echo "ALSA installed successfully. Version: $(pkg-config --modversion alsa)"
else
    echo "ALSA installation verification failed. Please check your setup."
    exit 1
fi

# Verify OpenCV installation
echo "Verifying OpenCV installation..."
if pkg-config --modversion opencv4 > /dev/null 2>&1; then
    echo "OpenCV installed successfully. Version: $(pkg-config --modversion opencv4)"
else
    echo "OpenCV installation verification failed. Please check your setup."
    exit 1
fi

echo "All dependencies installed successfully for Fedora!"
