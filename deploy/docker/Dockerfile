ARG BASE_IMAGE=public.ecr.aws/ubuntu/ubuntu:22.04
FROM ${BASE_IMAGE} AS base

ARG DEBIAN_FRONTEND=noninteractive
ARG TZ=UTC
ARG LIBJUICE_REF=

ENV TZ=${TZ}
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime \
    && echo ${TZ} > /etc/timezone \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
       autoconf \
       automake \
       build-essential \
       ca-certificates \
       cmake \
       curl \
       git \
       gdb \
       libevent-dev \
       libfmt-dev \
       libgrpc++-dev \
       libhiredis-dev \
       libprotobuf-dev \
       librabbitmq-dev \
       librdkafka-dev \
       libspdlog-dev \
       libssl-dev \
       default-libmysqlclient-dev \
       ninja-build \
       pkg-config \
       protobuf-compiler \
       protobuf-compiler-grpc \
       python3 \
       unzip \
       wget \
       zlib1g-dev \
       libtool \
    && rm -rf /var/lib/apt/lists/*

RUN if [ -n "${LIBJUICE_REF}" ]; then \
       git clone --depth 1 --branch "${LIBJUICE_REF}" https://github.com/paullouisageneau/libjuice.git /tmp/libjuice; \
    else \
       git clone --depth 1 https://github.com/paullouisageneau/libjuice.git /tmp/libjuice; \
    fi \
    && cmake -S /tmp/libjuice -B /tmp/libjuice/build -G Ninja \
       -DCMAKE_BUILD_TYPE=Release \
       -DBUILD_SHARED_LIBS=ON \
       -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/libjuice/build \
    && cmake --install /tmp/libjuice/build \
    && rm -rf /tmp/libjuice \
    && ldconfig

WORKDIR /workspace

CMD ["sleep", "infinity"]

FROM base AS dev

FROM base AS portable

# Preserve executable bits only for files that already have them, while normalizing
# regular files to 0644 and directories to 0755 inside the image.
COPY --chmod=u=rwX,go=rX . /workspace
