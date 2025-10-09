#!/bin/bash

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m' 
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Building Violet Engine...${NC}"

# Create build directory
if [ ! -d "build" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir build
fi

cd build

# Configure
echo -e "${YELLOW}Configuring CMake...${NC}"
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DVCPKG_OVERLAY_PORTS=../overlays

# Build
echo -e "${YELLOW}Building project...${NC}"
cmake --build . --config Debug -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Copy compile_commands.json for clangd
if [ -f "compile_commands.json" ]; then
    echo -e "${YELLOW}Copying compile_commands.json for clangd...${NC}"
    cp compile_commands.json ../
fi

echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}Executable: $(pwd)/VioletEngine${NC}"