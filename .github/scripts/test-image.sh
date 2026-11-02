#!/usr/bin/env bash
# Checks a locally built coturn image (IMAGE): every library of turnserver
# resolves, the tools the chart calls are present, and the federation DTLS
# handshake tests of test/ pass against the turnserver of the image.
set -euo pipefail

: "${IMAGE:?IMAGE is required}"

docker run --rm --entrypoint /bin/sh "$IMAGE" -c '! ldd /usr/bin/turnserver | grep "not found"'
docker run --rm --entrypoint /bin/sh "$IMAGE" -c 'test -x /usr/local/bin/pre-stop-hook && command -v curl pgrep dumb-init dig openssl'
docker run --rm --entrypoint /bin/bash --volume "$PWD/test:/src/test:ro" "$IMAGE" \
  -c 'cp -R /src/test /tmp/test && cd /tmp/test && ./run_federation_dtls_handlshake_tests.sh'
