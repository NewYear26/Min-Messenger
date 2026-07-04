#!/usr/bin/env sh
set -eu

c++ -std=c++20 -Wall -Wextra -Wpedantic -pthread \
  tests/secret_tests.cpp secret.cpp -o tests/secret_tests
timeout 5s tests/secret_tests

# Every production translation unit must at least compile independently.
for source in main.cpp server.cpp client.cpp secret.cpp; do
  c++ -std=c++20 -Wall -Wextra -Wpedantic -pthread -c "$source" \
    -o "/tmp/min-messenger-${source%.cpp}.o"
done

# The configured build is itself a smoke test for all application entry points.
cmake -S . -B /tmp/min-messenger-build
cmake --build /tmp/min-messenger-build
