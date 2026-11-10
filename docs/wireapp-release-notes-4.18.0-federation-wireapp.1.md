# wireapp/4.18.0-federation-wireapp.1

Wire's coturn moves from upstream 4.6.2 to upstream **4.18.0**. Wire's changes
(federation, zrest, the rate-limit allowlist, memory fixes and the Helm chart)
are applied on top of 4.18.0 as separate commits.

- Image / appVersion: `4.18.0-federation-wireapp.1`
- Helm chart: `1.0.0` (previously `0.0.46`), a **major** version bump.
- Previous release: `4.6.2-federation-wireapp.31`

## Breaking changes

### Configuration options removed

These Wire-only options no longer exist. A config that still sets any of them
**fails at startup** with an unknown-option error:

| Removed | Replacement |
|---|---|
| `401-req-limit=<n>` | `unauthorized-ratelimit` + `unauthorized-ratelimit-rps=<n>` (per second) |
| `401-window=<s>` | none, the limit is now per second |
| `401-allowlist=<file>` | `unauthorized-ratelimit-allowlist=<file>` |
| `prometheus-ip=<ip>` (removed upstream) | `prometheus` + `prometheus-address=<ip>` |

The chart handles this for you (see below). Only hand-written configs need
editing.

### 401 rate limit behaviour

Wire's own 401 limiter has been replaced by upstream's per-source limiter
(`--unauthorized-ratelimit`). The allowlist was ported on top of it.

- The limit was **1000 per 120 s window**. It is now **10 per second per source
  address with no burst** (`unauthorized-ratelimit-rps`, default 10).
- Source addresses are hashed into a fixed table, so two addresses that collide
  share one budget.
- Only UDP is limited. TCP/TLS cannot be spoofed for reflection.
- The allowlist now also covers the DTLS listener paths, not only the main UDP
  path.
- Upstream defaults the limiter to **off**. The chart turns it on
  (`ratelimit.enabled: true`), so protection does not silently disappear. With
  a hand-written config you must add `unauthorized-ratelimit` yourself.

See [docs/401-ratelimit.md](401-ratelimit.md) for details.

### Upstream behaviour changes since 4.6.2 that operators will notice

- **DTLS client listeners are off unless `--dtls` is given.** `no-dtls` is no
  longer needed. Federation does **not** depend on `--dtls`: the federation
  listener is started independently, with or without DTLS on the federation
  link.
- **New log format:** ISO-8601 timestamps, space separated, no thread id. Update
  log parsers and alerts that match on the old format.
- `no-cli`, `no-rfc5780`, `no-software-attribute` and
  `no-stun-backward-compatibility` describe what is now the default.
  `no-stun-backward-compatibility` is rejected; the chart no longer renders any
  of them.
- `drop-invalid-packets` and `response-origin-only-with-rfc5780` are deprecated
  and ignored.
- Stateless nonces are on by default.
- Relay peers in these ranges are denied by default:
  - IPv4 link-local `169.254.0.0/16`, which includes the cloud metadata
    address.
  - IPv6 link-local `fe80::/10`.
  - IPv6 ULA `fc00::/7`.
  - IPv6 site-local `fec0::/10`.

  Add an `allowed-peer-ip` range if your peers live there.
- `--udp-recvmmsg` (batched receive) is on by default on Linux.
- OpenSSL 1.1.1 or newer is required; 3.x is the supported target.

## Helm chart 1.0.0

Chart 1.0.0 deploys coturn 4.18. Before upgrading:

- Delete `ratelimit.override`, `ratelimit.requestLimit` and `ratelimit.window`
  from your values. Rendering **fails** if any of them is still set.
- New values: `ratelimit.enabled` (default `true`) and `ratelimit.rps`
  (default `10`).
- `ratelimit.allowlist` is unchanged. Changing it rolls the pods (checksum
  annotation); the file is mounted via `subPath`, so there is no live reload.
- Metrics now use `prometheus` + `prometheus-address`. `coturnPrometheusIP`
  still sets the address, so the metrics endpoint, the probes and the pre-stop
  drain hook keep working without value changes.
- The chart also picks up the changes that were only on the 4.6.2 line's
  feature branch: certificate reloader sidecar, SNAT port pinning, memory
  limits, optional external-IP helper and the HIP-15 annotation.

See the "Upgrading to 1.0.0" section in
[charts/coturn/README.md](../charts/coturn/README.md).

## New features

- **`federation-CA-file`**: a separate CA bundle for verifying federation
  peers. If it is not set, `CA-file` is used, as before. It is reloaded on
  SIGUSR2 together with the federation certificate and key.
- **`unauthorized-ratelimit-allowlist=<file>`**: one literal IPv4/IPv6 address
  per line; `#` comments and blank lines are skipped. IPv4-mapped IPv6 sources
  match their IPv4 entry. The file is re-read on `SIGRTMIN+3` (Linux). If the
  file can't be read, the old list is kept.
- **New Prometheus metrics**:
  - `turn_ratelimit_allowlist_bypasses`: allowlist bypass counter.
  - `turn_ratelimit_allowlist_entries`: allowlist size gauge.
  - Upstream's 401 limiter metrics (`turn_unauthenticated_401_*`,
    `turn_ratelimit_*`).
  - Packet and batching counters.

  No existing metric was removed or renamed.

## Carried over from the 4.6.2 line

- Federation (DTLS or plain UDP), including federation channel numbers,
  multi-SFT support, certificate reload on SIGUSR2 and local-address logging.
  Federation peers are exempt from 4.18's new DTLS protections, which cap
  half-open handshakes and drop records from unknown sources.
- zrest authentication, including SFT credentials (an `sft-` token is accepted
  past its deadline). The username parser no longer uses `regex.h`.
- Version string `wireapp/4.18.0a` so a running server identifies as Wire's
  build.

## Fixes

- Use-after-free in `write_client_connection`: the buffer size was read after
  the send. This is still present upstream.
- `handle_turn_refresh` tested the `no_response` pointer instead of its value.
- Per-engine STUN buffer reuse is disabled; buffers are allocated and freed
  directly, as on the 4.6.2 line. It can be turned back on at build time with
  `TURN_ENGINE_BUFFER_POOL=1`.

## Dropped

- Wire's own 401 limiter and its `401-*` options (replaced, see above).
- Thread-id logging. Upstream's 4.18 log format removes it on purpose.
- Debug-only commits from the 4.6.2 line.

## Image

`docker/coturn/wireapp/Dockerfile` builds the Wire image on top of the
upstream coturn image. It adds `dumb-init` as the entrypoint, so SIGTERM
reaches turnserver for draining, plus the `pre-stop-hook.sh` drain script.

## Validation

- Unit tests pass.
- All `examples/run_tests*.sh` pass on Linux. This includes the new federation
  end-to-end, zrest and allowlist tests.
- The federation DTLS handshake suite passes (67/67).
- The fuzzing smoke tests pass.
- The debian image builds and passes its Bats tests.
- Builds checked: autotools, CMake, and both without Prometheus.
- Checked by hand: federation with 1 and 4 relay threads, with and without
  DTLS on the federation link; a separate `federation-CA-file`, including a
  negative case; SIGUSR2 reload under live federation; zrest SFT credentials
  across the federation link; and draining via the pre-stop hook.
