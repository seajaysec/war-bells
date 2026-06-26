FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
        libc6-dev-arm64-cross \
        make ca-certificates xz-utils tar jq && \
    rm -rf /var/lib/apt/lists/*
# libc6-dev-arm64-cross is REQUIRED: it provides the target glibc headers (stdio.h, math.h, ...)
# and libm for the aarch64 cross-compile. It is only a *Recommends* of gcc-aarch64-linux-gnu, so
# --no-install-recommends drops it — without it the build fails "stdio.h: No such file or directory".
WORKDIR /work
ENV CROSS_PREFIX=aarch64-linux-gnu-
