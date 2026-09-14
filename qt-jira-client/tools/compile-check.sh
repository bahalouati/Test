#!/usr/bin/env bash
#
# Generate a test for every function the core headers declare, and compile the
# lot. Nothing is run: the skeletons are meant to fail until somebody fills them
# in. What this proves is that the generator still emits valid C++ against the
# Qt it is pointed at -- which is where the Q_DECLARE_METATYPE guard, the column
# types and the qualified names actually get checked.
#
#   tools/compile-check.sh [qt-major]     # 5 or 6, default 6
set -euo pipefail

QT_MAJOR="${1:-6}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT="$(dirname "$HERE")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for header in "$CLIENT"/src/core/*.h; do
    name="$(basename "$header" .h)"
    python3 "$HERE/qtestgen.py" --header "$header" --all --standalone \
        --class "TestGenerated_$name" -o "$WORK/tst_generated_$name.cpp" 2>/dev/null
done

cat > "$WORK/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.16)
project(QtestgenCompileCheck LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt$QT_MAJOR REQUIRED COMPONENTS Core Network Test)
add_subdirectory($CLIENT jiradesk EXCLUDE_FROM_ALL)
file(GLOB GENERATED "\${CMAKE_CURRENT_SOURCE_DIR}/tst_generated_*.cpp")
foreach(source \${GENERATED})
    get_filename_component(target \${source} NAME_WE)
    add_executable(\${target} \${source})
    target_link_libraries(\${target} PRIVATE jiradesk_core Qt$QT_MAJOR::Test)
endforeach()
CMAKE

cmake -S "$WORK" -B "$WORK/build" \
    -DJIRADESK_QT_VERSION="$QT_MAJOR" -DJIRADESK_BUILD_TESTS=OFF > /dev/null
cmake --build "$WORK/build" -j"$(nproc 2>/dev/null || echo 2)" > "$WORK/build.log" 2>&1 || {
    echo "generated code failed to compile against Qt $QT_MAJOR:" >&2
    grep -E "error:" "$WORK/build.log" >&2 || tail -30 "$WORK/build.log" >&2
    exit 1
}

count=$(grep -h -c "^void TestGenerated_.*::" "$WORK"/tst_generated_*.cpp | paste -sd+ | bc)
echo "qtestgen: $count generated slots compile against Qt $QT_MAJOR"
