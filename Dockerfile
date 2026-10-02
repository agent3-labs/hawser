# Builds hawser as one static binary and runs the unit tests on the way.
#
#   docker build --target binary --output dist .     writes dist/hawser
#   docker build --target test -t hawser-test .      the image the network test runs in

FROM alpine:3.20 AS build
RUN apk add --no-cache build-base cmake linux-headers
WORKDIR /src
COPY CMakeLists.txt ./
COPY third_party third_party
COPY src src
COPY tests tests
ARG HAWSER_VERSION=development
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHAWSER_STATIC=ON -DHAWSER_VERSION="$HAWSER_VERSION" \
    && cmake --build build -j \
    && ./build/hawser_tests

# A machine in the network test: the binary and nothing else of ours.
FROM alpine:3.20 AS test
COPY --from=build /src/build/hawser /usr/local/bin/hawser
CMD ["sleep", "infinity"]

# Just the binary, for `--output`.
FROM scratch AS binary
COPY --from=build /src/build/hawser /hawser
