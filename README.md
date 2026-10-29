# Faxwerk

Faxwerk sends PDF documents as a fax through a home router that offers IP telephony,
such as an AVM FRITZ!Box. It registers with the router over SIP as an IP phone, dials the
destination and runs the T.30 fax protocol in software (spandsp) over a G.711 audio
stream. You don't need a fax modem, an ISDN card or CAPI.

Windows and macOS are the target platforms. Development and testing currently happen on
Linux in a Docker dev container.

> **Status:** command line tool only. A GUI and T.38 are planned but not part of this
> step. The architecture has a replaceable transport backend for them (see
> [Architecture](#architecture)).

*FRITZ!Box is a trademark of AVM GmbH. Faxwerk is not affiliated with AVM. The product
name is set with the CMake cache variable `FAXWERK_PRODUCT_NAME` (default `Faxwerk`), and
CMake refuses names that contain "FRITZ".*

---

## Contents

- [Features](#features)
- [Building in the dev container (Linux)](#building-in-the-dev-container-linux)
- [Building against upstream spandsp](#building-against-upstream-spandsp)
- [Building on macOS](#building-on-macos)
- [Building on Windows](#building-on-windows)
- [Setting up the FRITZ!Box](#setting-up-the-fritzbox)
- [First practical test](#first-practical-test)
- [Command line reference](#command-line-reference)
- [Configuration file](#configuration-file)
- [Exit codes](#exit-codes)
- [Architecture](#architecture)
- [Tests](#tests)
- [Known limitations and risks](#known-limitations-and-risks)
- [Licensing](#licensing)

## Features

- Its own minimal SIP stack (RFC 3261) on `QUdpSocket`. It does not use PJSIP.
  - REGISTER with digest authentication (MD5, MD5-sess, SHA-256, qop `auth` or none),
    refresh at Expires/2, unregister on exit
  - INVITE with 401/407 handling (ACK, then a new INVITE with CSeq+1), correct ACK rules for
    2xx and non-2xx, retransmission timers A/B and E/F, CANCEL and BYE
  - Handles incoming OPTIONS, BYE, NOTIFY, re-INVITE and UPDATE. T.38 re-INVITEs are declined
    with `488 Not Acceptable Here`, and the fax continues on G.711.
- RTP with G.711 A-law (preferred) or µ-law, 20 ms packets, drift-free pacing, gap concealment
- T.30 via spandsp: ECM (on by default), V.27ter/V.29 up to 9600 bit/s by default, V.17 up to
  14400 bit/s optionally, station ID and page header
- PDF rasterization with QtPdf at 204×196 dpi on 1728-pixel lines (landscape pages are
  rotated), with a threshold for text or Floyd-Steinberg dithering for images
- Supports both spandsp API generations: 0.0.6 (Debian/Ubuntu) and upstream 0.0.7/3.x
  (needed on Windows/macOS)

## Building in the dev container (Linux)

Requirements: Docker (or Podman via `DOCKER=podman`).

```sh
scripts/dev.sh            # build image, configure, build (-Werror), run ctest
scripts/dev.sh test       # configure + build + ctest (image must exist or is built)
scripts/dev.sh shell      # interactive shell in the container
scripts/dev.sh run register-test --user faxwerk620   # run the CLI in the container
```

The script mounts the repository at `/workspace`, runs as your uid/gid and builds into
`build/container-debug`. `BUILD_TYPE=Release scripts/dev.sh` builds a release version.

**VS Code:** open the folder and choose *Reopen in Container*. `.devcontainer/devcontainer.json`
uses the same Dockerfile.

**Networking:** SIP and RTP must reach the router in your LAN, and the router's replies must
reach the container. Both `scripts/dev.sh` and the devcontainer run the container with
`--network=host`. This works on a **Linux** Docker host. Docker Desktop on macOS/Windows
cannot share the LAN this way, so build natively there for real tests (the unit tests work
anywhere).

The image is based on `ubuntu:24.04` with `build-essential cmake ninja-build git gdb
pkg-config qt6-base-dev qt6-pdf-dev libspandsp-dev libtiff-dev`. It also contains
`qt6-qpa-plugins` and `fonts-dejavu-core` (offscreen platform and fonts for `QPdfWriter`
in the tests), `libtiff-tools` for debugging, and the autotools that upstream spandsp needs.

Manual build inside the container:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFAXWERK_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Building against upstream spandsp

Ubuntu ships spandsp 0.0.6. Windows and macOS builds need upstream spandsp
([freeswitch/spandsp](https://github.com/freeswitch/spandsp)), which uses different callback
signatures. CMake detects the API with `check_cxx_source_compiles` (`cmake/SpanDspApi.cmake`)
and defines `FAXWERK_SPANDSP_NEW_API` (and `FAXWERK_SPANDSP_LOG_USER_DATA`).

To build upstream spandsp in the container and then build and test Faxwerk against it:

```sh
scripts/dev.sh upstream            # optional argument: git ref, default master
```

The script clones spandsp to `build/upstream/spandsp`, installs it to
`build/upstream/prefix`, configures Faxwerk with `CMAKE_PREFIX_PATH` pointing there, checks
that the new API was detected and runs ctest.

## Building on macOS

```sh
brew install cmake ninja pkg-config qt libtiff autoconf automake libtool

# spandsp from source (upstream API)
git clone https://github.com/freeswitch/spandsp.git
cd spandsp
./bootstrap.sh
./configure --prefix="$HOME/opt/spandsp" \
    CPPFLAGS="-I$(brew --prefix libtiff)/include" LDFLAGS="-L$(brew --prefix libtiff)/lib"
make -j"$(sysctl -n hw.ncpu)" && make install
cd ..

# Faxwerk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libtiff);$HOME/opt/spandsp"
cmake --build build
ctest --test-dir build --output-on-failure
```

Homebrew's `qt` formula includes the Qt PDF module. spandsp is linked dynamically
(`libspandsp.dylib`), so when you distribute the app bundle, ship it and run
`macdeployqt`.

## Building on Windows

Two options:

**MSYS2 (UCRT64), recommended because spandsp builds there with autotools:**

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libtiff \
    mingw-w64-ucrt-x86_64-qt6-base autoconf automake libtool git
# Qt PDF: install the MSYS2 package that provides Qt6Pdf
# (check with: pacman -Ss qt6 | grep -i pdf)

git clone https://github.com/freeswitch/spandsp.git
cd spandsp && ./bootstrap.sh && ./configure --prefix=/ucrt64/opt/spandsp && make -j && make install && cd ..

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/ucrt64/opt/spandsp
cmake --build build
ctest --test-dir build --output-on-failure
```

**MSVC + vcpkg:** `vcpkg install tiff` provides libtiff. Get Qt including *Qt PDF* from the
Qt online installer or vcpkg. Build spandsp from source first (for example with MSYS2 as
above, or with its Visual Studio project files) and pass its prefix through
`CMAKE_PREFIX_PATH` or `SpanDSP_ROOT`. `FindSpanDSP.cmake` falls back to
`find_path`/`find_library` when pkg-config is missing.

`windeployqt` ships the Qt DLLs. `libspandsp-*.dll` and the libtiff DLL must sit next to
`faxwerk.exe` because both are linked dynamically.

## Setting up the FRITZ!Box

Create an IP phone for Faxwerk in the FRITZ!Box web interface (wording from current
FRITZ!OS versions; older versions differ slightly):

1. **Telefonie → Telefoniegeräte → Neues Gerät einrichten**
2. Choose **Telefon (mit und ohne Anrufbeantworter)**, then **LAN/WLAN (IP-Telefon)**, and give it a
   name such as "Faxwerk".
3. Set a **Benutzername** (user name) and a strong **Kennwort** (password). Faxwerk uses
   these as `--user` and the password.
4. Choose the **outgoing number** for faxes. Incoming calls are not needed: Faxwerk rejects
   them with `486 Busy Here`, so don't assign incoming numbers if the dialog allows it.
5. Finish the wizard. The registrar is `fritz.box` (or the box's IP, for example
   `192.168.178.1`) on port 5060.

## First practical test

1. **Check the registration:**

   ```sh
   export FAXWERK_PASSWORD='the password from step 3'
   faxwerk register-test --server fritz.box --user faxwerk620 --sip-trace register.log
   ```

   A successful run prints `OK: registered as faxwerk620@fritz.box (expires … s, …)`. The
   phone shows up as registered in the FRITZ!Box for a moment and then unregisters. Exit
   code 5 means the user name or password is wrong.

2. **Send a test fax** to a fax machine you control or to a fax test service:

   ```sh
   faxwerk send --server fritz.box --user faxwerk620 \
       --to 0301234567 --pdf letter.pdf \
       --ident "+49 30 1234567" --header "Example Ltd." \
       --sip-trace send.log --verbose
   ```

   `--sip-trace` records every SIP datagram (digest hashes only, never the password).
   `--verbose` adds the T.30 log. If something fails, attach both to the bug report. If the
   transfer is unreliable, try `--no-ecm` or check that the router does not switch the call to
   T.38 (see below).

To check how a PDF will look as a fax without sending it:

```sh
faxwerk render --pdf letter.pdf --out letter.tif [--dither]
```

## Command line reference

```
faxwerk send --to <number> --pdf <file> [--pdf ...]
             [--server fritz.box] [--port 5060] [--domain <domain>]
             [--user <name>] [--auth-user <name>] [--password <secret> | env FAXWERK_PASSWORD]
             [--ident "+49 ..."] [--header "..."] [--max-rate 9600|14400] [--no-ecm]
             [--dither] [--timeout <seconds>] [--local-port N]
             [--sip-trace <file>] [--verbose] [--config <file.ini>]
faxwerk register-test [SIP options as above]
faxwerk render --pdf <file> [--pdf ...] --out <file.tif> [--dither]
```

- Faxwerk takes the password from `--password`, then the `FAXWERK_PASSWORD` environment
  variable, then the INI file. If none of these is set and stdin is a terminal, it asks for
  the password (without echo). `--password` is visible in the process list, so use the
  environment variable. Passwords are never logged.
- `--ident` is the station ID (TSI, at most 20 characters) and `--header` the page header line
  (at most 50 characters).
- `--max-rate 9600` (default) uses V.29/V.27ter, and `14400` also allows V.17. Over IP the
  lower rate is usually more robust.

## Configuration file

The same settings can be stored in an INI file (QSettings). The default path is
`~/.config/Faxwerk/faxwerk.ini` on Linux/macOS and `%APPDATA%\Faxwerk\faxwerk.ini` on
Windows; `--config <file>` picks another one. See
[`docs/faxwerk.ini.example`](docs/faxwerk.ini.example). Command line options override the
file.

## Exit codes

| Code | Meaning |
|-----:|---------|
| 0    | Success |
| 1    | Internal error |
| 2    | Invalid command line or configuration |
| 3    | Document error (PDF unreadable, rendering or TIFF writing failed) |
| 4    | Network error (server not resolvable, port in use, no route) |
| 5    | Authentication failed (wrong SIP user name or password) |
| 6    | Registration failed for another reason (rejected, no answer) |
| 7    | Call failed (busy, number not found, no answer, rejected) |
| 8    | Fax transfer failed (T.30 negotiation or page transfer) |
| 9    | Overall timeout exceeded |
| 130  | Interrupted (Ctrl+C); Faxwerk hangs up and unregisters first |

## Architecture

```
src/core (static library faxwerk_core)
  sip/SipMessage      RFC 3261 parser/serializer
  sip/DigestAuth      digest authentication
  sip/Sdp             SDP offer/answer, T.38 detection
  sip/SipUserAgent    registration, one call, transactions (QUdpSocket)
  rtp/G711, rtp/RtpSession   G.711 codec and RTP endpoint (AudioPort interface)
  fax/FaxEngine       spandsp fax_state in audio mode (implements AudioPort)
  document/PdfRasterizer, document/FaxTiff   PDF -> 1 bit pages -> CCITT G3 TIFF
  transport/FaxTransport       abstract delivery backend
  transport/SipG711Transport   REGISTER -> INVITE -> RTP+FaxEngine -> BYE -> unregister
  FaxJob              rasterize -> transport -> outcome, overall timeout
src/cli               faxwerk executable
tests/                QtTest suites incl. MockFritzBox
```

`FaxTransport` is the extension point: a future `T38Transport` (SIP + UDPTL with spandsp's
`t38_terminal`) or a `CapiTransport` (CAPI over TCP, where the router does the modulation)
can be added and chosen in `FaxJob::createTransport()` without changing the CLI.

## Tests

`ctest` runs these suites. All pass in the container with spandsp 0.0.6 and against
upstream spandsp.

| Suite | What it checks |
|-------|----------------|
| `tst_sipmessage` | parsing, compact forms, folding, list headers, Content-Length, roundtrip, name-addr/URI helpers |
| `tst_digestauth` | RFC 2617 vector (Mufasa → `6629fae4…`), nonce count, no-qop, MD5-sess, RFC 7616 SHA-256 |
| `tst_sdp` | offer (PCMA, PCMU, ptime 20, no telephone-event), answer selection, T.38 detection |
| `tst_g711` | A-law/µ-law roundtrip tolerance over the full 16-bit range |
| `tst_rtpsession` | accelerated pacing, gap concealment, late packets, SSRC change |
| `tst_pdfrasterizer` | 3-page PDF from `QPdfWriter` → 3-page TIFF, 1728 px, 204×196 dpi, landscape rotation |
| `tst_faxloopback` | two FaxEngines back to back: 3 pages with ECM, without ECM, and at V.17 |
| `tst_endtoend` | full `FaxJob` against `MockFritzBox` (401 on REGISTER, 407 on INVITE, independent digest check, ACK rules, duplicate 200 OK, RTP at 2 ms packets, BYE from the mock); T.38 re-INVITE → 488; wrong password; busy |

## Known limitations and risks

- **T.38:** Faxwerk declines T.38 re-INVITEs and stays on G.711. This works as long as the
  router or provider accepts the refusal. Some providers only carry faxes reliably over T.38.
  On those lines, G.711 transfers can fail at higher rates or lengths.
- **Passthrough to the provider:** the mock tests only cover the hop between Faxwerk and the
  router. Whether the FRITZ!Box passes the G.711 stream to the SIP provider unchanged (no
  transcoding to a compressed codec, no echo cancellation or comfort noise in the way) depends
  on the box and the provider. A real test on the target line is essential.
- Only one call at a time. No incoming faxes, no polling, no NAT traversal (STUN/ICE) because
  the router is in the LAN.
- spandsp 0.0.6 has no user data pointer for log callbacks. With that version, T.30 log
  output always goes to the engine that most recently enabled logging.

## Licensing

Faxwerk's own source code is not yet under a license. spandsp is LGPL-2.1 and is **linked
dynamically**. Qt is used under the LGPL (dynamic linking). libtiff uses a BSD-style
license. Faxwerk does not use PJSIP or any other GPL SIP stack; the SIP implementation is
its own.
