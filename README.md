## 1. Introduction
This project **RTMPProxy** is developed to provide a proxy between the RTMP client and RTMP server, so that the data streams published to server or pulled from server can be processed for some specific purposes (such as encryption and decryption) before they reach their target.

Obviously, for a RTMP application, the data streams mainly refer to audio stream and video stream.

## 2. Dependency

**RTMPProxy** only depends on [librtmp 2.3](http://rtmpdump.mplayerhq.hu/download/), while the build of [librtmp 2.3](http://rtmpdump.mplayerhq.hu/download/) requires the [openssl 1.1.1](https://www.openssl.org/source) and [zlib 1.2.11](http://www.zlib.net/). Note that, other versions of openssl and zlib may also work well, but they need to be verified.

Source code and corresponding library files (based on Ubuntu 20.04) of the above three open source projects are already included. 

