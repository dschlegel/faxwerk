#!/usr/bin/env bash
# Build and test Faxwerk inside the dev container without VS Code.
#
# Usage: scripts/dev.sh [command] [args...]
#   all        (default) build image, configure, build, run ctest
#   image      build the container image only
#   build      configure + build
#   test       configure + build + ctest
#   shell      interactive shell in the container
#   run ...    run the faxwerk binary with the given arguments
#   upstream   build upstream spandsp from GitHub and compile/test Faxwerk against it
#
# Environment:
#   DOCKER      container engine (default: docker; podman works as well)
#   IMAGE       image tag (default: faxwerk-dev:latest)
#   BUILD_TYPE  CMake build type (default: Debug)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOCKER="${DOCKER:-docker}"
IMAGE="${IMAGE:-faxwerk-dev:latest}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
BUILD_DIR="build/container-${BUILD_TYPE,,}"

build_image() {
    "$DOCKER" build -t "$IMAGE" -f "$REPO_DIR/.devcontainer/Dockerfile" "$REPO_DIR/.devcontainer"
}

ensure_image() {
    if ! "$DOCKER" image inspect "$IMAGE" >/dev/null 2>&1; then
        build_image
    fi
}

# Runs a command in a throw-away container. The repo is mounted at /workspace,
# host networking is used so SIP/RTP can reach the LAN (Linux hosts only).
in_container() {
    local tty_flags=()
    if [ -t 0 ] && [ -t 1 ]; then
        tty_flags=(-it)
    fi
    "$DOCKER" run --rm "${tty_flags[@]}" \
        --network=host \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -e FAXWERK_PASSWORD \
        -v "$REPO_DIR:/workspace" \
        -w /workspace \
        "$IMAGE" "$@"
}

configure_and_build() {
    in_container bash -c "
        set -e
        cmake -S . -B '$BUILD_DIR' -G Ninja -DCMAKE_BUILD_TYPE='$BUILD_TYPE' -DFAXWERK_WERROR=ON
        cmake --build '$BUILD_DIR'
    "
}

run_tests() {
    in_container ctest --test-dir "$BUILD_DIR" --output-on-failure
}

cmd="${1:-all}"
[ $# -gt 0 ] && shift

case "$cmd" in
    all)      build_image; configure_and_build; run_tests ;;
    image)    build_image ;;
    build)    ensure_image; configure_and_build ;;
    test)     ensure_image; configure_and_build; run_tests ;;
    shell)    ensure_image; in_container bash ;;
    run)      ensure_image; in_container "$BUILD_DIR/src/cli/faxwerk" "$@" ;;
    upstream) ensure_image; in_container scripts/build-upstream-spandsp.sh "$@" ;;
    *)        echo "unknown command: $cmd" >&2; sed -n '2,20p' "$0" >&2; exit 2 ;;
esac
