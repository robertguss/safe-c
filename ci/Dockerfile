# The CI toolchain in one image, so `make ci-docker` runs the same checks as
# .github/workflows/gates.yml on any machine with Docker. Versions match the
# workflow and AGENTS.md; bump them together.
ARG BASE=ubuntu:24.04
FROM ${BASE}

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl git make python3 python3-venv openjdk-17-jre-headless \
      opam build-essential clang-18 clang-tidy-18 libclang-rt-18-dev libfuzzer-18-dev \
      pkg-config libgmp-dev libmpfr-dev graphviz zlib1g-dev libmicrohttpd-dev valgrind unzip bubblewrap \
 && ln -s /usr/bin/clang-18 /usr/local/bin/clang \
 && ln -s /usr/bin/clang-tidy-18 /usr/local/bin/clang-tidy \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/tools \
 && /opt/tools/bin/pip install --no-cache-dir semgrep==1.181.0 z3-solver==4.13.3.0

ENV OPAMROOT=/opt/opam OPAMYES=1 OPAMJOBS=4
RUN opam init --bare --disable-sandboxing \
 && opam switch create safec ocaml-base-compiler.5.2.1 \
 && opam install --switch=safec frama-c.33.0 alt-ergo.2.6.4 why3.1.8.2 \
 && opam clean -a -c -s --logs \
 && PATH=/opt/opam/safec/bin:/opt/tools/bin:$PATH why3 config detect

RUN mkdir -p /opt/tla \
 && curl -fsSL -o /opt/tla/tla2tools.jar https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar \
 && echo "936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88  /opt/tla/tla2tools.jar" | sha256sum -c -

# GenMC (scripts/genmc.py) and tlapm (scripts/tlaps.py). The pins match
# scripts/install-genmc.sh and scripts/install-tlapm.sh; bump them together.
RUN apt-get update -qq \
 && apt-get install -y --no-install-recommends llvm-20-dev clang-20 g++-14 cmake libffi-dev libedit-dev \
 && rm -rf /var/lib/apt/lists/* \
 && git -c advice.detachedHead=false clone --quiet --depth 1 --branch v0.19.0 https://github.com/MPI-SWS/genmc.git /tmp/genmc \
 && test "$(git -C /tmp/genmc rev-parse HEAD)" = 9f6c4c0772d0c42b325681581acc6a0f3eb9b5f7 \
 && CC=gcc-14 CXX=g++-14 cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/lib/llvm-20/cmake \
      -DCMAKE_INSTALL_PREFIX=/opt/genmc -B /tmp/genmc/build -S /tmp/genmc \
 && cmake --build /tmp/genmc/build -j "$(nproc)" && cmake --install /tmp/genmc/build \
 && rm -rf /tmp/genmc
RUN curl -fsSL -o /tmp/tlapm.tar.gz https://github.com/tlaplus/tlapm/releases/download/1.6.0-pre/tlapm-1.6.0-pre-x86_64-linux-gnu.tar.gz \
 && echo "0d1b393a1042aa08961bb024c784a80a25009a5874258c0ddbbfd95b92e80fe5  /tmp/tlapm.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/tlapm.tar.gz -C /opt && rm /tmp/tlapm.tar.gz

# The explorer (examples/explorer): libpq, a Postgres server for the U7 explorer-pq
# work, and SDL3 and SDL3_ttf, which Ubuntu 24.04 does not package. The build context is
# ci/, so this repeats scripts/install-sdl3.sh; the pins match it, bump them together.
RUN apt-get update -qq \
 && apt-get install -y --no-install-recommends libpq-dev postgresql-16 libfreetype-dev libharfbuzz-dev \
      libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxfixes-dev libxtst-dev \
      libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev libgl-dev libdrm-dev libgbm-dev \
 && rm -rf /var/lib/apt/lists/* \
 && curl -fsSL -o /tmp/sdl.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz \
 && echo "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3  /tmp/sdl.tar.gz" | sha256sum -c - \
 && curl -fsSL -o /tmp/ttf.tar.gz https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz \
 && echo "63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d  /tmp/ttf.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/sdl.tar.gz -C /tmp && tar -xzf /tmp/ttf.tar.gz -C /tmp \
 && cmake -S /tmp/SDL3-3.4.18 -B /tmp/build-sdl -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
      -DCMAKE_INSTALL_LIBDIR=lib -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_STATIC=OFF \
 && cmake --build /tmp/build-sdl -j "$(nproc)" && cmake --install /tmp/build-sdl \
 && cmake -S /tmp/SDL3_ttf-3.2.2 -B /tmp/build-ttf -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
      -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH=/opt/sdl3 \
      -DSDLTTF_VENDORED=OFF -DSDLTTF_PLUTOSVG=OFF -DSDLTTF_SAMPLES=OFF \
 && cmake --build /tmp/build-ttf -j "$(nproc)" && cmake --install /tmp/build-ttf \
 && rm -rf /tmp/sdl.tar.gz /tmp/ttf.tar.gz /tmp/SDL3-3.4.18 /tmp/SDL3_ttf-3.2.2 /tmp/build-sdl /tmp/build-ttf

# make ci-docker runs as the caller's uid, so the prover config cannot live in $HOME.
ENV PATH=/opt/opam/safec/bin:/opt/tools/bin:$PATH TLA2TOOLS=/opt/tla/tla2tools.jar \
    GENMC=/opt/genmc/bin/genmc TLAPM=/opt/tlapm/bin/tlapm \
    WHY3CONFIG=/opt/why3.conf HOME=/tmp SEMGREP_ENABLE_VERSION_CHECK=0 \
    PKG_CONFIG_PATH=/opt/sdl3/lib/pkgconfig
RUN why3 config detect && chmod a+r /opt/why3.conf
WORKDIR /src
