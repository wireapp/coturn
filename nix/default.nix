# Nix build of the two container images this repository publishes:
#
#   base-image  -> quay.io/wire/coturn-base   (docker/coturn/debian/Dockerfile)
#   wire-image  -> quay.io/wire/coturn        (docker/coturn/wireapp/Dockerfile)
#
# The Dockerfiles are still there for local use, but CI builds these instead,
# because building a Dockerfile needs a privileged container and nix does not.
# See ci/parametrized-pipelines/coturn.dhall in zinfra/cailleach.
#
# `pkgs` and `src` are passed in by cailleach, which owns the nixpkgs pin:
#
#   nix-build ./nix/default.nix -A base-image --arg pkgs 'import <nixpkgs> {}'
{ pkgs
, src ? ../.
}:

let
  inherit (pkgs) dockerTools;

  # Drop .git and friends, so the image does not get rebuilt for changes that
  # cannot affect it.
  cleanSrc = pkgs.lib.cleanSource src;

  prometheus-client-c = pkgs.callPackage ./prometheus-client-c.nix { };

  coturn = pkgs.callPackage ./coturn.nix {
    src = cleanSrc;
    inherit prometheus-client-c;
  };

  # docker/coturn/rootfs, plus /usr/bin/dumb-init, which the helm chart runs by
  # absolute path.
  rootfs = pkgs.runCommand "coturn-rootfs" { } ''
    mkdir -p $out/usr/local/bin $out/usr/bin

    install -m 0755 ${cleanSrc}/docker/coturn/rootfs/usr/local/bin/docker-entrypoint.sh \
      $out/usr/local/bin/docker-entrypoint.sh
    install -m 0755 ${cleanSrc}/docker/coturn/rootfs/usr/local/bin/detect-external-ip.sh \
      $out/usr/local/bin/detect-external-ip.sh
    ln -s /usr/local/bin/detect-external-ip.sh $out/usr/local/bin/detect-external-ip

    ln -s ${pkgs.dumb-init}/bin/dumb-init $out/usr/bin/dumb-init
  '';

  # Only in the wire image, used as the pod's preStop hook.
  preStopHook = pkgs.runCommand "coturn-pre-stop-hook" { } ''
    mkdir -p $out/usr/local/bin
    install -m 0755 ${cleanSrc}/docker/coturn/wireapp/pre-stop-hook.sh \
      $out/usr/local/bin/pre-stop-hook
  '';

  baseContents = [
    coturn
    rootfs

    pkgs.bashInteractive # docker-entrypoint.sh and pre-stop-hook.sh are bash
    pkgs.coreutils
    pkgs.gnused # the chart templates its config with sed
    pkgs.gnugrep
    pkgs.gawk
    pkgs.dnsutils # dig, for detect-external-ip.sh
    pkgs.dumb-init
    pkgs.openssl.bin # test/run_federation_dtls_handlshake_tests.sh
    pkgs.cacert

    dockerTools.binSh
    dockerTools.usrBinEnv

    # The images run as `nobody:nogroup` (as the Dockerfiles do), but fakeNss
    # only creates a `nobody` group, and a container whose group cannot be
    # resolved fails to start. 65534 is the gid debian's nogroup has.
    (dockerTools.fakeNss.override {
      extraGroupLines = [ "nogroup:x:65534:" ];
    })
  ];

  mkImage = { name, contents }: dockerTools.buildLayeredImage {
    inherit name contents;
    tag = "dev";

    extraCommands = ''
      # /var would be a symlink into the read-only store, but coturn's turndb
      # directory has to be writable.
      cp -aL var var.real 2>/dev/null || mkdir var.real
      rm -rf var
      mv var.real var
      chmod -R u+w var
      mkdir -p var/lib/coturn
      chmod 0777 var/lib/coturn
    '';

    config = {
      # As in the Dockerfiles. Note that turnserver no longer carries
      # CAP_NET_BIND_SERVICE as a file capability -- nix cannot set one -- so
      # the chart grants the capability to the container instead.
      User = "nobody:nogroup";

      Entrypoint = [ "/usr/local/bin/docker-entrypoint.sh" ];
      Cmd = [ "--log-file=stdout" "--external-ip=$(detect-external-ip)" ];

      Env = [
        "PATH=/usr/local/bin:/usr/bin:/bin"
        "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
      ];

      ExposedPorts = {
        "3478/tcp" = { };
        "3478/udp" = { };
        "5349/tcp" = { };
        "5349/udp" = { };
      };

      Volumes = { "/var/lib/coturn" = { }; };
    };
  };
in
{
  inherit coturn prometheus-client-c;

  base-image = mkImage {
    name = "coturn-base";
    contents = baseContents;
  };

  wire-image = mkImage {
    name = "coturn";
    contents = baseContents ++ [
      pkgs.curl
      pkgs.procps # pgrep/pkill, used by pre-stop-hook.sh
      preStopHook
    ];
  };
}
