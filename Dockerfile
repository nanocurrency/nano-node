FROM debian:8.9
MAINTAINER Zjeraar <zjeraar@palmweb.nl>

ENV BOOST_BASENAME=boost_1_66_0 \
    BOOST_ROOT=/tmp/boost  \
    BOOST_URL=http://sourceforge.net/projects/boost/files/boost/1.66.0/boost_1_66_0.tar.gz/download

RUN \
  apt-get update && \
  apt-get install -yq apt-utils && \
  apt-get install -yq locales && \
  apt-get install -yq build-essential && \
  apt-get install -yq wget && \
  apt-get install -yq git && \
  apt-get install -yq cmake && \
  apt-get install -yq g++ && \
  apt-get install -yq curl && \
  apt-get clean && \
  rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* && \
  dpkg-reconfigure locales && \
  echo 'en_US.UTF-8 UTF-8' >> /etc/locale.gen && \
  locale-gen

WORKDIR /tmp

RUN \
  wget -O ${BOOST_BASENAME}.tar.gz ${BOOST_URL} && \
  tar xzvf ${BOOST_BASENAME}.tar.gz && \
  cd ${BOOST_BASENAME} && \
  ./bootstrap.sh && \
  ./b2 --prefix=${BOOST_ROOT} link=static install && \
  rm -rf ${BOOST_BASENAME} && \
  rm -f ${BOOST_BASENAME}.tar.gz && \
  cd .. && \
  mkdir app

ADD ./ /tmp/app

RUN \
  cd app && \
  git submodule update --init --recursive && \
  cmake -DBOOST_ROOT=${BOOST_ROOT} -G "Unix Makefiles" && \
  make rai_node && \
  cp rai_node /usr/local/bin/rai_node &&  \
  ln -s /usr/local/bin/rai_node /usr/bin/rai_node && \
  cd .. && \
  rm -rf app && \
  rm -rf ${BOOST_ROOT}

ADD ./docker_init.sh /usr/local/bin/rai_node_init.sh

RUN chmod +x /usr/local/bin/rai_node_init.sh

WORKDIR /root

VOLUME /root/RaiBlocks

EXPOSE 7075 7076

ENTRYPOINT ["/bin/bash", "/usr/local/bin/rai_node_init.sh"]
