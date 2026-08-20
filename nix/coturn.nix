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
