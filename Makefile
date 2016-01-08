UNAME_S := $(shell uname -s)
CFLAGS=

ifeq ($(UNAME_S),Linux)
    LDFLAGS=-lrt
    CC=gcc
    CPP=g++
endif

ifeq ($(UNAME_S),FreeBSD)
    LDFLAGS=
    CC=cc
    CPP=c++
endif

.PHONY: all
all: async-io-test

async-io-test: async-io-test.o async-file-writer.o
	$(CPP) -o $@ $^ $(LDFLAGS)

async-io-test.o: async-io-test.cc
	$(CPP) -c $< $(CFLAGS)

async-file-writer.o: async-file-writer.cc
	$(CPP) -c $< $(CFLAGS)

clean:
	rm -f *.o async-io-test
