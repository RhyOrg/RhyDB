ARG DEPENDENCY_IMAGE=ghcr.io/rhyorg/rhydb-dependencies:latest

FROM $DEPENDENCY_IMAGE AS builder

COPY CMakeLists.txt Makefile conanfile.py ./
COPY buildScripts/ ./buildScripts/
COPY src/ ./src/
COPY app/ ./app/
COPY performance/ ./performance/
COPY testBaseData/ ./testBaseData/
# optionally copy release_version.txt if it exists
COPY release_version.txt* ./

RUN  \
    export CMAKE_BUILD_PARALLEL_LEVEL=4; \
    make build/Release/rhydb_test \
    && make build/Release/rhydb_app_test \
    && make build/Release/rhydb \
    && cp build/Release/rhydb_test . \
    && cp build/Release/rhydb_app_test . \
    && cp build/Release/rhydb .


FROM ubuntu:26.04 AS server

WORKDIR /app
COPY <<EOF ./default_preprocessing_config.yaml
inputDirectory: "/preprocessing/input/"
outputDirectory: "/preprocessing/output/"
EOF

COPY <<EOF ./default_runtime_config.yaml
dataDirectory: /data/
EOF

COPY --from=builder /src/rhydb ./

# Deprecation compatibility: the binary was renamed silo -> rhydb. Keep a
# `silo` shim in the image during the deprecation period so consumers that
# invoke the binary by its old name (e.g. `--entrypoint ./silo` or scripts
# that exec /app/silo) keep working. Remove once the deprecation period ends.
RUN ln -s rhydb /app/silo

RUN apt update && apt dist-upgrade -y \
    &&  apt install -y curl jq

# call /info, extract "seqeunceCount" from the JSON and assert that the value is not 0. If any of those fails, "exit 1".
HEALTHCHECK --start-period=20s CMD curl --fail --silent localhost:8081/info | jq .sequenceCount | xargs test 0 -ne || exit 1

EXPOSE 8081

ENTRYPOINT ["./rhydb"]

ENV RHYDB_PREPROCESSING_CONFIG="/app/preprocessing_config.yaml"
ENV RHYDB_DEFAULT_PREPROCESSING_CONFIG="/app/default_preprocessing_config.yaml"
ENV RHYDB_DEFAULT_RUNTIME_CONFIG="/app/default_runtime_config.yaml"

LABEL org.opencontainers.image.source="https://github.com/RhyOrg/RhyDB"
LABEL org.opencontainers.image.description="High-performance analytical database for sequence alignment data"
