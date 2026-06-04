FROM debian:bookworm-slim

RUN dpkg --add-architecture amd64 && \
    apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        gcc \
        make \
        pkg-config \
        libvlc-dev:amd64 \
        libvlccore-dev:amd64 \
        gcc-mingw-w64-x86-64 \
        wget \
        p7zip-full \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/vlc-win64 && \
    wget -O /tmp/vlc-win64.7z https://get.videolan.org/vlc/last/win64/vlc-3.0.21-win64.7z || \
    wget -O /tmp/vlc-win64.7z https://get.videolan.org/vlc/3.0.21/win64/vlc-3.0.21-win64.7z && \
    7z x /tmp/vlc-win64.7z -o/opt/vlc-win64 '*/*/sdk/*' -r && \
    rm -f /tmp/vlc-win64.7z

COPY build.sh /build.sh

ENTRYPOINT ["/bin/bash", "/build.sh"]
