#!/bin/bash
set -e

cd /tmp
rm -rf cpp_build_dir
mkdir -p cpp_build_dir
cd cpp_build_dir

# Copy project (with timeout protection)
timeout 120 cp -r /mnt/c/Users/Atharv/Downloads/cpp_answer . || true

if [ ! -f cpp_answer/CMakeLists.txt ]; then
    echo "Copy failed, trying alternative..."
    mkdir -p cpp_answer/build
    cd cpp_answer
else
    cd cpp_answer
    mkdir -p build
    cd build
fi

# Configure and build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON ..
make -j4
