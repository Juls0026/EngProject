#!/bin/bash

echo "Starting dependency installation for Fedora..."

# Update system packages
echo "Updating package repository..."
sudo dnf check-update || { echo "Failed to update package repository. Check your internet connection."; exit 1; }

# Install required development tools
echo "Installing development tools and libraries..."
sudo dnf install -y gcc gcc-c++ cmake pkg-config git || { echo "Failed to install basic development tools."; exit 1; }

# Install ALSA development library
echo "Installing ALSA library..."
sudo dnf install -y alsa-lib-devel || { echo "Failed to install ALSA library."; exit 1; }

# Install OpenCV development libraries and dependencies
echo "Installing OpenCV and dependencies..."
sudo dnf install -y opencv opencv-devel gtk2-devel gtk3-devel ffmpeg-devel || { echo "Failed to install OpenCV or its dependencies."; exit 1; }

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
