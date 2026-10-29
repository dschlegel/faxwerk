# Development container for Faxwerk (Linux build + tests).
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Core toolchain and libraries as required by the project.
# Extra packages:
#   qt6-qpa-plugins, fonts-dejavu-core  - offscreen platform + fonts for QPdfWriter in tests
#   libtiff-tools                       - tiffinfo/tiffdump for debugging rasterizer output
#   autoconf automake libtool ca-certificates
#                                       - building upstream spandsp from GitHub
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git gdb pkg-config \
      qt6-base-dev qt6-pdf-dev libspandsp-dev libtiff-dev \
      qt6-qpa-plugins fonts-dejavu-core libtiff-tools \
      autoconf automake libtool ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# The image ships the user "ubuntu" (uid 1000); scripts/dev.sh runs with the
# caller's uid/gid instead, so files in the mounted repo keep their owner.
ENV QT_QPA_PLATFORM=offscreen
WORKDIR /workspace
