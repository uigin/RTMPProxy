## 1. Introduction
This project **RTMPProxy** is developed to provide a proxy between the RTMP client and RTMP server, so that the data streams published to server or pulled from server can be processed for some specific purposes (such as encryption and decryption) before they reach their target.

Obviously, for a RTMP application, the data streams mainly refer to audio stream and video stream.

## 2. Dependency

**RTMPProxy** only depends on [librtmp 2.3](http://rtmpdump.mplayerhq.hu/download/), while the build of [librtmp 2.3](http://rtmpdump.mplayerhq.hu/download/) requires the [openssl 1.0.2u](https://www.openssl.org/source/old/1.0.2/) and [zlib 1.2.11](https://sourceforge.net/projects/libpng/files/zlib//). Note that, other versions of openssl and zlib may also work well, but they need to be verified.

## 3. Build Project

- Step 1
```
./build_lib.sh <COMPILER_PREFIX>
```
By default, the script will use **gcc** to compile the source codes of all libs, but the compiler can be changed by specifying the prefix of target compiler, i.e, **<COMPILER_PREFIX>**.
- Step 2
```
make CROSS_COMPILE=<COMPILER_PREFIX>
```



