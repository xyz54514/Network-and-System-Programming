CC := gcc
BASE_CFLAGS := -Wall -Wextra -Wpedantic -std=c11
LIBS := -L. -lcommon

.PHONY: all debug release clean

all: libcommon.so server client

libcommon.so: common.c common.h
	$(CC) $(CFLAGS) -fPIC -shared common.c -o $@

server: server.c common.h libcommon.so
	$(CC) $(CFLAGS) server.c -o $@ $(LIBS)

client: client.c common.h libcommon.so
	$(CC) $(CFLAGS) client.c -o $@ $(LIBS)

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -g -O0 -DENABLE_DEBUG" all

release:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -O2" all

clean:
	rm -f server client libcommon.so *.o *.pcap
