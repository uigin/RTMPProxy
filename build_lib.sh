#!/bin/bash

echo "[current compiler]: "$1"gcc"
echo ""

echo "[1/3]============= build zlib ================"
echo ""
sleep 1s
cd zlib-1.2.11
make CROSS_COMPILE=$1 
make install CROSS_COMPILE=$1
cd ..
echo ""

echo "[2/3]============= build openssl ================"
echo ""
sleep 1s
cd openssl-1.0.2u
make CROSS_COMPILE=$1 
make install CROSS_COMPILE=$1
cd ..
echo ""

echo "[2/3]============= build openssl ================"
echo ""
sleep 1s
cd librtmp-2.3
make CROSS_COMPILE=$1 
make install CROSS_COMPILE=$1
cd ..
echo ""

