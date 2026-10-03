# syntax=docker/dockerfile:1

FROM docker.io/library/alpine:3.24@sha256:294b683cb724975bec92580e1e685676bd4b50bda910ddb8c51d4cabeaec77e6 AS builder
# hadolint ignore=DL3018
RUN apk add --no-cache clang gcc musl-dev make pkgconf ca-certificates \
    libxml2-dev curl-dev openssl-dev zlib-dev binutils
WORKDIR /src
COPY . .
RUN set -e; \
    test -f /src/beaker/Makefile || (echo "Missing vendored beaker/ (include beaker/ in the repo clone)." >&2; exit 1); \
    rm -rf /src/beaker/build /src/obj /src/bin; \
    make CC=clang all

FROM docker.io/library/alpine:3.24@sha256:294b683cb724975bec92580e1e685676bd4b50bda910ddb8c51d4cabeaec77e6
# hadolint ignore=DL3018
RUN apk add --no-cache libxml2 libcurl openssl zlib libgcc wget \
    && addgroup -g 65532 seeker \
    && adduser -D -H -u 65532 -G seeker -s /sbin/nologin seeker \
    && mkdir -p /app \
    && chown seeker:seeker /app
COPY --from=builder /src/bin/seeker /usr/local/bin/seeker
COPY docker/config.ini /etc/seeker/config.ini
COPY --from=builder /src/templates /app/templates
COPY --from=builder /src/static /app/static
COPY --from=builder /src/locales /app/locales
COPY docker/entrypoint.sh /usr/local/bin/seek-entry
RUN chmod 755 /usr/local/bin/seek-entry \
    && chown -R seeker:seeker /app
WORKDIR /app
ENV HOME=/tmp
USER 65532:65532
EXPOSE 5000
HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
    CMD ["wget", "-q", "-O", "/dev/null", "http://127.0.0.1:5000/"]
ENTRYPOINT ["/usr/local/bin/seek-entry"]
