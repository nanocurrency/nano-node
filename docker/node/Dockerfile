FROM ubuntu:16.04

ENV BOOST_BASENAME=boost_1_66_0 \
    BOOST_ROOT=/tmp/boost_install  \
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
    ./b2 --prefix=${BOOST_ROOT} link=static install && \
    rm -rf ${BOOST_BASENAME} && \
    rm -f ${BOOST_BASENAME}.tar.gz && \
    cd ..

ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DBOOST_ROOT=${BOOST_ROOT} && \
    make rai_node && \
    cd ..

FROM ubuntu:16.04
COPY --from=0 /tmp/build/rai_node /usr/bin
COPY --from=0 /tmp/build/librai_lib.so /usr/bin
CMD ["rai_node", "--daemon"]
