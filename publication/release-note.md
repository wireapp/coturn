## Overview

5.6.0 is the first coturn release made the standard Wire way. The server itself doesn't change: it's the one of `4.6.2-federation-wireapp.59`, upstream coturn 4.6.2 plus the Wire patches (drain mode, federation relay, rate limiting, DTLS certificate reload). What changes is how it's versioned, built and published. The structured release declaration is [`declaration.json`](declaration.json).

### Highlights

**SemVer versions**: from now on coturn is versioned `X.Y.Z`. We start at 5.6.0 to pick up where the coturn charts already published stop (the `5.5.x` ones in the S3 repositories). The image `quay.io/wire/coturn` and the Helm chart share the version, and the chart is also published to `oci://quay.io/wire/charts/stable/coturn`.

**Release pipeline**: releases are prepared and published by this repository's own workflows, with the shared `wire-rm-actions`, then declared to the release manager.

## Breaking changes

- Deployments pin `5.6.0` instead of `4.6.2-federation-wireapp.N`.
- The chart now declares `kubeVersion: ">=1.21.0-0"`. On an older cluster the install stops right away.

## Maintenance

- Release files live in `publication/`: compatibility, release note, release declaration.
- The CI inherited from upstream coturn is replaced by a PR workflow (build and tests, image, chart, release files) and a lint of the workflows (actionlint, zizmor).
- One image build instead of two (`docker/Dockerfile`). The base moves from Debian bullseye, now out of support and missing its security packages on the mirrors, to bookworm. coturn links against OpenSSL 3 instead of 1.1.

## Configuration

No configuration change.
