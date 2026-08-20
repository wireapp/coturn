# Builds turnserver from this checkout, mirroring the `dist-coturn` stage of
# docker/coturn/debian/Dockerfile: same configure flags, same DESTDIR layout,
# so the resulting image still has /usr/bin/turnserver where the helm chart
# and docker/coturn/wireapp/pre-stop-hook.sh expect it.
{ lib
, stdenv
, src
, prometheus-client-c
, pkg-config
, openssl
, libevent
, libmicrohttpd
, sqlite
, postgresql
, libmysqlclient
, hiredis
, mongoc
}:

stdenv.mkDerivation {
  pname = "coturn";
  version = "wireapp-4.6.2";

  inherit src;

  nativeBuildInputs = [ pkg-config ];

  buildInputs = [
    openssl
    (libevent.override { inherit openssl; })
    libmicrohttpd
    sqlite.dev
    postgresql
    libmysqlclient
    hiredis
    mongoc
    prometheus-client-c
  ];

  # This is a 4.6.2-era codebase, and gcc 14 promotes several of the sloppy C
  # constructs in it to hard errors: an out-of-order typedef in
  # ns_turn_maps.h (ur_addr_map_value_type is used at line 51 but only defined
  # at line 160), plus a char[] initialised from NULL, a missing prototype and
  # an array-vs-pointer argument in the ratelimit patch. Debian bullseye's
  # gcc 10, which docker/coturn/debian/Dockerfile builds with, accepts all of
  # them as warnings. Keep it that way rather than patching the source, so this
  # builds the same binary the image has always shipped.
  env.NIX_CFLAGS_COMPILE = toString [
    "-Wno-error=declaration-missing-parameter-type"
    "-Wno-error=implicit-function-declaration"
    "-Wno-error=implicit-int"
    "-Wno-error=incompatible-pointer-types"
    "-Wno-error=int-conversion"
    "-Wno-error=return-mismatch"
  ];

  # coturn ships a hand written ./configure that wants to drop temporary files
  # in /var/tmp or /tmp, neither of which exists in the build sandbox.
  postPatch = ''
    substituteInPlace configure \
      --replace-fail 'if [ -d /var/tmp ] ; then' 'if false ; then' \
      --replace-fail 'elif [ -d /tmp ] ; then' 'elif false ; then'
  '';

  # Same prefix as the Dockerfile, so the paths compiled into the binary match
  # what the image and the chart use. The tree is then staged into $out via
  # DESTDIR. Note that --disable-rpath is deliberately *not* passed: the
  # binaries need their RPATH to find the libraries in the nix store.
  dontAddPrefix = true;
  configureFlags = [
    "--prefix=/usr"
    "--sysconfdir=/etc/coturn"
    "--turndbdir=/var/lib/coturn"
    # No documentation in the image, to keep it small.
    "--mandir=/tmp/coturn/man"
    "--docsdir=/tmp/coturn/docs"
    "--examplesdir=/tmp/coturn/examples"
  ];

  installFlags = [ "DESTDIR=${placeholder "out"}" ];

  # The binaries land in $out/usr/bin, which nixpkgs' default strip list does
  # not cover. Leaving them unstripped keeps the debug info that coturn builds
  # with (-g), and that debug info still contains every
  # -I/nix/store/...-dev/include the compiler was invoked with. Nix scans those
  # strings and records them as runtime references, which pulls postgresql --
  # and with it clang, llvm and gcc, about 2GB -- into the image.
  stripDebugList = [ "usr/bin" "usr/lib" ];

  postInstall = ''
    rm -rf $out/tmp

    mkdir -p $out/usr/share/licenses/coturn
    cp LICENSE $out/usr/share/licenses/coturn/

    # The chart mounts its own config; the default would only be confusing.
    rm -f $out/etc/coturn/turnserver.conf.default

    # /var/lib/coturn has to be writable at runtime, so the image creates it
    # instead of it being a symlink into the read-only store.
    rm -rf $out/var
  '';

  meta = {
    description = "TURN server, wire fork with federation and prometheus support";
    homepage = "https://github.com/wireapp/coturn";
    license = lib.licenses.bsd3;
    mainProgram = "turnserver";
  };
}
