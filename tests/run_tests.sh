#!/usr/bin/env sh
set -u

CXX="${CXX:-c++}"
CXXFLAGS="-std=c++20 -Wall -Wextra -Wpedantic -pthread"
mkdir -p tests/bin
status=0

run() {
  echo
  echo "== $1 =="
  shift
  "$@" || status=1
}

run "Build unit tests" \
  "$CXX" $CXXFLAGS tests/secret_tests.cpp secret.cpp -o tests/bin/secret_tests

if [ -x tests/bin/secret_tests ]; then
  run "Protocol unit tests" tests/bin/secret_tests
fi

run "Build legacy server" \
  "$CXX" $CXXFLAGS main.cpp -o tests/bin/legacy_server
run "Build Secret server" \
  "$CXX" $CXXFLAGS server.cpp secret.cpp -o tests/bin/secret_server
run "Build client" \
  "$CXX" $CXXFLAGS client.cpp secret.cpp -o tests/bin/client

if [ -x tests/bin/legacy_server ] &&
   [ -x tests/bin/secret_server ] &&
   [ -x tests/bin/client ]; then
  run "TCP integration tests" python3 tests/integration_tests.py
fi

for source in main.cpp server.cpp client.cpp secret.cpp; do
  run "Compile $source independently" \
    "$CXX" $CXXFLAGS -c "$source" -o "tests/bin/${source%.cpp}.o"
done

if command -v cmake >/dev/null 2>&1; then
  run "Configured CMake build" \
    sh -c 'cmake -S . -B tests/bin/cmake && cmake --build tests/bin/cmake'
else
  echo "SKIP: cmake is not installed"
fi

exit "$status"
