FROM ubuntu:16.04

ENV BOOST_BASENAME=boost_1_66_0 \
    BOOST_ROOT=/tmp/boost_install  \
    BOOST_URL=https://sourceforge.net/projects/boost/files/boost/1.66.0/boost_1_66_0.tar.gz/download

RUN apt-get update -qq && apt-get install -yqq \
    build-essential \
    cmake \
    g++ \
    wget && \
    wget -qO ${BOOST_BASENAME}.tar.gz ${BOOST_URL} && \
    tar xzf ${BOOST_BASENAME}.tar.gz && \
    cd ${BOOST_BASENAME} && \
    ./bootstrap.sh && \
    ./b2 -d0 --prefix=${BOOST_ROOT} link=static install && \
    rm -rf ${BOOST_BASENAME} && \
    rm -f ${BOOST_BASENAME}.tar.gz && \
    cd .. && \
    mkdir /usr/share/raiblocks/

ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DBOOST_ROOT=${BOOST_ROOT} && \
    make rai_node && \
    cd ..

FROM ubuntu:16.04
COPY --from=0 /tmp/build/rai_node /usr/bin
COPY docker/node/entry.sh /entry.sh
COPY docker/node/config.json /usr/share/raiblocks/config.json
RUN chmod +x /entry.sh
CMD ["/bin/bash",  "/entry.sh"]
