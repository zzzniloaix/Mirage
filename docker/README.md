# The Linux image for eval/run_all.sh

A Docker image that reruns the committed measurements on Linux with `eval/run_all.sh`, with FFmpeg and libvmaf at the versions the measurements were made with (`docs/DECISIONS.md` D-031).

**Not built.** The machine the image was written on, the machine of every committed measurement, has no container runtime (D-031's known limit). The image has not been built and nothing in it has run, so this file says what the Dockerfile asks for, not what a build or a run did. Whether it builds, and whether its run of `eval/run_all.sh` reproduces the committed scores, are MEASUREMENT PENDING; the second is the image's first measurement (D-031).

| File | What it is |
|---|---|
| `Dockerfile` | the image |
| `../.dockerignore` | the build context: the repository's `.git` alone |
| `query_base_image.sh` | asks Docker Hub's registry which image index a tag names |
| `base-image.txt` | its output for the base image's tags, the source of the Dockerfile's digest |

## Building and running it

```
git clone <repository> mirage
docker build -f mirage/docker/Dockerfile -t mirage-eval mirage
mkdir <out>
docker run --rm -v <out>:/out mirage-eval
docker run --rm -v <out>:/out mirage-eval /out --skip-full --jobs 4
```

- **The context is a clone's `.git`.** The image clones the commit checked out in it, with its branch's history, so uncommitted changes and untracked files don't reach the image, and its working tree is clean, as the records of `run_all.sh` check. Build from the top directory of a full clone: a shallow clone lacks the older commits `run_all.sh` builds, and a git worktree's `.git` is a file. Other branches of the context stay in the build's first stage and don't reach the image.
- **The command is `eval/run_all.sh`** with the image's programs: the `mirage_vmaf` it built, and FFmpeg and libvmaf's model files from `/usr/local`, with the libvmaf package version of FFmpeg's side (D-038). Its default output directory is `/out`; `run_all.sh` needs a new or empty directory (`eval/README.md`), so mount an empty one there. Arguments after the image name replace `/out`: the output directory first, then any of `run_all.sh`'s options, such as `--skip-full`, `--only` or `--jobs`.
- **The older programs.** `run_all.sh` builds the `mirage_vmaf` of the runs made before D-016, D-022 and D-028 from their commits (`eval/README.md`). Those commits' `CMakeLists.txt` configure the player as well, so the image has the development files of OpenGL, GLFW and GTK 3, and Python 3 for glad's configure step at those commits; none of it is compiled into `mirage_vmaf`.
- **The unit tests** are built too: `docker run --rm --entrypoint build/linux-release/mirage_tests mirage-eval '~[audio]'` runs them as `docs/BUILD.md` step 4 does, without the `[audio]` tests, for which a container has no sound device.
- **The network.** Building fetches Debian's packages, x264 from code.videolan.org, libvmaf from GitHub, FFmpeg from ffmpeg.org, and CMake's FetchContent dependencies from GitHub. A run fetches the streams from Apple's CDN and, for the older programs, their FetchContent dependencies.

## What is pinned

| What | Pin | Where the pin comes from |
|---|---|---|
| Base image | `debian:trixie-20261020-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad`, an image index: Docker picks the image of the host's platform | `base-image.txt`: the digest the registry gave for the tags `trixie-20261020-slim`, `trixie-slim` and `13-slim`, and the platforms in it |
| FFmpeg | 8.1.1: `https://ffmpeg.org/releases/ffmpeg-8.1.1.tar.xz`, SHA-256 `b6863adde98898f42602017462871b5f6333e65aec803fdd7a6308639c52edf3` | the `url` and `sha256` of the ffmpeg-full 8.1.1 formula as installed on the machine of the measurements (`eval/build_check/macos-2026-10-24-5ae44a4.txt`, "The sources of the installed ffmpeg-full, libvmaf and x264") |
| libvmaf | 3.1.0 (its library reports 3.0.0, D-006): `https://github.com/Netflix/vmaf/archive/refs/tags/v3.1.0.tar.gz`, SHA-256 `80090e29d7fd0db472ddc663513f5be89bc936815e62b767e630c1d627279fe2` | the libvmaf 3.1.0 formula, likewise; ffmpeg-full's libraries link this libvmaf (the same section) |
| x264 | the commit `b35605ace3ddf7c1a5d67a2eb553f034aef41d55` of `https://code.videolan.org/videolan/x264.git`, checked after checkout | the x264 r3222 formula, likewise; Homebrew pins it by that commit, not by a tarball, and ffmpeg-full's libraries link this x264 (the same section) |
| `mirage_vmaf`, `mirage_tests` | the cloned commit, `linux-release` preset (`MIRAGE_HEADLESS`), with the FetchContent dependencies `CMakeLists.txt` pins | the repository |
| Debian's packages | not pinned: what Debian's archive has when the image is built | the image lists them in `/usr/local/share/mirage-image/packages.txt` |

How each is built, against Homebrew's formulae:

- **x264**: the formula's configure options (`--disable-lsmash --disable-swscale --disable-ffms --enable-shared --enable-static --enable-strip`).
- **libvmaf**: `meson setup build libvmaf` with the options Homebrew's `std_meson_args` gives (`--libdir=lib --buildtype=release --wrap-mode=nofallback`), its model directory installed under `share/libvmaf/model` as the formula installs it. The build stops unless libvmaf's configuration has its built-in models, which need `xxd`.
- **FFmpeg**: of the formula's configure options, those of what the evaluation uses: `--enable-shared --enable-pthreads --enable-version3 --enable-gpl --enable-gnutls --enable-libvmaf --enable-libx264`, and `--disable-doc`. The formula's other external libraries, its macOS-only options (VideoToolbox, AudioToolbox) and `ffplay` are left out, so `ffmpeg -version` shows another configuration line than on macOS.
- **The compilers** are Debian's (GCC), not Apple clang.

## Known differences from the macOS measurements

- The libvmaf package version that D-006 records is read from a Homebrew Cellar path (`CMakeLists.txt`, `eval/consistency/run_delta.py`). In the image there is none, so the image gives the release it builds, `LIBVMAF_VERSION`, to the configure (`-DMIRAGE_LIBVMAF_PACKAGE`) and, written out as 3.1.0, to `run_all.sh` (`--ffmpeg-libvmaf-package`) for FFmpeg's side (D-038); the run-time version is reported as on macOS.
- The image runs on the host's architecture; every committed measurement was made on Apple Silicon (arm64, macOS). Whether scores computed on Linux, by another compiler or on another architecture, equal them is what the first run measures.
