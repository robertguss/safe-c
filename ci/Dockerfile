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

# make ci-docker runs as the caller's uid, so the prover config cannot live in $HOME.
ENV PATH=/opt/opam/safec/bin:/opt/tools/bin:$PATH TLA2TOOLS=/opt/tla/tla2tools.jar \
    WHY3CONFIG=/opt/why3.conf HOME=/tmp SEMGREP_ENABLE_VERSION_CHECK=0
RUN why3 config detect && chmod a+r /opt/why3.conf
WORKDIR /src
