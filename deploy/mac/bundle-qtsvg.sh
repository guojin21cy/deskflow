#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# bundle-qtsvg.sh <app-bundle> <qt-lib-dir>
#
# In split Qt installs (e.g. Homebrew) QtSvg lives outside qtbase and
# macdeployqt fails to resolve it from the svg plugins, leaving theme icons
# broken. Copy the framework into the bundle and point its Qt references at
# the bundled frameworks.
set -e

app="$1"
qtlib="$2"
fw="$app/Contents/Frameworks/QtSvg.framework/Versions/A"

[ -f "$fw/QtSvg" ] && exit 0

mkdir -p "$fw/Resources"
cp -f "$qtlib/QtSvg.framework/Versions/A/QtSvg" "$fw/QtSvg"
if [ -f "$qtlib/QtSvg.framework/Versions/A/Resources/Info.plist" ]; then
  cp -f "$qtlib/QtSvg.framework/Versions/A/Resources/Info.plist" "$fw/Resources/Info.plist"
fi

# codesign requires the standard framework symlink layout
fwroot="$app/Contents/Frameworks/QtSvg.framework"
ln -sfh A "$fwroot/Versions/Current"
ln -sfh Versions/Current/QtSvg "$fwroot/QtSvg"
ln -sfh Versions/Current/Resources "$fwroot/Resources"

chmod u+w "$fw/QtSvg"
install_name_tool -id @rpath/QtSvg.framework/Versions/A/QtSvg "$fw/QtSvg"

otool -L "$fw/QtSvg" | awk 'NR>1 && $1 ~ /^\// && $1 ~ /Qt[A-Za-z0-9]+\.framework/ {print $1}' |
while read -r dep; do
  case "$dep" in
  /System/*) continue ;;
  esac
  rel="@rpath/$(echo "$dep" | sed -E 's|.*/(Qt[A-Za-z0-9]+\.framework/.*)|\1|')"
  install_name_tool -change "$dep" "$rel" "$fw/QtSvg"
done

echo "bundled QtSvg into $app"
