FROM i386/debian:stretch

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q && \
    apt-get upgrade -yq && \
    apt-get install -yq --no-install-recommends --force-yes \
    build-essential \
	ca-certificates \
    automake \
	pkg-config

COPY ./src/ /ltp/
COPY ./scripts/syscalls-custom /ltp/runtest/
COPY ./scripts/fs-custom /ltp/runtest/

WORKDIR /ltp

RUN make autotools && \
    ./configure && \
    make -j$(nproc) && \
    make install
