# Development container for Faxwerk (Linux build + tests).
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Core toolchain and libraries as required by the project.
# Extra packages:
#   qt6-qpa-plugins, fonts-dejavu-core  - offscreen platform + fonts for QPdfWriter in tests
#   libtiff-tools                       - tiffinfo/tiffdump for debugging rasterizer output
#   autoconf automake libtool ca-certificates
#                                       - building upstream spandsp from GitHub
#   curl                                - downloading linuxdeploy for the AppImage
# libappimageupdate is built below.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git gdb pkg-config \
      qt6-base-dev qt6-pdf-dev libspandsp-dev libtiff-dev \
      qt6-qpa-plugins fonts-dejavu-core libtiff-tools \
      autoconf automake libtool ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

# libappimageupdate for the self-update of the AppImage (FAXWERK_APPIMAGEUPDATE).
# Not packaged by Ubuntu, so it is built from the AppImageUpdate sources into
# /usr/local; its CMake build fetches zsync2, libappimage, cpr and curl at the
# versions pinned by the tag. Unix Makefiles, because libappimage's external
# projects do not build with Ninja.
ARG APPIMAGEUPDATE_TAG=2.0.0-alpha-1-20251018
ARG APPIMAGEUPDATE_COMMIT=a211784dfc746fdb6d8d32d6bb39add451c1ddeb
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libgpgme-dev libcurl4-openssl-dev libssl-dev nlohmann-json3-dev \
      libx11-dev libxpm-dev libfuse-dev libglib2.0-dev libcairo2-dev librsvg2-dev \
      libarchive-dev liblzma-dev libzstd-dev zlib1g-dev libgcrypt20-dev libboost-dev \
 && rm -rf /var/lib/apt/lists/* \
 && git clone --depth 1 --branch "$APPIMAGEUPDATE_TAG" \
      https://github.com/AppImageCommunity/AppImageUpdate /tmp/AppImageUpdate \
 && test "$(git -C /tmp/AppImageUpdate rev-parse HEAD)" = "$APPIMAGEUPDATE_COMMIT" \
 && cmake -S /tmp/AppImageUpdate -B /tmp/AppImageUpdate/build -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DBUILD_LIBAPPIMAGEUPDATE_ONLY=ON \
      -DBUILD_QT_UI=OFF \
 && cmake --build /tmp/AppImageUpdate/build -j"$(nproc)" \
 && cmake --install /tmp/AppImageUpdate/build \
 && ldconfig \
 && rm -rf /tmp/AppImageUpdate

# The image ships the user "ubuntu" (uid 1000); scripts/dev.sh runs with the
# caller's uid/gid instead, so files in the mounted repo keep their owner.
ENV QT_QPA_PLATFORM=offscreen
WORKDIR /workspace
