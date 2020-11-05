#!/usr/bin/env bash
set -euxo pipefail

mkdir -p build
if [ -z ${AR} ] || [ -z ${RANLIB} ]; then
  echo "Make sure AR and RANLIB are set"
  exit 1
fi
cmake -S . -B build -GNinja -DBUILD_TESTING_ANALYZER=ON -DENABLE_RTLSDR=OFF -DCMAKE_AR=$(which $AR) -DCMAKE_RANLIB=$(which $RANLIB)
cmake --build build

