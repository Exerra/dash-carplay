#!/bin/sh
set -eu
cd "$(dirname "$0")/receiver"
./gradlew build :server:installDist
case "$(uname -s)" in
  Darwin) ./macos/bt-bridge/build.sh ;;
esac
echo "Dash CarPlay receiver built. Hardware pairing has not been tested."
