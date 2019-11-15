FROM nanocurrency/nano-env:gcc

ARG NETWORK=live
ARG TRAVIS_TAG=DEV_BUILD
ARG CI_BUILD=OFF
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DCI_BUILD=${CI_BUILD} -DBOOST_ROOT=${BOOST_ROOT} -DACTIVE_NETWORK=nano_${NETWORK}_network \
    -DNANO_ROCKSDB=ON -DNANO_POW_SERVER=ON -DROCKSDB_LIBRARIES=/tmp/rocksdb/lib/librocksdb.a \
    -DROCKSDB_INCLUDE_DIRS=/tmp/rocksdb/include && \
    make bananode -j $(nproc) && \
    make banano_rpc -j $(nproc) && \
    make nano_pow_server -j $(nproc) && \
    cd .. && \
    echo ${NETWORK} > /etc/nano-network

FROM ubuntu:16.04

RUN groupadd --gid 1000 nanocurrency && \
    useradd --uid 1000 --gid nanocurrency --shell /bin/bash --create-home nanocurrency

COPY --from=0 /tmp/build/bananode /usr/bin
COPY --from=0 /tmp/build/banano_rpc /usr/bin
COPY --from=0 /tmp/build/nano_pow_server /usr/bin
COPY --from=0 /etc/nano-network /etc
COPY docker/node/entry.sh /usr/bin/entry.sh
COPY docker/node/config /usr/share/nano/config
RUN chmod +x /usr/bin/entry.sh

WORKDIR /root
USER root

ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/bin/bash",  "/usr/bin/entry.sh"]
CMD ["bananode daemon -l"]
