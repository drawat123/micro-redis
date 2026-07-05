# The Linux development environment for micro-redis.
#
# You're on Apple-Silicon macOS, but this project uses epoll / io_uring / /proc /
# valgrind — all Linux-only. So we build and run INSIDE a Linux container: you
# edit files on your Mac, and compile/run/debug in here. This image has the whole
# toolchain preinstalled and is reproducible (CI can use the exact same image).
#
# Build the image once:   docker build -t microredis-dev .
# Then drop into it:       docker run --rm -it -v "$PWD":/work -p 6380:6380 microredis-dev
#
# (See README.md for the full workflow and docs/TOOLCHAIN.md for what each tool is.)

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang clangd clang-tidy clang-format \
    cmake ninja-build \
    gdb valgrind \
    git ca-certificates \
    redis-tools \
    netcat-openbsd \
    liburing-dev \
 && rm -rf /var/lib/apt/lists/*

# Toolchain notes:
#   build-essential  → gcc/g++/make
#   clang*           → clang compiler + clang-tidy (lint) + clang-format
#   cmake, ninja     → configure + fast parallel builds
#   gdb, valgrind    → debugger + memory checker (both work on arm64 Linux)
#   redis-tools      → redis-cli + redis-benchmark (drive & benchmark the server)
#   netcat-openbsd   → `nc`, handy for poking the rung-0 echo server
#   liburing-dev     → io_uring headers (rung 5)
#
# `perf` is intentionally NOT installed: it needs host-kernel access and is
# unreliable inside Docker Desktop. For the performance pillar (rung 4+), profile
# in a real Linux VM or cloud box instead — see docs/TOOLCHAIN.md §4 & §7.

WORKDIR /work
CMD ["bash"]
