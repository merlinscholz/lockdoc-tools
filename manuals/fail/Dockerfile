FROM amd64/debian:bullseye-slim

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q && \
    apt-get upgrade -yq && \
    apt-get install -yq --no-install-recommends \
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

RUN wget https://www.aspectc.org/daily/aspectcpp-linux64-daily.tar.gz && \
    tar --no-same-owner --no-same-permissions -xf aspectcpp-linux64-daily.tar.gz && \
    cp aspectc++/ac++ aspectc++/ag++ /usr/local/bin

COPY ./src/ /fail/

RUN mkdir /fail/build

WORKDIR /fail/build

RUN cmake \
    -DBUILD_BOCHS=yes \
    -DBUILD_GEM5=no \
    -DEXPERIMENTS_ACTIVATED=lockdoc \
    -DPLUGINS_ACTIVATED=tracing .. && \
    make -j$(nproc)