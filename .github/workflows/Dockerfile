FROM ubuntu:20.04 as builder

RUN apt-get update && \
    DEBIAN_FRONTEND="noninteractive" apt-get install --no-install-recommends -y \
        build-essential wget git \
        libreadline-dev libboost-coroutine-dev libboost-context-dev \
        libboost-serialization-dev libboost-filesystem-dev libssl-dev libc-ares-dev zlib1g-dev \
        ca-certificates automake libtool patchelf cmake pkg-config liblua5.3-dev lua-socket && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/emulator
COPY . .

RUN make -j$(nproc) dep && \
    make -j$(nproc) && \
    make install && \
    make clean && \
    rm -rf *

FROM ubuntu:20.04

RUN apt-get update && DEBIAN_FRONTEND="noninteractive" apt-get install -y \
    libboost-coroutine1.71.0 \
    libboost-context1.71.0 \
    libboost-serialization1.71.0 \
    libboost-filesystem1.71.0 \
    libreadline8 \
    openssl \
    libc-ares2 \
    zlib1g \
    ca-certificates \
    libgomp1 \
    lua5.3 \
    lua-socket \
    genext2fs \
    && rm -rf /var/lib/apt/lists/*

ENV PATH="/opt/cartesi/bin:${PATH}"
WORKDIR /opt/cartesi
COPY --from=builder /opt/cartesi .

CMD [ "/opt/cartesi/bin/remote-cartesi-machine" ]
