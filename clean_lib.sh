#!/bin/bash

ROOT=`pwd`
cd $ROOT/zlib-1.2.11
rm -rf build
make clean

cd $ROOT/openssl-1.0.2u
rm -rf build
make clean

cd $ROOT/librtmp-2.3
rm -rf build
make clean

