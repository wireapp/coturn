# The wire fork of prometheus-client-c, which coturn links against for its
# /metrics endpoint. This mirrors the `dist-libprom` stage of
# docker/coturn/debian/Dockerfile.
#
# Upstream's CMakeLists.txt only has `install(TARGETS ... ARCHIVE)`, which
# installs nothing for a SHARED library, so the .so files and headers are
# copied by hand here, exactly like the Dockerfile does.
{ stdenv
, fetchFromGitHub
, cmake
, libmicrohttpd
}:

stdenv.mkDerivation {
  pname = "prometheus-client-c";
  version = "unstable-2022-03-18";

  src = fetchFromGitHub {
    owner = "wireapp";
    repo = "prometheus-client-c";
    # Keep in sync with the `prom_commit` build arg in
    # docker/coturn/debian/Dockerfile.
    rev = "66deada5dc9b005ffd1bb509ff08b2d1d722356c";
    hash = "sha256-2bTJMOdSj4pjgKBUGOk2HHIxRCrHJiATf0572cKQfnA=";
  };

  nativeBuildInputs = [ cmake ];
  buildInputs = [ libmicrohttpd ];

  # prom and promhttp are two separate cmake projects, and promhttp's
  # CMakeLists.txt looks for libprom in ../prom/build, so they have to be built
  # in-tree and in that order rather than by the generic cmake hooks.
  dontUseCmakeConfigure = true;

  # Upstream compiles with -Werror against a much older compiler than the one
  # nixpkgs provides, and predates -fno-common becoming the default.
  env.NIX_CFLAGS_COMPILE = "-fcommon -Wno-error";

  buildPhase = ''
    runHook preBuild

    cmake -S prom -B prom/build -G "Unix Makefiles" \
      -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
      -DCMAKE_INSTALL_RPATH="${placeholder "out"}/lib" \
      -DCMAKE_C_FLAGS="-DPROM_LOG_ENABLE -g -O3"
    make -C prom/build

    cmake -S promhttp -B promhttp/build -G "Unix Makefiles" \
      -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
      -DCMAKE_INSTALL_RPATH="${placeholder "out"}/lib" \
      -DCMAKE_C_FLAGS="-g -O2"
    make -C promhttp/build VERBOSE=1

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/lib $out/include
    cp prom/build/libprom.so promhttp/build/libpromhttp.so $out/lib/
    cp -r prom/include/* promhttp/include/* $out/include/

    runHook postInstall
  '';

  meta = {
    description = "Prometheus client library in C, wire fork used by coturn";
    homepage = "https://github.com/wireapp/prometheus-client-c";
  };
}
