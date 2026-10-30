#!/usr/bin/env bash
# A local Windows 11 VM for building and trying out the Windows version.
#
# The VM runs in Docker with dockur/windows (QEMU/KVM inside a container).
# The first start downloads Windows from Microsoft and installs it unattended,
# then scripts/windows-vm/setup.ps1 adds OpenSSH, MSYS2 with the build
# packages and Inno Setup. That takes 30 to 60 minutes; later starts take
# about a minute. Windows runs unactivated, which is fine for building and
# testing.
#
# Usage: scripts/windows-vm.sh <command>
#   up       create or start the VM and wait until it accepts builds
#   build    build the working tree (also uncommitted changes) in the VM with
#            scripts/package-windows.sh; the installer lands in out/windows/
#   shell    MSYS2 UCRT64 shell in the VM
#   status   container state and setup progress
#   stop     shut the VM down
#
# The screen of the VM is at http://127.0.0.1:8006 (user faxwerk, password
# faxwerk); install and try the installer there.
#
# Environment:
#   FAXWERK_WINVM_DIR  disk, SSH key and setup files
#                      (default: ~/.local/share/faxwerk-windows-vm)
#   WINVM_RAM, WINVM_CPUS, WINVM_DISK  VM size (default: 8G, 8, 96G)
#   DOCKER             container engine (default: docker)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VM_DIR="${FAXWERK_WINVM_DIR:-$HOME/.local/share/faxwerk-windows-vm}"
DOCKER="${DOCKER:-docker}"
NAME=faxwerk-windows
IMAGE=docker.io/dockurr/windows:latest
SSH_PORT=2222
WEB_PORT=8006
USER_NAME=faxwerk
KEY="$VM_DIR/ssh/id_ed25519"
COMMON_OPTS=(-i "$KEY" -o BatchMode=yes -o ConnectTimeout=5
             -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
SSH_OPTS=("${COMMON_OPTS[@]}" -p "$SSH_PORT")
SCP_OPTS=("${COMMON_OPTS[@]}" -P "$SSH_PORT")
# Runs a script from stdin in the VM's MSYS2 UCRT64 environment.
MSYS_BASH='C:\msys64\usr\bin\env.exe MSYSTEM=UCRT64 C:\msys64\usr\bin\bash.exe -l -s'

vm_ssh() { ssh "${SSH_OPTS[@]}" "$USER_NAME@127.0.0.1" "$@"; }

container_state() {
    local state
    state="$("$DOCKER" inspect -f '{{.State.Status}}' "$NAME" 2>/dev/null)" || state=missing
    echo "${state:-missing}"
}

ready() { vm_ssh 'if exist C:\OEM\ready (exit 0) else (exit 1)' >/dev/null 2>&1; }

cmd_up() {
    mkdir -p "$VM_DIR/storage" "$VM_DIR/oem" "$VM_DIR/ssh"
    # Copy-on-write on btrfs slows the disk image down and can break Windows
    # Setup; only affects files created afterwards.
    chattr +C "$VM_DIR/storage" 2>/dev/null || true
    if [ ! -f "$KEY" ]; then
        ssh-keygen -q -t ed25519 -N '' -C faxwerk-windows-vm -f "$KEY"
    fi
    cp "$REPO_DIR/scripts/windows-vm/install.bat" "$REPO_DIR/scripts/windows-vm/setup.ps1" "$VM_DIR/oem/"
    cp "$KEY.pub" "$VM_DIR/oem/authorized_keys"

    case "$(container_state)" in
        missing)
            "$DOCKER" run -d --name "$NAME" \
                --device=/dev/kvm --device=/dev/net/tun --cap-add NET_ADMIN \
                -p "127.0.0.1:$WEB_PORT:8006" -p "127.0.0.1:$SSH_PORT:22" \
                -e VERSION=11 \
                -e RAM_SIZE="${WINVM_RAM:-8G}" -e CPU_CORES="${WINVM_CPUS:-8}" -e DISK_SIZE="${WINVM_DISK:-96G}" \
                -e USERNAME="$USER_NAME" -e PASSWORD="$USER_NAME" \
                -v "$VM_DIR/storage:/storage" -v "$VM_DIR/oem:/oem" \
                --stop-timeout 120 \
                "$IMAGE" >/dev/null
            echo "VM created. Windows is being downloaded and installed; watch it at http://127.0.0.1:$WEB_PORT"
            ;;
        running) ;;
        *) "$DOCKER" start "$NAME" >/dev/null ;;
    esac

    echo -n "Waiting until the VM is ready"
    until ready; do
        if [ "$(container_state)" != running ]; then
            echo; echo "error: the container stopped; see: $DOCKER logs $NAME" >&2
            exit 1
        fi
        echo -n .
        sleep 30
    done
    echo; echo "VM ready (ssh port $SSH_PORT, screen http://127.0.0.1:$WEB_PORT)"
}

cmd_build() {
    if ! ready; then
        echo "error: VM not ready; run: scripts/windows-vm.sh up" >&2
        exit 1
    fi
    # Snapshot of the working tree as a commit, including uncommitted and new
    # files (not ignored ones), built in a temporary index.
    local rev tree version bundle="$VM_DIR/faxwerk.bundle" index="$VM_DIR/snapshot.index"
    rm -f "$index"
    GIT_INDEX_FILE="$index" git -C "$REPO_DIR" read-tree HEAD
    GIT_INDEX_FILE="$index" git -C "$REPO_DIR" add -A
    tree="$(GIT_INDEX_FILE="$index" git -C "$REPO_DIR" write-tree)"
    rm -f "$index"
    rev="$(git -C "$REPO_DIR" commit-tree "$tree" -p HEAD -m 'windows-vm build snapshot')"
    version="$(git -C "$REPO_DIR" describe --tags --always --dirty)"
    git -C "$REPO_DIR" update-ref refs/faxwerk-winvm/build "$rev"
    git -C "$REPO_DIR" bundle create -q "$bundle" refs/faxwerk-winvm/build
    git -C "$REPO_DIR" update-ref -d refs/faxwerk-winvm/build
    scp -q "${SCP_OPTS[@]}" "$bundle" "$USER_NAME@127.0.0.1:faxwerk.bundle"

    vm_ssh "$MSYS_BASH" <<EOF
set -euo pipefail
mkdir -p /c/faxwerk && cd /c/faxwerk
[ -d .git ] || git init -q
git fetch -q "/c/Users/$USER_NAME/faxwerk.bundle" refs/faxwerk-winvm/build
git checkout -q -f FETCH_HEAD
git clean -fdxq -e build
FAXWERK_VERSION='${version#v}' SKIP_TESTS='${SKIP_TESTS:-0}' scripts/package-windows.sh
EOF

    mkdir -p "$REPO_DIR/out/windows"
    vm_ssh "$MSYS_BASH" <<<'cd /c/faxwerk/out && tar cf - *.exe' | tar xf - -C "$REPO_DIR/out/windows"
    ls -l "$REPO_DIR/out/windows"
}

cmd_shell() {
    ssh -t "${SSH_OPTS[@]}" "$USER_NAME@127.0.0.1" \
        'C:\msys64\usr\bin\env.exe MSYSTEM=UCRT64 C:\msys64\usr\bin\bash.exe -l'
}

cmd_status() {
    echo "container: $(container_state)"
    if vm_ssh 'type C:\OEM\setup.log' 2>/dev/null | grep '^== ' ; then :; else echo "setup: not reachable yet"; fi
}

cmd_stop() {
    "$DOCKER" stop "$NAME" >/dev/null && echo "VM stopped"
}

case "${1:-}" in
    up) cmd_up ;;
    build) cmd_build ;;
    shell) cmd_shell ;;
    status) cmd_status ;;
    stop) cmd_stop ;;
    *) sed -n '2,30p' "$0" >&2; exit 2 ;;
esac
