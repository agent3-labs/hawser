#!/bin/sh
# Makes the downloads for one release, in release/: for each Linux architecture
# a tarball with the static binary, the service unit and install.sh, and a deb,
# an rpm and an apk; then a SHA256SUMS over all of them.
#
#   make package
#
# Each architecture is built by the Dockerfile on its own Alpine image, so the
# unit tests run on every one. The other architectures run under QEMU, which
# Docker needs set up once:
#   docker run --privileged --rm tonistiigi/binfmt --install arm64,arm
# The packages need nfpm: go install github.com/goreleaser/nfpm/v2/cmd/nfpm@latest
set -eu
cd "$(dirname "$0")/.."

command -v nfpm >/dev/null || { echo "package: nfpm is not installed"; exit 1; }
version="$(cat VERSION)"
out=release
rm -rf "$out"
mkdir -p "$out"

# Docker's platform name, the label in file names, and nfpm's name for the architecture.
for arch in "linux/amd64 linux-x64 amd64" "linux/arm64 linux-arm64 arm64" "linux/arm/v7 linux-armv7 arm7"; do
  set -- $arch
  platform=$1 label=$2 nfpm_arch=$3
  echo "== $label"

  # nfpm reads the binary from this one path, whatever the architecture.
  docker build --platform "$platform" --target binary \
    --build-arg HAWSER_VERSION="v$version" --output "$out/stage" .
  binary="$out/stage/hawser"

  tarball="hawser-v$version-$label"
  mkdir "$out/$tarball"
  cp "$binary" packaging/hawser.service scripts/install.sh "$out/$tarball"
  tar -C "$out" -czf "$out/$tarball.tar.gz" "$tarball"
  rm -r "$out/$tarball"

  for packager in deb rpm apk; do
    HAWSER_VERSION="$version" HAWSER_ARCH="$nfpm_arch" \
      nfpm package --config packaging/nfpm.yaml --packager "$packager" --target "$out/"
  done
  rm -r "$out/stage"
done

(cd "$out" && sha256sum -- * > SHA256SUMS)
echo
ls -l "$out"
