#!/usr/bin/env bash
# Packages charts/coturn at VERSION (chart version == appVersion == image tag)
# into OUT_DIR, with the coturn image of the helm.sh/images annotation (HIP-15)
# set to quay.io/wire/coturn:VERSION. The prepared chart directory is kept in
# OUT_DIR/src/coturn for publish-chart in package mode. Prints dir=<chart
# directory> and file=<path of the .tgz>, and appends them to $GITHUB_OUTPUT
# when set. The working tree is not modified.
set -euo pipefail

: "${VERSION:?VERSION is required}"
: "${OUT_DIR:?OUT_DIR is required}"

placeholder='quay.io/wire/coturn:do-not-use'
dir="$OUT_DIR/src/coturn"

rm -rf "$dir"
mkdir -p "$OUT_DIR/src"
cp -R charts/coturn "$dir"
grep -qF "$placeholder" "$dir/Chart.yaml" \
  || { echo "error: $placeholder not found in charts/coturn/Chart.yaml" >&2; exit 1; }
sed -i "s|${placeholder}|quay.io/wire/coturn:${VERSION}|" "$dir/Chart.yaml"

helm package "$dir" --version "$VERSION" --app-version "$VERSION" --destination "$OUT_DIR" >/dev/null
file="$OUT_DIR/coturn-${VERSION}.tgz"
[[ -f $file ]] || { echo "error: $file was not written" >&2; exit 1; }

printf 'dir=%s\nfile=%s\n' "$dir" "$file"
if [[ -n ${GITHUB_OUTPUT:-} ]]; then
  printf 'dir=%s\nfile=%s\n' "$dir" "$file" >> "$GITHUB_OUTPUT"
fi
