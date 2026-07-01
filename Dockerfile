FROM ubuntu:22.04 AS ps3toolchain

ENV DEBIAN_FRONTEND=noninteractive

RUN \
  apt-get -y update && \
  apt-get -y install \
  autoconf automake bison flex gcc libelf-dev make \
  texinfo libncurses5-dev patch python-is-python3 subversion wget zlib1g-dev build-essential \
  libtool libtool-bin python-dev-is-python3 bzip2 libgmp3-dev pkg-config g++ libssl-dev clang && \
  apt-get -y clean autoclean autoremove && \
  rm -rf /var/lib/{apt,dpkg,cache,log}/

RUN mkdir /build
WORKDIR /build

COPY ./ps3toolchain /build

# Fixes certificate errors with letsencrypt in ARMv7
RUN echo "\nca_certificate=/etc/ssl/certs/ca-certificates.crt" | tee -a /etc/wgetrc

ENV PS3DEV=/ps3dev
ENV PSL1GHT=${PS3DEV}
ENV PATH=${PATH}:${PS3DEV}/bin:${PS3DEV}/ppu/bin:${PS3DEV}/spu/bin

RUN /build/toolchain.sh

###

FROM ubuntu:22.04 AS build

RUN apt-get update && \
  apt-get install -y \
    binutils-mips-linux-gnu \
    bsdmainutils \
    build-essential \
    libaudiofile-dev \
    libelf-dev \
    pkg-config \
    python3 \
    wget \
    zlib1g-dev

RUN wget http://developer.download.nvidia.com/cg/Cg_3.1/Cg-3.1_April2012_x86_64.deb && \
  echo '6da24fd6698dbb43ae5eee8691817d88d5792d89e2e8b9acf07597bec35cb080  Cg-3.1_April2012_x86_64.deb' \
    | sha256sum Cg-3.1_April2012_x86_64.deb && \
  dpkg -i Cg-3.1_April2012_x86_64.deb && \
  rm Cg-3.1_April2012_x86_64.deb

COPY --from=ps3toolchain /ps3dev /ps3dev

ENV PS3DEV=/ps3dev
ENV PSL1GHT=${PS3DEV}
ENV PATH=${PATH}:${PS3DEV}/bin:${PS3DEV}/ppu/bin:${PS3DEV}/spu/bin

RUN mkdir /sm64
WORKDIR /sm64

CMD echo 'usage: docker run --rm -v $(pwd):/sm64 sm64_ps3 make VERSION=xx build/xx_ps3/sm64.xx.f3dex2e.pkg ICON0=ICON0.PNG -j$(nproc)\n'
