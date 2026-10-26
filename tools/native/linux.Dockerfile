# The product's Linux build and launch check (plan phase S, stage S6): tools/native/linux_app.sh.
FROM ubuntu:25.10
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config python3 \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev \
    libgl-dev libegl-dev libgles-dev libpulse-dev libasound2-dev \
    xvfb xauth libgl1-mesa-dri mesa-utils \
 && rm -rf /var/lib/apt/lists/*
