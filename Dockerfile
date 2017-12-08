FROM debian:8.9
MAINTAINER Zjeraar <gerard@meijer.gs>

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
  wget -O boost_1_63_0.tar.gz http://sourceforge.net/projects/boost/files/boost/1.63.0/boost_1_63_0.tar.gz/download && \
  tar xzvf boost_1_63_0.tar.gz && \
  cd boost_1_63_0 && \
  ./bootstrap.sh && \
  ./b2 --prefix=../[boost] link=static install && \
  cd .. && \
  mkdir app

ADD ./ /tmp/app

RUN \
  cd app && \
  git submodule update --init --recursive && \
  cmake -DBOOST_ROOT=../[boost] -G "Unix Makefiles" && \
  make rai_node && \
  cp rai_node /usr/local/bin/rai_node &&  \
  ln -s /usr/local/bin/rai_node /usr/bin/rai_node && \
  cd .. && \
  rm -rf rai_build && \
  rm -rf boost_1_63_0 && \
  rm -f boost_1_63_0.tar.gz && \
  useradd -m -u 7075 rai

EXPOSE 7075  7076

USER rai
