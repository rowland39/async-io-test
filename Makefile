CFLAGS=
LDFLAGS=
CC=gcc
CPP=g++

.PHONY: all
all: async-io-test

async-io-test: async-io-test.o async-file-writer.o
	$(CPP) -o $@ $^ $(LDFLAGS) -lrt

async-io-test.o: async-io-test.cc
	$(CPP) -c $< $(CFLAGS)

async-file-writer.o: async-file-writer.cc
	$(CPP) -c $< $(CFLAGS)

clean:
	rm -f *.o async-io-test
