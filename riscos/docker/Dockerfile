# RISC OS cross-build environment: GCCSDK 4.7.4 (arm-unknown-riscos) + SDL 1.2.
# The official GCCSDK SVN host is gone, so we start from the prebuilt
# riscos.info image and add a modern CMake/Ninja plus the packaged SDL 1.2.
FROM riscosdotinfo/riscos-gccsdk-4.7:latest

ARG CMAKE_VERSION=3.30.5
ARG NINJA_VERSION=1.12.1
ARG SDL_PKG=https://www.riscos.info/packages/arm/Library/libsdl1.2-dev_1.2.16-1_arm.zip

RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz \
 && tar -xzf cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz -C /opt \
 && ln -s /opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin/* /usr/local/bin/ \
 && rm cmake-*.tar.gz \
 && wget -q https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip \
 && unzip -q ninja-linux.zip -d /usr/local/bin && rm ninja-linux.zip

# SDL 1.2: RISC OS packages store headers as SDL/h/<name>; convert to SDL/<name>.h.
RUN wget -q -O /tmp/sdl.zip ${SDL_PKG} \
 && unzip -q /tmp/sdl.zip -d /tmp/sdl \
 && mkdir -p /home/riscos/env/include/SDL /home/riscos/env/lib \
 && for f in /tmp/sdl/Apps/Library/!SDL/SDL/h/*; do cp "$f" /home/riscos/env/include/SDL/$(basename "$f").h; done \
 && cp /tmp/sdl/Apps/Library/!SDL/libSDL.a /tmp/sdl/Apps/Library/!SDL/libSDLmain.a /home/riscos/env/lib/ \
 && rm -rf /tmp/sdl /tmp/sdl.zip

ENV PATH=/home/riscos/cross/bin:$PATH
WORKDIR /work
