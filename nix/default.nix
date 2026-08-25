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
#
# See ./README.md for building, testing and pushing these by hand.
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

  baseContents = [ coturn rootfs ] ++ (with pkgs; [
    bashInteractive # docker-entrypoint.sh and pre-stop-hook.sh are bash
    cacert
    coreutils
    dnsutils # dig, for detect-external-ip.sh
    dumb-init
    gawk
    gnugrep
    gnused # the chart templates its config with sed
    iana-etc
    openssl.bin # test/run_federation_dtls_handlshake_tests.sh

    dockerTools.binSh
    dockerTools.fakeNss
    dockerTools.usrBinEnv
  ]);

  mkImage = { name, contents }: dockerTools.streamLayeredImage {
    inherit name contents;
    tag = "dev";

    extraCommands = ''
      # fakeNss is the only input providing /var (as /var/empty), so buildEnv
      # links /var into the read-only store. Make it a real directory again:
      # coturn's turndb directory has to be writable.
      if [ -L var ]; then
        rm var
        mkdir -p var/empty
      fi
      mkdir -p var/lib/coturn
      chmod 0777 var/lib/coturn
    '';

    config = {
      # nobody:nogroup, which is what the Dockerfiles switch to. Note that
      # turnserver no longer carries CAP_NET_BIND_SERVICE as a file capability
      # -- a nix build sandbox has no CAP_SETFCAP -- so the chart grants the
      # capability to the container instead.
      User = "65534";
      Group = "65534";

      Entrypoint = [ "/usr/local/bin/docker-entrypoint.sh" ];
      Cmd = [ "--log-file=stdout" "--external-ip=$(detect-external-ip)" ];

      Env = [
        "PATH=/usr/local/bin:/usr/bin:/bin"
        "SSL_CERT_FILE=/etc/ssl/certs/ca-bundle.crt"
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
    name = "quay.io/wire/coturn-base";
    contents = baseContents;
  };

  wire-image = mkImage {
    name = "quay.io/wire/coturn";
    contents = baseContents ++ [ preStopHook ] ++ (with pkgs; [
      curl
      procps # pgrep/pkill, used by pre-stop-hook.sh
    ]);
  };
}
