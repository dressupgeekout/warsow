#!/usr/bin/env bash
set -ex

mkdir -p build /tmp/warsow/share/warsow

pushd build
  cmake -GNinja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/tmp/warsow ..
  ninja
  rsync -avr --delete ./ /tmp/warsow/share/warsow/
popd

pushd ~/dl
  tar -xzv -f warsow-2.1.2.tar.gz
  rsync -avr ~/dl/warsow-2.1.2/basewsw/ /tmp/warsow/share/warsow/basewsw/
popd

pushd /tmp/warsow/share/warsow
  LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/X11R7/lib ./warsow
popd
