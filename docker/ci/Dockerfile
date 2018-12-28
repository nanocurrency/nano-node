FROM ubuntu:16.04

ENV BOOST_BASENAME=boost_1_66_0 \
    BOOST_URL=https://netix.dl.sourceforge.net/project/boost/boost/1.66.0/boost_1_66_0.tar.gz

RUN apt-get update -qq && apt-get install -yqq \
    build-essential \
    cmake \
    g++ \
    wget

ADD util/build_prep/bootstrap_boost.sh bootstrap_boost.sh
RUN ./bootstrap_boost.sh -m

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && apt-get install -yqq \
    qt5-default \
    valgrind \
    xorg xvfb xauth xfonts-100dpi xfonts-75dpi xfonts-scalable xfonts-cyrillic \
    cargo

