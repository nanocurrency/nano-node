FROM ubuntu:16.04

ENV BOOST_BASENAME=boost_1_66_0 \
    BOOST_URL=http://sourceforge.net/projects/boost/files/boost/1.66.0/boost_1_66_0.tar.gz/download

RUN apt-get update && apt-get install -yq \
    build-essential \
    cmake \
    g++ \
    wget

WORKDIR "/tmp"

RUN wget -O ${BOOST_BASENAME}.tar.gz ${BOOST_URL} && \
    tar xzvf ${BOOST_BASENAME}.tar.gz && \
    cd ${BOOST_BASENAME} && \
    ./bootstrap.sh && \
    ./b2 link=static install && \
    rm -rf ${BOOST_BASENAME} && \
    rm -f ${BOOST_BASENAME}.tar.gz && \
    cd ..

RUN apt-get install -yq \
    qt5-default \
    valgrind

RUN rm -rf /tmp/*
