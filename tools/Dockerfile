FROM devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
ENV DEBIAN_FRONTEND=noninteractive
RUN echo 'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] http://deb.debian.org/debian trixie main' > /etc/apt/sources.list.d/trixie.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    clang-19 g++-14 cmake ninja-build python3 nodejs git pkg-config ca-certificates \
    libvulkan-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxinerama-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev \
    libgl-dev libasound2-dev libpulse-dev libudev-dev && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends \
    libx11-xcb-dev libxss-dev libxtst-dev libxfixes-dev libxrender-dev \
    libxcomposite-dev libxdamage-dev libxft-dev libxcb-render0-dev libxcb-shm0-dev \
    wayland-protocols libdecor-0-dev libpipewire-0.3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /project
