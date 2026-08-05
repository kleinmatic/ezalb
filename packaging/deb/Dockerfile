FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential pkg-config libsdl2-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
