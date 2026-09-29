#!/usr/bin/env sh
set -eu

cmake -S . -B build/container -G Ninja -DPM_BUILD_TESTS=ON
cmake --build build/container
ctest --test-dir build/container --output-on-failure
