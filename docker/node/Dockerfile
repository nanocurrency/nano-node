FROM nanocurrency/nano-env:gcc

ARG NETWORK=live
ARG CI_TAG=DEV_BUILD
ARG CI_BUILD=OFF
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
cd /tmp/build && \
cmake /tmp/src -DCI_BUILD=${CI_BUILD} \
-DBOOST_ROOT=${BOOST_ROOT} -DPORTABLE=1 \
-DACTIVE_NETWORK=nano_${NETWORK}_network \
-DNANO_POW_SERVER=ON -DNANO_SHARED_BOOST=ON && \
make nano_node -j $(nproc) && \
make nano_rpc -j $(nproc) && \
make nano_pow_server -j $(nproc) && \
cd .. && \
echo ${NETWORK} >/etc/nano-network

FROM ubuntu:18.04

RUN groupadd --gid 1000 nanocurrency && \
useradd --uid 1000 --gid nanocurrency --shell /bin/bash --create-home nanocurrency

COPY --from=0 /tmp/build/nano_node /usr/bin
COPY --from=0 /tmp/build/nano_rpc /usr/bin
COPY --from=0 /tmp/build/nano_pow_server /usr/bin
COPY --from=0 /tmp/src/api/ /usr/bin/api/
COPY --from=0 /etc/nano-network /etc
COPY docker/node/entry.sh /usr/bin/entry.sh
COPY docker/node/config /usr/share/nano/config
COPY --from=0 /tmp/boost/lib/* /usr/local/lib/
RUN chmod +x /usr/bin/entry.sh
RUN ln -s /usr/bin/nano_node /usr/bin/rai_node
RUN ldconfig

WORKDIR /root
USER root

ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/usr/bin/entry.sh"]
CMD ["nano_node", "daemon", "-l"]
ARG REPOSITORY=nanocurrency/nano-node
LABEL org.opencontainers.image.source https://github.com/$REPOSITORY
