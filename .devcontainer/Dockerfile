ARG BASE_IMAGE=ghcr.io/jaster-prj/devcontainer-zephyr:latest
FROM ${BASE_IMAGE}

USER root

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        build-essential \
        cmake \
        ninja-build \
        git \
        pkg-config \
        clang-format \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /opt/venv/bin/west /usr/local/bin/west

ENV PM_WORKSPACE=/workspace
ENV PATH=/opt/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

WORKDIR /workspace

CMD ["sleep", "infinity"]
