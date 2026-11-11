#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
${CXX:-clang++} -std=c++20 -pthread -Isrc $(pkg-config --cflags SDL2_ttf) tests/settings_test.cpp src/settings.cpp $(pkg-config --libs SDL2_ttf) -o "$test_dir/settings_test"
SDL_VIDEODRIVER=dummy "$test_dir/settings_test" "$test_dir"
echo 'Settings persistence, local controllers, rebinding, audio, graphics and public-IP prefill checks passed.'
