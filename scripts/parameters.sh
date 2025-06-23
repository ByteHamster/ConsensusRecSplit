#!/bin/bash
# shellcheck disable=SC2086

hostname
strings Benchmark | grep " -m"

params="-n 100M -numQueries 100M"

./Benchmark -k 256   --overhead 0.5    $params
./Benchmark -k 256   --overhead 0.1    $params
./Benchmark -k 512   --overhead 0.1    $params
./Benchmark -k 512   --overhead 0.06   $params
./Benchmark -k 512   --overhead 0.03   $params
./Benchmark -k 32768 --overhead 0.1    $params
./Benchmark -k 32768 --overhead 0.06   $params
./Benchmark -k 32768 --overhead 0.01   $params
./Benchmark -k 32768 --overhead 0.006  $params
./Benchmark -k 32768 --overhead 0.003  $params
./Benchmark -k 32768 --overhead 0.001  $params
./Benchmark -k 32768 --overhead 0.0005 $params

./Benchmark -k 256   --overhead 0.5    --queryOptimized $params
./Benchmark -k 256   --overhead 0.1    --queryOptimized $params
./Benchmark -k 512   --overhead 0.1    --queryOptimized $params
./Benchmark -k 512   --overhead 0.06   --queryOptimized $params
./Benchmark -k 512   --overhead 0.03   --queryOptimized $params
./Benchmark -k 32768 --overhead 0.1    --queryOptimized $params
./Benchmark -k 32768 --overhead 0.06   --queryOptimized $params
./Benchmark -k 32768 --overhead 0.01   --queryOptimized $params
./Benchmark -k 32768 --overhead 0.006  --queryOptimized $params
./Benchmark -k 32768 --overhead 0.003  --queryOptimized $params
./Benchmark -k 32768 --overhead 0.001  --queryOptimized $params
./Benchmark -k 32768 --overhead 0.0005 --queryOptimized $params
