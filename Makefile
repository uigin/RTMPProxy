
INC=-I./librtmp-2.3/build/include -I./openssl-1.0.2u/build/include -I./zlib-1.2.11/build/include
LIB=-L./librtmp-2.3/build/lib -L./openssl-1.0.2u/build/lib -L./zlib-1.2.11/build/lib

all:
	@$(CROSS_COMPILE)gcc rtmp_proxy.c -o rtmp_proxy $(INC) $(LIB) -lrtmp -lssl -lcrypto -lpthread -lz
clean:
	@rm rtmp_proxy
