FROM ubuntu:16.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && apt-get install -yqq \
    build-essential \
    cmake \
    g++ \
    wget \
    python

ENV BOOST_ROOT=/usr/local
ADD util/build_prep/bootstrap_boost.sh bootstrap_boost.sh
RUN ./bootstrap_boost.sh -m -B 1.66
RUN rm bootstrap_boost.sh

RUN apt-get update -qq && apt-get install -yqq \
    qt5-default \
    valgrind \
    xorg xvfb xauth xfonts-100dpi xfonts-75dpi xfonts-scalable xfonts-cyrillic \
    cargo

