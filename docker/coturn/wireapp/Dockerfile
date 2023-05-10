# This image derives from another coturn image, and adds a script which
# polls the Prometheus metrics endpoint until there are no more active
# allocations. This is intended to be used as a pre-stop hook to allow graceful
# termination of the coturn process without disrupting active allocations.

ARG coturn_image=coturn/coturn
ARG coturn_tag=latest

FROM ${coturn_image}:${coturn_tag}

USER root

# TODO: Drop tcpdump once it's not needed for testing anymore.
RUN apt-get update \
  && apt-get install -y --no-install-recommends --no-install-suggests \
    curl grep coreutils tcpdump libcap2-bin

RUN groupadd pcap
RUN usermod -a -G pcap nobody
RUN chgrp pcap /usr/bin/tcpdump

RUN setcap cap_net_raw,cap_net_admin=eip /usr/bin/tcpdump

COPY docker/coturn/wireapp/pre-stop-hook.sh /usr/local/bin/pre-stop-hook
RUN chmod +x /usr/local/bin/pre-stop-hook

USER nobody:nogroup
