# Building the wire coturn container images

CI builds the two images this repository publishes with nix, not from the
Dockerfiles in `docker/coturn/`: building a Dockerfile needs a privileged
container, and nix does not. The Dockerfiles are still there and still work, but
they are not what ends up on quay.

| nix attribute | image | replaces |
| --- | --- | --- |
| `base-image` | `quay.io/wire/coturn-base` | `docker/coturn/debian/Dockerfile` |
| `wire-image` | `quay.io/wire/coturn` | `docker/coturn/wireapp/Dockerfile` |

The derivations are in this directory; the pipeline that drives them is
`ci/parametrized-pipelines/coturn.dhall` in
[zinfra/cailleach](https://github.com/zinfra/cailleach).

## Build

nixpkgs is pinned in cailleach, so build through cailleach to get the same
image CI would produce. Adjust the path to your cailleach checkout:

```bash
cailleach=~/git/cailleach

nix-build "$cailleach/nix/default.nix" -A coturn-container --argstr coturnSrc "$PWD"
# ... or -A coturn-base-container for the base image
```

`./result` is a *script*, not a tarball: `dockerTools.streamLayeredImage` writes
the image to stdout instead of putting a copy of it in the nix store. Run it to
get one:

```bash
./result >/tmp/coturn.tar
```

To iterate without a cailleach checkout — faster, but built against whatever
nixpkgs your `NIX_PATH` points at rather than the pin CI uses:

```bash
nix-build ./nix/default.nix -A wire-image --arg pkgs 'import <nixpkgs> {}'
```

## Test it without pushing anything

The quickest check is the federation DTLS suite, which is also what the pipeline
runs against the built image. Build just the binary and lay it out where the test
script looks for it (it prefers `../bin/turnserver` over `/usr/bin/turnserver`):

```bash
nix-build "$cailleach/nix/default.nix" -A coturn-images.coturn --argstr coturnSrc "$PWD"

work=$(mktemp -d)
mkdir -p "$work/bin"
cp -r test "$work/test"
ln -s "$(realpath ./result)/usr/bin/turnserver" "$work/bin/turnserver"

(cd "$work/test" && ./run_federation_dtls_handlshake_tests.sh)
```

Or load the image and poke at it:

```bash
docker load </tmp/coturn.tar   # or: podman load
docker run --rm -it --entrypoint /bin/bash quay.io/wire/coturn:dev
```

## Push manually with a dummy tag

Use a tag that nothing can possibly deploy. Not `latest`, and not a
`<version>-federation-*` tag — the helm chart resolves those, and the pipeline
owns them.

```bash
tag="test-$USER-$(date +%Y%m%d)"

skopeo login quay.io   # or pass --dest-creds "$user:$token" below

skopeo --insecure-policy copy \
    docker-archive:/tmp/coturn.tar \
    docker://quay.io/wire/coturn:"$tag"
```

`--insecure-policy` is needed because skopeo refuses to do anything without a
`containers-policy.json`, and a nix-installed skopeo does not ship one. If you
would rather not pass the flag every time, write the policy once:

```bash
mkdir -p ~/.config/containers
echo '{"default": [{"type": "insecureAcceptAnything"}]}' \
    >~/.config/containers/policy.json
```

To try the image on a cluster, point the chart at your tag:

```bash
helm upgrade ... --set image.tag="$tag"
```

Delete the tag on quay when you are done with it.

## Two things worth knowing

- **`turnserver` has no file capability.** The Dockerfile ran
  `setcap CAP_NET_BIND_SERVICE=+ep /usr/bin/turnserver` so that it could bind
  port 3478 as a non-root user. A nix build sandbox has no `CAP_SETFCAP`, so it
  cannot do that, and `charts/coturn` grants `NET_BIND_SERVICE` to the container
  instead. A nix-built image on a chart older than that change will fail to bind
  3478.
- **This is a 4.6.2-era codebase built with a current compiler.** `nix/coturn.nix`
  demotes a handful of gcc 14 errors back to warnings, which is what the gcc 10
  in `docker/coturn/debian/Dockerfile` did with them. If you bump nixpkgs and the
  build breaks on a new diagnostic, that list is the first place to look.
