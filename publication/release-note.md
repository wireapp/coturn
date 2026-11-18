## Overview

5.6.0 is the first coturn release made the standard Wire way. It starts from the server of `4.6.2-federation-wireapp.59`, upstream coturn 4.6.2 plus the Wire patches (drain mode, federation relay, rate limiting, DTLS certificate reload), and adds two fixes: TURNS clients are no longer asked for a client certificate, and the rate limiter no longer races with its own pruning. The rest is about how coturn is versioned, built and published. The structured release declaration is [`declaration.json`](declaration.json).

Deploy the image and the chart of the same release. An older image ignores `federation-CA-file` with a warning and accepts federation peers without checking their certificate.

### Highlights

**TURNS works for clients again**: the chart used to set `CA-file` on every deployment, for federation. That option also makes coturn require a client certificate on its TLS listener, so every TURNS client was turned away at its first Allocate. Federation now has its own CA bundle, `federation-CA-file`, and the chart no longer sets `CA-file`.

**SemVer versions**: from now on coturn is versioned `X.Y.Z`. We start at 5.6.0 to pick up where the coturn charts already published stop (the `5.5.x` ones in the S3 repositories). The image `quay.io/wire/coturn` and the Helm chart share the version, and the chart is also published to `oci://quay.io/wire/charts/stable/coturn`.

**Release pipeline**: releases are prepared and published by this repository's own workflows, with the shared `wire-rm-actions`, then declared to the release manager.

## Breaking changes

- Deployments pin `5.6.0` instead of `4.6.2-federation-wireapp.N`.
- The chart now declares `kubeVersion: ">=1.21.0-0"`. On an older cluster the install stops right away.

## Fixes

- TURNS clients without a client certificate are accepted again. With DTLS federation, peers are still verified, against `federation-CA-file`.
- A federation CA bundle that cannot be found no longer switches off the TLS and DTLS client listeners. Only federation peers are rejected.
- The rate limiter no longer reads its address table while another thread inserts or prunes entries.

## Deprecations

- `ca.enabled` will be removed in a future release. A custom bundle will then be used whenever `ca.secretRef` or `ca.cert` is set, and turning off federation peer validation will no longer be possible.

## Maintenance

- Release files live in `publication/`: compatibility, release note, release declaration.
- The CI inherited from upstream coturn is replaced by a PR workflow (build and tests, image, chart, release files) and a lint of the workflows (actionlint, zizmor).
- One image build instead of two (`docker/Dockerfile`). The base moves from Debian bullseye, now out of support and missing its security packages on the mirrors, to bookworm. coturn links against OpenSSL 3 instead of 1.1.

## Configuration

- New coturn option `federation-CA-file`: the CA bundle federation uses to verify its DTLS peers. When it is not set, federation falls back to `CA-file`, so hand-written configurations behave as before.
- Chart: the `ca.*` bundle is rendered as `federation-CA-file` inside the `federate.dtls` block, and `CA-file` is no longer rendered. The values keep their meaning but only apply when `federate.enabled` and `federate.dtls.enabled` are both true; otherwise the CA secret and its mount are not created.
- Chart: `ca.enabled` is deprecated, see above.

---

## Details

- Federation CA bundle separate from `CA-file`, in the server, the chart and the tests: [#70](https://github.com/wireapp/coturn/pull/70), [`633790f5`](https://github.com/wireapp/coturn/commit/633790f5), [`0215bdb5`](https://github.com/wireapp/coturn/commit/0215bdb5), [`100c81b7`](https://github.com/wireapp/coturn/commit/100c81b7), [`dd0672d9`](https://github.com/wireapp/coturn/commit/dd0672d9)
- Rate limit lock held across the address lookup: [#77](https://github.com/wireapp/coturn/pull/77), [`179a8e3f`](https://github.com/wireapp/coturn/commit/179a8e3f), WPB-15070
