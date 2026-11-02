# The Wire coturn image: coturn and prometheus-client-c built from source on
# Debian, with the pre-stop hook that drains allocations before termination.
# Built from the repository root: docker build -f docker/Dockerfile .

FROM debian:bookworm-slim@sha256:99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3 AS base

FROM base AS dist-libprom

RUN apt-get update \
 && apt-get install -y --no-install-recommends --no-install-suggests \
            ca-certificates cmake g++ git make libmicrohttpd-dev \
 && update-ca-certificates

ARG prom_commit=66deada5dc9b005ffd1bb509ff08b2d1d722356c
RUN mkdir -p /build/ && cd /build/ \
 && git init \
 && git remote add origin https://github.com/wireapp/prometheus-client-c \
 && git fetch --depth=1 origin "${prom_commit}" \
 && git checkout FETCH_HEAD

RUN mkdir -p /build/prom/build/ && cd /build/prom/build/ \
 && TEST=0 cmake -G "Unix Makefiles" \
                 -DCMAKE_INSTALL_PREFIX=/usr \
                 -DCMAKE_SKIP_BUILD_RPATH=TRUE \
                 -DCMAKE_C_FLAGS="-DPROM_LOG_ENABLE -g -O3" \
                 .. \
 && make

RUN mkdir -p /build/promhttp/build/ && cd /build/promhttp/build/ \
 && TEST=0 cmake -G "Unix Makefiles" \
                 -DCMAKE_INSTALL_PREFIX=/usr \
                 -DCMAKE_SKIP_BUILD_RPATH=TRUE \
                 -DCMAKE_C_FLAGS="-g -O2" \
                 .. \
 && make VERBOSE=1

RUN LIBS_DIR=/out/$(dirname $(find /usr/ -name libc.so)) \
 && mkdir -p $LIBS_DIR/ \
 && cp -rf /build/prom/build/libprom.so \
           /build/promhttp/build/libpromhttp.so \
       $LIBS_DIR/ \
 && mkdir -p /out/usr/include/ \
 && cp -rf /build/prom/include/* \
           /build/promhttp/include/* \
       /out/usr/include/ \
 && mkdir -p /out/usr/share/licenses/prometheus-client-c/ \
 && cp /build/LICENSE /out/usr/share/licenses/prometheus-client-c/

FROM base AS dist-coturn

RUN apt-get update \
 && apt-get install -y --no-install-recommends --no-install-suggests \
            autoconf ca-certificates coreutils g++ libtool make pkg-config \
            libevent-dev \
            libssl-dev \
            libpq-dev libmariadb-dev libsqlite3-dev \
            libhiredis-dev \
            libmongoc-dev \
            libmicrohttpd-dev \
 && update-ca-certificates

COPY --from=dist-libprom /out/ /

COPY CMakeLists.txt \
     configure \
     INSTALL \
     LICENSE \
     make-man.sh Makefile.in \
     postinstall.txt \
     README.turn* \
     /app/
COPY cmake/ /app/cmake/
COPY examples/ /app/examples/
COPY man/ /app/man/
COPY src/ /app/src/
COPY turndb/ /app/turndb/
WORKDIR /app/

RUN ./configure --prefix=/usr \
                --turndbdir=/var/lib/coturn \
                --disable-rpath \
                --sysconfdir=/etc/coturn \
                --mandir=/tmp/coturn/man \
                --docsdir=/tmp/coturn/docs \
                --examplesdir=/tmp/coturn/examples \
 && make

RUN mkdir -p /out/ \
 && DESTDIR=/out make install \
 && rm -rf /out/tmp/ \
 && mkdir -p /out/usr/share/licenses/coturn/ \
 && cp LICENSE /out/usr/share/licenses/coturn/ \
 && rm -f /out/etc/coturn/turnserver.conf.default

COPY docker/coturn/rootfs/ /out/
COPY docker/coturn/wireapp/pre-stop-hook.sh /out/usr/local/bin/pre-stop-hook
RUN chmod +x /out/usr/local/bin/docker-entrypoint.sh \
             /out/usr/local/bin/detect-external-ip.sh \
             /out/usr/local/bin/pre-stop-hook \
 && ln -s /usr/local/bin/detect-external-ip.sh \
          /out/usr/local/bin/detect-external-ip \
 && chown -R nobody:nogroup /out/var/lib/coturn/

COPY --from=dist-libprom /out/ /out/

FROM base AS runtime

RUN apt-get update \
 && apt-get upgrade -y \
 && apt-get install -y --no-install-recommends --no-install-suggests \
            ca-certificates \
 && update-ca-certificates \
 && apt-get install -y --no-install-recommends --no-install-suggests \
            libatomic1 \
            libevent-2.1-7 libevent-core-2.1-7 libevent-extra-2.1-7 \
            libevent-openssl-2.1-7 libevent-pthreads-2.1-7 \
            libssl3 \
            libpq5 libmariadb3 libsqlite3-0 \
            libhiredis0.14 \
            libmongoc-1.0-0 \
            libmicrohttpd12 \
            bind9-dnsutils \
            dumb-init \
            curl grep coreutils procps \
 && rm -rf /var/lib/apt/lists/*

COPY --from=dist-coturn /out/ /

RUN apt-get update \
 && apt-get install -y --no-install-recommends --no-install-suggests \
            libcap2-bin \
 && setcap CAP_NET_BIND_SERVICE=+ep /usr/bin/turnserver \
 && apt-get purge -y --auto-remove \
                  -o APT::AutoRemove::RecommendsImportant=false \
            libcap2-bin \
 && rm -rf /var/lib/apt/lists/*

USER nobody:nogroup

EXPOSE 3478 3478/udp 5349 5349/udp

VOLUME ["/var/lib/coturn"]

ENTRYPOINT ["docker-entrypoint.sh"]

CMD ["--log-file=stdout", "--external-ip=$(detect-external-ip)"]
