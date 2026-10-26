FROM alpine:3.22 AS build
RUN apk add --no-cache build-base linux-headers curl tar patch
WORKDIR /src
RUN curl -fsSLO https://chrony-project.org/releases/chrony-4.8.tar.gz \
    && echo '33ea8eb2a4daeaa506e8fcafd5d6d89027ed6f2f0609645c6f149b560d301706  chrony-4.8.tar.gz' | sha256sum -c - \
    && tar -xzf chrony-4.8.tar.gz
WORKDIR /src/chrony-4.8
COPY patches/ /patches/
COPY request_log.h ./request_log.h
COPY log_offset.h ./log_offset.h
COPY tests/backpressure.c ./tests/backpressure.c
RUN patch --batch --fuzz=0 -p1 < /patches/0001-json-request-log.patch \
    && patch --batch --fuzz=0 -p1 < /patches/0002-synchronized-test-fixture.patch \
    && LDFLAGS=-static ./configure --prefix=/usr --sysconfdir=/etc/chrony --with-user=chrony \
       --disable-readline --disable-nts --disable-privdrop --without-seccomp \
    && make -j2 \
    && cc -Wall -Wextra -Werror tests/backpressure.c -o /tmp/test-log \
    && /tmp/test-log \
    && (make -C test/unit check > /tmp/unit-tests.log 2>&1 || { tail -80 /tmp/unit-tests.log; exit 1; }) \
    && make DESTDIR=/out install \
    && strip /out/usr/sbin/chronyd /out/usr/bin/chronyc
COPY entrypoint.c /src/entrypoint.c
COPY log_offset.h /src/log_offset.h
RUN cc -static -Os -Wall -Wextra -Werror /src/entrypoint.c -o /out/entrypoint \
    && strip /out/entrypoint \
    && ! readelf -l /out/entrypoint /out/usr/sbin/chronyd /out/usr/bin/chronyc | grep INTERP \
    && mkdir -p /out/etc /out/tmp \
    && chmod 1777 /out/tmp \
    && printf 'root:x:0:0:root:/root:/sbin/nologin\nchrony:x:100:101:chrony:/:/sbin/nologin\n' > /out/etc/passwd \
    && printf 'root:x:0:\nchrony:x:101:\n' > /out/etc/group

FROM scratch
ENV PATH=/usr/local/bin:/usr/sbin:/usr/bin
COPY --from=build /out/etc/ /etc/
COPY --from=build /out/tmp/ /tmp/
COPY --from=build /out/usr/sbin/chronyd /usr/sbin/chronyd
COPY --from=build /out/usr/bin/chronyc /usr/bin/chronyc
COPY --from=build /src/chrony-4.8/COPYING /usr/share/licenses/chrony/COPYING
COPY chrony.conf /usr/share/chrony-log/chrony.conf
COPY --from=build /out/entrypoint /usr/local/bin/entrypoint
USER 100:101
EXPOSE 123/udp
ENTRYPOINT ["/usr/local/bin/entrypoint"]
