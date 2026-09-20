#!/usr/bin/env bash
# builds and runs both implementations, then verifies the output matches
set -u
cd "$(dirname "$0")"
DATA="data/employees.txt"

echo "building go..."
(cd go && go build -o ../build/scheduler_go .) || exit 1
echo "building c++..."
mkdir -p build
g++ -std=c++17 -Wall -Wextra -O2 -pthread -o build/scheduler_cpp cpp/scheduler.cpp || exit 1

./build/scheduler_go  "$DATA" > build/out_go.txt
./build/scheduler_cpp "$DATA" > build/out_cpp.txt

echo
echo "=================== GO OUTPUT ==================="
cat build/out_go.txt
echo
echo "=================== C++ OUTPUT ==================="
cat build/out_cpp.txt
echo
echo "=================== COMPARISON ==================="
if diff -q build/out_go.txt build/out_cpp.txt > /dev/null; then
  echo "PASS: both implementations produced identical output"
  echo "      $(wc -l < build/out_go.txt) lines, $(wc -c < build/out_go.txt) bytes"
else
  echo "FAIL: outputs differ"
  diff -u build/out_go.txt build/out_cpp.txt
  exit 1
fi
