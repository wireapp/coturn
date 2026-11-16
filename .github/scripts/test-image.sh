#!/usr/bin/env bash
# Checks a locally built coturn image (IMAGE): every library of turnserver
# resolves, the tools the chart calls are present, and the federation DTLS
# handshake tests of test/ pass against the turnserver of the image.
#
# When REPORT is set, the output of the federation DTLS handshake tests is also
# written to that file, with a header naming what was tested (VERSION,
# SOURCE_COMMIT and RUN_URL are optional and only used for the header).
set -euo pipefail

: "${IMAGE:?IMAGE is required}"

docker run --rm --entrypoint /bin/sh "$IMAGE" -c '! ldd /usr/bin/turnserver | grep "not found"'
docker run --rm --entrypoint /bin/sh "$IMAGE" -c 'test -x /usr/local/bin/pre-stop-hook && command -v curl pgrep dumb-init dig openssl'

federation_dtls_tests() {
  docker run --rm --entrypoint /bin/bash --volume "$PWD/test:/src/test:ro" "$IMAGE" \
    -c 'cp -R /src/test /tmp/test && cd /tmp/test && ./run_federation_dtls_handlshake_tests.sh'
}

if [[ -z "${REPORT:-}" ]]; then
  federation_dtls_tests
  exit 0
fi

{
  echo "coturn federation DTLS handshake test report"
  echo
  echo "Version:       ${VERSION:-unknown}"
  echo "Source commit: ${SOURCE_COMMIT:-unknown}"
  echo "Workflow run:  ${RUN_URL:-unknown}"
  echo "Date (UTC):    $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "Tested image:  $IMAGE ($(docker image inspect --format '{{.Id}}' "$IMAGE"))"
  echo "OpenSSL:       $(docker run --rm --entrypoint openssl "$IMAGE" version)"
  echo "Test script:   test/run_federation_dtls_handlshake_tests.sh"
  echo
} > "$REPORT"

federation_dtls_tests 2>&1 | tee -a "$REPORT"
