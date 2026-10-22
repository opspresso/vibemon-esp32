# Host toolchain for native tests and firmware builds. ESP-IDF installs its own
# cross compilers under build/ at run time. Its installer checks OpenOCD, which needs libusb.
FROM python:3.12.15-bookworm@sha256:14a4933e6cef421424f7a66d89a3f4eafcbd0d90e0a822251f298e05132dc735

RUN apt-get update \
  && apt-get install -y --no-install-recommends cmake libusb-1.0-0 ninja-build zip \
  && rm -rf /var/lib/apt/lists/*

ARG TARGETARCH
ARG ARDUINO_CLI_VERSION=1.5.1
RUN case "$TARGETARCH" in \
      amd64) asset=Linux_64bit; sha256=28a8e119c498a25607821c36cb2dc49e8463941b261a0d99091baa7bc692dd2b ;; \
      arm64) asset=Linux_ARM64; sha256=1e69e077479f300614d4551334e0a33f08ee40b04315d83b8e7e0e94f0d0ee62 ;; \
      *) echo "Unsupported architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac \
  && curl -fsSL -o /tmp/arduino-cli.tar.gz \
    "https://downloads.arduino.cc/arduino-cli/arduino-cli_${ARDUINO_CLI_VERSION}_${asset}.tar.gz" \
  && echo "$sha256  /tmp/arduino-cli.tar.gz" | sha256sum -c - \
  && tar -xzf /tmp/arduino-cli.tar.gz -C /usr/local/bin arduino-cli \
  && rm /tmp/arduino-cli.tar.gz
