# A Linux image that reruns the committed measurements with eval/run_all.sh
# (docs/DECISIONS.md D-031): FFmpeg 8.1.1 and libvmaf 3.1.0, the versions
# the measurements were made with, built from release sources checked by
# SHA-256, and mirage_vmaf built from this repository with the headless
# configuration. NOT BUILT YET: the machine it was written on has no container
# runtime, so nothing here has been run (docker/README.md).
#
#   docker build -f docker/Dockerfile -t mirage-eval <clone>
#   docker run --rm -v <empty directory>:/out mirage-eval [/out [run_all.sh options]]
#
# <clone> is the top directory of a full clone of the repository. The build
# context is its .git alone (.dockerignore), and the image holds the history
# of the commit checked out there, not its working tree.

# The base image, pinned by the digest Docker Hub's registry gave for its tag
# (docker/base-image.txt).
ARG BASE=debian:trixie-20261020-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad

# ── The repository ───────────────────────────────────────────────────────────

# A clone of the context's HEAD: its branch's history, which eval/run_all.sh
# builds older programs from, and a clean working tree, which its records
# check. Other branches and the context's own files stay in this stage.
FROM ${BASE} AS source
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates git \
 && rm -rf /var/lib/apt/lists/*
COPY .git /context/.git
RUN git clone --quiet --no-local --single-branch /context /src/mirage \
 && git -C /src/mirage log -1 --format='%H %cs %s'

# ── The evaluation environment ───────────────────────────────────────────────

FROM ${BASE}

# From Debian's archive as it is when the image is built (not pinned; the
# versions installed are listed in /usr/local/share/mirage-image/packages.txt):
# - compilers and build tools, for FFmpeg, x264, libvmaf and the CMake build;
# - git and Python 3, which eval/run_all.sh and its scripts need;
# - xxd, without which libvmaf's build leaves its built-in models out;
# - GnuTLS, for FFmpeg's HTTPS to Apple's CDN; zlib and bzip2, which FFmpeg
#   finds on macOS too;
# - the development files of OpenGL, GLFW and GTK 3: the CMakeLists.txt of the
#   commits from before MIRAGE_HEADLESS require them to configure, and
#   eval/run_all.sh builds the mirage_vmaf of three such commits. None of them
#   is compiled into mirage_vmaf.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential ca-certificates cmake curl git make meson nasm ninja-build pkgconf \
      python3 xxd xz-utils \
      libgnutls28-dev zlib1g-dev libbz2-dev \
      libgl-dev libglfw3-dev libgtk-3-dev \
 && rm -rf /var/lib/apt/lists/*

ENV LANG=C.UTF-8

# x264, the H.264 encoder of eval/consistency/color_cases.py's clips: the
# commit of Homebrew's x264 formula r3222, configured as that formula
# configures it.
ARG X264_COMMIT=b35605ace3ddf7c1a5d67a2eb553f034aef41d55
RUN git clone --quiet https://code.videolan.org/videolan/x264.git /tmp/x264 \
 && git -C /tmp/x264 checkout --quiet --detach "${X264_COMMIT}" \
 && test "$(git -C /tmp/x264 rev-parse HEAD)" = "${X264_COMMIT}" \
 && cd /tmp/x264 \
 && ./configure --prefix=/usr/local --disable-lsmash --disable-swscale --disable-ffms \
      --enable-shared --enable-static --enable-strip \
 && make -j"$(nproc)" \
 && make install \
 && ldconfig \
 && rm -rf /tmp/x264

# libvmaf 3.1.0 (its library reports 3.0.0), the release Homebrew's libvmaf
# formula builds, with that formula's meson options and its model files
# installed under share/libvmaf/model.
ARG LIBVMAF_VERSION=3.1.0
ARG LIBVMAF_SHA256=80090e29d7fd0db472ddc663513f5be89bc936815e62b767e630c1d627279fe2
RUN curl -fsSL -o /tmp/vmaf.tar.gz \
      "https://github.com/Netflix/vmaf/archive/refs/tags/v${LIBVMAF_VERSION}.tar.gz" \
 && echo "${LIBVMAF_SHA256}  /tmp/vmaf.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/vmaf.tar.gz -C /tmp \
 && cd "/tmp/vmaf-${LIBVMAF_VERSION}" \
 && meson setup build libvmaf --prefix=/usr/local --libdir=lib --buildtype=release \
      --wrap-mode=nofallback \
 && grep -q '^#define VMAF_BUILT_IN_MODELS 1' build/src/config.h \
 && meson compile -C build \
 && meson install -C build \
 && mkdir -p /usr/local/share/libvmaf \
 && cp -R model /usr/local/share/libvmaf/model \
 && ldconfig \
 && rm -rf /tmp/vmaf.tar.gz "/tmp/vmaf-${LIBVMAF_VERSION}"

# FFmpeg 8.1.1, the release Homebrew's ffmpeg-full formula builds, with that
# formula's configure options for what the evaluation uses: GPL version 3,
# GnuTLS, libvmaf and libx264. Its other external libraries are left out.
ARG FFMPEG_VERSION=8.1.1
ARG FFMPEG_SHA256=b6863adde98898f42602017462871b5f6333e65aec803fdd7a6308639c52edf3
RUN curl -fsSL -o /tmp/ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" \
 && echo "${FFMPEG_SHA256}  /tmp/ffmpeg.tar.xz" | sha256sum -c - \
 && tar -xJf /tmp/ffmpeg.tar.xz -C /tmp \
 && cd "/tmp/ffmpeg-${FFMPEG_VERSION}" \
 && ./configure --prefix=/usr/local --enable-shared --enable-pthreads --enable-version3 \
      --enable-gpl --enable-gnutls --enable-libvmaf --enable-libx264 --disable-doc \
 && make -j"$(nproc)" \
 && make install \
 && ldconfig \
 && rm -rf /tmp/ffmpeg.tar.xz "/tmp/ffmpeg-${FFMPEG_VERSION}"

# mirage_vmaf and mirage_tests from the repository, headless (the
# linux-release preset); CMake fetches miniaudio, pugixml and Catch2.
COPY --from=source /src/mirage /src/mirage
WORKDIR /src/mirage
RUN cmake --preset linux-release \
 && cmake --build --preset linux-release

RUN mkdir -p /usr/local/share/mirage-image \
 && dpkg-query -W -f '${Package} ${Version} ${Architecture}\n' \
      > /usr/local/share/mirage-image/packages.txt

# eval/run_all.sh with this image's programs: the repository's mirage_vmaf
# built above, and FFmpeg and libvmaf's models from /usr/local. It builds the
# programs of the runs made before D-016, D-022 and D-028 from their commits
# itself. The default output directory is /out, which needs to be an empty
# directory mounted from the host; further arguments replace it.
ENTRYPOINT ["eval/run_all.sh", "--mirage-vmaf", "build/linux-release/mirage_vmaf", \
            "--ffmpeg", "/usr/local/bin/ffmpeg", "--model-dir", "/usr/local/share/libvmaf/model"]
CMD ["/out"]
