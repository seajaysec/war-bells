FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
        make ca-certificates xz-utils tar jq && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /work
ENV CROSS_PREFIX=aarch64-linux-gnu-
