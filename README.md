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

## How to release

1. Prepare the release files in [`publication/`](publication) (release note, declaration, compatibility file) and merge them into `dev`.
2. Start the `Prepare release` workflow, from the Actions tab or the command line. For the GA of the version prepared on `dev`:

   ```sh
   gh workflow run prepare-release.yml -R wireapp/coturn --ref dev -f source_branch=dev -f type=ga -f bump=patch
   ```

   `type` is the channel (`alpha`, `beta`, `rc`, `ga`). `bump` counts when the last release was a GA: it picks the next version, for the next GA or for the first pre-release of a new line. After a pre-release, keep `patch`: the GA takes the version of the pre-release.

   For a patch of an LTS line, release from its branch, for example `release/5.6`. The release files are read from the source branch, so they go into `release/5.6`, not `dev`:

   ```sh
   gh workflow run prepare-release.yml -R wireapp/coturn --ref dev -f source_branch=release/5.6 -f type=ga -f bump=patch
   ```
3. Approve the publication: the `Publish release` job waits for a release manager in the `release-publication` environment.
4. The release is then finalised: the GitHub Release is published with its tag, the image is promoted on Quay and the chart published, `wire-builds` is bumped for a release from `dev`, and the release manager is notified.
5. For now, pre-releases and GAs land in staging. Pushing to production is a separate, explicit step with its own workflow.

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

```sh
apt-get install build-essential pkg-config libevent-dev libssl-dev \
  libpq-dev libmariadb-dev libsqlite3-dev libhiredis-dev libmongoc-dev libmicrohttpd-dev
```

```sh
./configure
make
make check
(cd examples && ./run_tests.sh)
```

The image is built by [`docker/Dockerfile`](docker/Dockerfile) (Debian bookworm, OpenSSL 3), and [`.github/scripts/test-image.sh`](.github/scripts/test-image.sh) checks it, including the federation DTLS handshake tests of [`test/`](test):

```sh
docker build -f docker/Dockerfile -t coturn:local .
IMAGE=coturn:local .github/scripts/test-image.sh
```

## CI and releases

- `pr.yml` runs on every pull request: build and tests, image, chart, release files. The four checks are required to merge.
- `dev.yml` runs on every push to `dev`: it publishes the dev build, image and chart, and declares it to the release manager.
- `prepare-release.yml` is started by hand with the source branch, the channel (`ga`, `alpha`, `beta`, `rc`) and the bump. It builds the candidate and a draft GitHub Release. `publish-release.yml` then waits for a release manager's approval in the `release-publication` environment, promotes the image by digest, publishes the chart, tags the commit and declares the release.

The release flow and the files in `publication/` follow Wire's release standard with the shared actions of [wire-rm-actions](https://github.com/wireapp/wire-rm-actions).

## Contributing

The code is owned by `@wireapp/avs`; the workflows and `publication/` also by `@wireapp/release-managers` (see [`CODEOWNERS`](.github/CODEOWNERS)). Open pull requests against `dev`. Keep the patches on top of upstream as small as you can: the fork is meant to be rebased on newer upstream versions.

## More documentation

- [`README.turnserver`](README.turnserver), [`README.turnadmin`](README.turnadmin), [`README.turnutils`](README.turnutils): all the options.
- [`charts/coturn/README.md`](charts/coturn/README.md): deploying the chart.
- [`docs/`](docs): the upstream documentation (build, configuration, databases, testing).
- [`publication/release-note.md`](publication/release-note.md): the note of the release in preparation.
