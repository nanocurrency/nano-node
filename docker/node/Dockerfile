FROM nanocurrency/nano-env:gcc

ARG NETWORK=live
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DBOOST_ROOT=${BOOST_ROOT} -DACTIVE_NETWORK=nano_${NETWORK}_network && \
    make nano_node && \
    make nano_rpc && \
    cd .. && \
    echo ${NETWORK} > /etc/nano-network

FROM ubuntu:16.04
COPY --from=0 /tmp/build/nano_node /usr/bin
COPY --from=0 /tmp/build/nano_rpc /usr/bin
COPY --from=0 /etc/nano-network /etc
COPY docker/node/entry.sh /entry.sh
COPY docker/node/config /usr/share/nano/config
RUN chmod +x /entry.sh
RUN ln -s /usr/bin/nano_node /usr/bin/rai_node
CMD ["/bin/bash",  "/entry.sh"]
