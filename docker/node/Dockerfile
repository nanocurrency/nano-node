FROM ubuntu:22.04 AS builder

ARG COMPILER=gcc
ARG CELERIX_NETWORK=live

# Install build dependencies
COPY ./ci/prepare/linux /tmp/prepare
RUN /tmp/prepare/prepare.sh

COPY ./ /tmp/src
WORKDIR /tmp/src

# Define ARGs for ci/build-node.sh
ARG BUILD_TYPE=RelWithDebInfo
ARG CELERIX_TEST=OFF
ARG CELERIX_TRACING=OFF
ARG COVERAGE=OFF
ARG CMAKE_SANITIZER=""
ARG CI_TAG=DEV_BUILD
ARG CI_VERSION_PRE_RELEASE=OFF
ARG SANITIZER

# Build node
RUN ci/build-node.sh
RUN echo ${CELERIX_NETWORK} >/etc/celerix-network

FROM ubuntu:22.04

RUN groupadd --gid 1000 celerixcurrency && \
    useradd --uid 1000 --gid celerixcurrency --shell /bin/bash --create-home celerixcurrency

COPY --from=builder /tmp/src/build/celerix_node /usr/bin
COPY --from=builder /tmp/src/build/celerix_rpc /usr/bin
COPY --from=builder /tmp/src/api/ /usr/bin/api/
COPY --from=builder /etc/celerix-network /etc

COPY docker/node/entry.sh /usr/bin/entry.sh
COPY docker/node/config /usr/share/celerix/config
RUN chmod +x /usr/bin/entry.sh
RUN ln -s /usr/bin/celerix_node /usr/bin/rai_node
RUN ldconfig

WORKDIR /root
USER root

ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/usr/bin/entry.sh"]
CMD ["celerix_node", "daemon", "-l"]

ARG REPOSITORY=celerixcurrency/celerix-node
LABEL org.opencontainers.image.source https://github.com/$REPOSITORY