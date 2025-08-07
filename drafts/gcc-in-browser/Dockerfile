FROM --platform=i386 docker.io/i386/debian:bookworm

ARG DEBIAN_FRONTEND=noninteractive

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get -y upgrade && \
    apt-get install -y binutils-arm-none-eabi gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-dev libstdc++-arm-none-eabi-newlib libstdc++-arm-none-eabi-picolibc \
    picolibc-arm-none-eabi make

# TODO: picolibc is huge: need to delete unused architectures

RUN useradd -m user && echo "user:password" | chpasswd
RUN echo 'root:password' | chpasswd
CMD [ "/bin/bash" ]

# Image creation process:
# 1. Build the image: docker build -t <image_name>.
# 2. Create a container (e.g. run any command): docker run --name <container_name> <image_name> true
# 3. Export container to a tar file: docker export -o <file_name>.tar <container_id>
# 4. Create zeroed file: dd if=/dev/zero of=ext2.img bs=1M count=1024
# 5. Create ext2 filesystem: mkfs.ext2 -F ext2.img
# 6. mkdir mnt && sudo mount -o loop ext2.img mnt
# 7. sudo tar -xf rootfs.tar -C mnt
# 8. sudo umount mnt
