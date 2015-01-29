CC=gcc
CFLAGS=-std=c99 -Wall -pedantic -O3 -march=native

all:		blake2s blake2b blake2sp blake2bp

blake2s:	blake2s.c
		$(CC) blake2s.c -o $@ $(CFLAGS) -DBLAKE2S_SELFTEST

blake2b:	blake2b.c
		$(CC) blake2b.c -o $@ $(CFLAGS) -DBLAKE2B_SELFTEST

blake2sp:	blake2sp.c blake2s.c
		$(CC) blake2sp.c blake2s.c -o $@ $(CFLAGS) -DBLAKE2SP_SELFTEST

blake2bp:	blake2bp.c blake2b.c
		$(CC) blake2bp.c blake2b.c -o $@ $(CFLAGS) -DBLAKE2BP_SELFTEST

clean:		
		rm -rf *.o blake2s blake2b blake2sp blake2bp
