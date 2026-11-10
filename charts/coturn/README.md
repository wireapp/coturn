This chart deploys Wire's fork of [coturn](https://github.com/coturn/coturn),
a STUN and TURN server, with some additional features developed by Wire (see
[here](https://github.com/wireapp/coturn/tree/wireapp)) to support our calling
services.

You need to supply a list of one or more zrest secrets at the key
`secrets.zrestSecrets`. The secret provided to the brig chart in
`secrets.turn.secret` must be included in this list.

Note that coturn pods are deployed with `hostNetwork: true`, as they need to
listen on a wide range of UDP ports. Additionally, some TCP ports need to be
exposed on the hosting node, which are listed in `values.yaml`.

Due to the nature of TURN, this service might also expose the
internal network to which the hosting node is connected. It is
therefore recommended to run coturn on a separate Kubernetes cluster
from the rest of the Wire services. Further details may be found in
Wire's documentation for Restund, another TURN implementation, on
[this](https://docs.wire.com/understand/restund.html#network) page.

coturn can optionally be configured to expose a TLS control port. The TLS
private key and certificates should be provided in a `Secret` whose name is
given in `tls.secretRef`.

## Upgrading to 1.0.0

Chart 1.0.0 deploys coturn 4.18. Review these changes before upgrading:

- The 401 rate limit now uses coturn's per-source limiter
  (`unauthorized-ratelimit`). It is on by default (`ratelimit.enabled: true`)
  and allows `ratelimit.rps` (default 10) 401 responses per second per source
  address with no burst, instead of the previous 1000 per 120 s window.
  Sources are hashed into buckets, so colliding addresses share one budget.
  Only UDP is limited.
- `ratelimit.override`, `ratelimit.requestLimit` and `ratelimit.window` are
  removed. Rendering fails if any of them is set; delete them from your values.
- `ratelimit.allowlist` is kept and now also exempts sources on the DTLS
  listener paths. Changing it rolls the pods.
- The metrics endpoint is enabled with `prometheus` and
  `prometheus-address` (coturn 4.18 dropped `prometheus-ip`).
  `coturnPrometheusIP` still sets the address.
- `no-dtls`, `no-cli`, `no-rfc5780`, `no-software-attribute` and
  `no-stun-backward-compatibility` are no longer rendered. coturn 4.18 rejects
  or deprecates them, and their behavior is now the default.
