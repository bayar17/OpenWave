#!/usr/bin/env bash
set -Eeuo pipefail
out="${1:?usage: fetch-host-rpm-tools.sh DIRECTORY}"
mkdir -p "$out/debs" "$out/host-rpm"
cd "$out/debs"
names=$(apt-get install --simulate rpm 2>/dev/null | awk '/^Inst/ {print $2}')
apt-get download $names >/dev/null
for deb in ./*.deb; do dpkg-deb -x "$deb" "$out/host-rpm"; done
LD_LIBRARY_PATH="$out/host-rpm/usr/lib/x86_64-linux-gnu" "$out/host-rpm/usr/bin/rpmbuild" --version
