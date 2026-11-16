# The Linux jobs' container (.github/workflows/build.yml, linux-x86_64 and
# linux-arm64): Ubuntu 22.04 (glibc 2.35, so the builds start on older
# distributions too) with the packages they build and test with already
# installed, rather than installed by apt in every job (20-50 s each).
# .github/workflows/ci-image.yml builds it, for each processor on its own
# runner, and publishes it from master as
# ghcr.io/openh1/ci-linux:ubuntu-22.04-<x86_64|arm64> when this folder
# changes.
#
# packages.txt is both jobs', packages-x86_64.txt the x86-64 job's alone
# (its tests'). The jobs check that each is installed and install what is
# not, so a branch that adds one builds with it before master publishes
# the image that has it.
FROM ubuntu:22.04
LABEL org.opencontainers.image.source=https://github.com/OpenH1/goath1
LABEL org.opencontainers.image.description="OpenH1's Linux CI builds: Ubuntu 22.04 with their packages"
ARG ARCH
COPY packages*.txt /tmp/ci-image/
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       $(cat /tmp/ci-image/packages.txt) $(cat "/tmp/ci-image/packages-$ARCH.txt" 2>/dev/null) \
    && rm -rf /var/lib/apt/lists/* /tmp/ci-image
