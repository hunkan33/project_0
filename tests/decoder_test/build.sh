#!/bin/bash
set -e
cd "$(dirname "$0")"
TC="/opt/atk-dlt113is-toolchain"
rm -rf build && mkdir -p build && cd build
"$TC/bin/qmake" ..
make -j4
cd ..
ls -lh build/decoder_test | awk '{print "  大小: "$5}'
