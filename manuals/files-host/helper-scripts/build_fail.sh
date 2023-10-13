#!/bin/bash

set -eu

FILE=./src/experiments/lockdoc/CMakeList.txt

if [ ! -f "$FILE" ]; then
    echo "This helper script has to be started from the root of the LockDoc/fail repo!"
    exit
fi

sudo apt-get install -yq --no-install-recommends \
        build-essential \
        wget \
        protobuf-compiler libprotobuf-dev \
        libpcl1-dev \
        libboost-thread-dev libboost-system-dev libboost-regex-dev libboost-coroutine-dev libboost-context-dev \
        libdwarf-dev libelf-dev \
        cmake \
        libfontconfig1-dev \
        zlib1g-dev \
        binutils-dev libiberty-dev \
        doxygen \
        xorg-dev \
        libsdl1.2-dev \
        git \
        ca-certificates \
        libmariadb-dev \
        file

cd /tmp
wget -nc https://www.aspectc.org/daily/aspectcpp-linux64-daily.tar.gz
tar -xvf ./aspectcpp-linux64-daily.tar.gz
sudo cp aspectc++/ac++ aspectc++/ag++ /usr/local/bin
sudo chmod +x /usr/local/bin/ac++ /usr/local/bin/ag++

cd -

mkdir -p ./build
cd ./build
rm -rf *

cmake \
    -DBUILD_BOCHS=yes \
    -DBUILD_GEM5=no \
    -DEXPERIMENTS_ACTIVATED=lockdoc \
    -DPLUGINS_ACTIVATED=tracing \
    -DCMAKE_AGPP_FLAGS="-D__constinit= -D__NO_MATH_INLINES" \
    ..

make -j$(nproc)