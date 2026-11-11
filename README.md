# coturn, Wire's fork

Wire's TURN server: upstream [coturn](https://github.com/coturn/coturn) 4.6.2 with the patches Wire needs for its calling services. It ships as the image `quay.io/wire/coturn` and the Helm chart in [`charts/coturn`](charts/coturn), deployed next to [wire-server](https://github.com/wireapp/wire-server) and the SFT.

The upstream README, with the list of supported RFCs and platforms, is kept as [`docs/README.upstream.md`](docs/README.upstream.md).

## What Wire adds

- **Federation.** Clients of two coturn instances exchange media through a single tunnel between the servers, over DTLS with mutual certificate authentication and a whitelist of peers. The SFT uses it for federated calls. A client enters federation by allocating with a username that starts with `sft-`. Options `--federation-*` in [`README.turnserver`](README.turnserver).
- **zrest authentication.** The TURN REST API with Wire's username format and password derivation, sharing its secret with brig. Option `--zrest`.
- **Drain mode.** On `SIGUSR1` (or `drain` on the CLI) the server stops accepting allocations and waits for the existing ones to end. The chart's preStop hook uses it for rolling restarts.
- **Rate limiting of 401 responses** per client address, with an allow-list reloaded on `SIGRTMIN+3`, against reflection attacks. Options `--401-req-limit`, `--401-window`, `--401-allowlist`.
- **Certificate reload** on `SIGUSR2`, for the TURNS and the federation certificates. The chart runs a sidecar that sends it when the mounted secrets change.
- **The Helm chart**, with Prometheus metrics, detection of the external IP, SNAT of the federation port and a pod disruption budget.

## Versions and artifacts

Since 5.6.0 the versions are SemVer `X.Y.Z`, tagged `vX.Y.Z`. The image and the chart share the version:

| Artifact | Location |
| --- | --- |
| Image | `quay.io/wire/coturn:X.Y.Z` |
| Chart, releases | `oci://quay.io/wire/charts/stable/coturn` |
| Chart, pre-releases and dev builds | `oci://quay.io/wire/charts/dev/coturn` |

Every push to `dev` produces a dev build, `<next version>-<timestamp>.g<sha>`, published to the `dev` chart repository. Pre-releases are `X.Y.Z-alpha.N`, `-beta.N` or `-rc.N`. Each release has a GitHub Release with the release note, the release declaration and the compatibility file from [`publication/`](publication).

Releases before 5.6.0 were named `4.6.2-federation-wireapp.N`.

## Branches

- `dev` is the trunk and the default branch. Every pull request targets it.
- `release/X.Y` is created at each minor release and receives its patch releases. A fix lands on `dev` first and is backported.
- `wireapp/4.6.2-federation` is the previous trunk, frozen at the last `4.6.2-federation-wireapp.N` build.

## Building and testing

Dependencies: libevent2 and OpenSSL, plus libmicrohttpd and [prometheus-client-c](https://github.com/digitalocean/prometheus-client-c) for the metrics, and the client libraries of the databases you want. On Debian or Ubuntu:

```
apt-get install build-essential pkg-config libevent-dev libssl-dev \
  libpq-dev libmariadb-dev libsqlite3-dev libhiredis-dev libmongoc-dev libmicrohttpd-dev
```

```
./configure
make
make check
(cd examples && ./run_tests.sh)
```

The image is built by [`docker/Dockerfile`](docker/Dockerfile) (Debian bookworm, OpenSSL 3), and [`.github/scripts/test-image.sh`](.github/scripts/test-image.sh) checks it, including the federation DTLS handshake tests of [`test/`](test):

```
docker build -f docker/Dockerfile -t coturn:local .
IMAGE=coturn:local .github/scripts/test-image.sh
```

## CI and releases

- `pr.yml` runs on every pull request: build and tests, image, chart, release files. The four checks are required to merge.
- `dev.yml` runs on every push to `dev`: it publishes the dev build, image and chart, and declares it to the release manager.
- `prepare-release.yml` is started by hand with the source branch, the channel (`ga`, `alpha`, `beta`, `rc`) and the bump. It builds the candidate and a draft GitHub Release. `publish-release.yml` then waits for a release manager's approval in the `release-publication` environment, promotes the image by digest, publishes the chart, tags the commit and declares the release.

The release flow and the files in `publication/` follow Wire's release standard ([wire-onprem-first-decisions](https://github.com/wireapp/wire-onprem-first-decisions), ADR-0020 to ADR-0022), with the shared actions of [wire-rm-actions](https://github.com/wireapp/wire-rm-actions).

## Contributing

The code is owned by `@wireapp/avs`; the workflows and `publication/` also by `@wireapp/release-managers` (see [`CODEOWNERS`](.github/CODEOWNERS)). Open pull requests against `dev`. Keep the patches on top of upstream as small as you can: the fork is meant to be rebased on newer upstream versions.

## More documentation

- [`README.turnserver`](README.turnserver), [`README.turnadmin`](README.turnadmin), [`README.turnutils`](README.turnutils): all the options.
- [`charts/coturn/README.md`](charts/coturn/README.md): deploying the chart.
- [`docs/`](docs): the upstream documentation (build, configuration, databases, testing).
- [`publication/release-note.md`](publication/release-note.md): the note of the release in preparation.
