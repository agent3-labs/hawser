# The usual jobs. Each is one command; see the scripts for what they need.

.PHONY: build test network-test package

build:
	cmake -S . -B build && cmake --build build -j

test: build
	./build/hawser_tests

# A real network of four containers.
network-test:
	scripts/network-test.sh

# The downloads for a release, in release/: a tarball and deb, rpm and apk
# packages for each Linux architecture.
package:
	scripts/package.sh
