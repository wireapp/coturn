## Overview

5.6.0 is the first coturn release made the standard Wire way. The server is the one of `4.6.2-federation-wireapp.59`, upstream coturn 4.6.2 plus the Wire patches (drain mode, federation relay, rate limiting, DTLS certificate reload), with one fix: the CA bundle of the federation no longer makes the TURN server ask its clients for a certificate. What changes besides is how it's versioned, built and published. The structured release declaration is [`declaration.json`](declaration.json).

### Highlights

**SemVer versions**: from now on coturn is versioned `X.Y.Z`. We start at 5.6.0 to pick up where the coturn charts already published stop (the `5.5.x` ones in the S3 repositories). The image `quay.io/wire/coturn` and the Helm chart share the version, and the chart is also published to `oci://quay.io/wire/charts/stable/coturn`.

**Release pipeline**: releases are prepared and published by this repository's own workflows, with the shared `wire-rm-actions`, then declared to the release manager.

**TURNS works for clients again**: the chart set `CA-file` on every deployment, and in coturn that option makes the TLS and DTLS listeners require a client certificate, which Wire clients don't have. The federation now has its own option, `federation-CA-file`, and the chart renders the `ca.*` values into it, only when DTLS federation is enabled. `CA-file` is no longer set.

## Breaking changes

- Deployments pin `5.6.0` instead of `4.6.2-federation-wireapp.N`.
- The chart now declares `kubeVersion: ">=1.21.0-0"`. On an older cluster the install stops right away.

## Maintenance

- Release files live in `publication/`: compatibility, release note, release declaration.
- The CI inherited from upstream coturn is replaced by a PR workflow (build and tests, image, chart, release files) and a lint of the workflows (actionlint, zizmor).
- One image build instead of two (`docker/Dockerfile`). The base moves from Debian bullseye, now out of support and missing its security packages on the mirrors, to bookworm. coturn links against OpenSSL 3 instead of 1.1.

## Configuration

- New option `federation-CA-file`: the CA bundle used to verify the federation peers. When it is not set, the federation falls back to `CA-file`, as before.
- Chart: `ca.*` is only used when `federate.enabled` and `federate.dtls.enabled` are both true. `ca.enabled` is deprecated and will be removed in a later release: a custom bundle will then apply whenever `ca.secretRef` or `ca.cert` is set.
