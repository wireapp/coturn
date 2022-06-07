ARG coturn_image=coturn/coturn
ARG coturn_tag=latest

FROM ${coturn_image}:${coturn_tag}

USER root

RUN apt-get update \
  && apt-get install -y --no-install-recommends --no-install-suggests \
    curl grep coreutils

COPY docker/coturn/wireapp/pre-stop-hook.sh /usr/local/bin/pre-stop-hook
RUN chmod +x /usr/local/bin/pre-stop-hook

USER nobody:nogroup
