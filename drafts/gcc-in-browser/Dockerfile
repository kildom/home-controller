FROM --platform=i386 docker.io/i386/debian:bookworm

ARG ARM_BUILD_PACKAGES=" \
    binutils-arm-none-eabi gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-dev libstdc++-arm-none-eabi-newlib \
    libstdc++-arm-none-eabi-picolibc picolibc-arm-none-eabi make \
"

# Install linux packages (with cache)
ENV TZ=Etc/UTC
ARG DEBIAN_FRONTEND=noninteractive
RUN rm -f /etc/apt/apt.conf.d/docker-clean; echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' > /etc/apt/apt.conf.d/keep-cache
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get -y upgrade && \
    apt-get install -y $ARM_BUILD_PACKAGES

RUN useradd -m user && echo "user:password" | chpasswd
RUN echo 'root:password' | chpasswd
CMD [ "/bin/bash" ]

RUN \
    find /usr/lib/picolibc/arm-none-eabi/lib/ -type f \( -path '*/arm/*' -o -path '*/thumb/*' \) ! -path '*/thumb/v7e-m+fp/hard/*' -exec rm -rf {} +    && \
    (cd /usr/lib/picolibc/arm-none-eabi/lib/ && rm -f `ls /usr/lib/picolibc/arm-none-eabi/lib/thumb/v7e-m+fp/hard/`)    && \
    (cd /usr/lib/picolibc/arm-none-eabi/lib/release/ && rm -f `ls /usr/lib/picolibc/arm-none-eabi/lib/release/thumb/v7e-m+fp/hard/`)    && \
    find /usr/lib/picolibc/arm-none-eabi/lib/ -type d -empty -delete    && \
    \
    find /usr/lib/arm-none-eabi/newlib/ -type f \( -path '*/arm/*' -o -path '*/thumb/*' \) ! -path '*/thumb/v7e-m+fp/hard/*' -exec rm -rf {} +    && \
    (cd /usr/lib/arm-none-eabi/newlib/ && rm -f `find /usr/lib/arm-none-eabi/newlib/thumb/v7e-m+fp/hard/ \( -name '*.a' -o -name '*.o' \) -printf '%f '`)    && \
    find /usr/lib/arm-none-eabi/newlib/ -type d -empty -delete    && \
    \
    find /usr/lib/gcc/arm-none-eabi/*/thumb/ -type f ! -path '*/thumb/v7e-m+fp/hard/*' -exec rm -rf {} +    && \
    find /usr/lib/gcc/arm-none-eabi/*/arm/ -type f ! -path '*/arm/v7e-m+fp/hard/*' -exec rm -rf {} +    && \
    find /usr/lib/gcc/arm-none-eabi/*/thumb/ -type d -empty -delete    && \
    find /usr/lib/gcc/arm-none-eabi/*/arm/ -type d -empty -delete
COPY fanotify_monitor.c /root/fanotify_monitor.c
