UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CFLAGS=-std=c++11 -pthread -g
    LDFLAGS=-pthread
    CC=gcc
    CPP=g++
endif

ifeq ($(UNAME_S),FreeBSD)
    CFLAGS=-std=c++11 -pthread
    LDFLAGS=-pthread
    CC=cc
    CPP=c++
endif

.PHONY: all
all: async-io-test sync-io-test async-cp sync-cp

async-io-test: async-io-test.o async-file-writer.o
	$(CPP) -o $@ $^ $(LDFLAGS)

async-io-test.o: async-io-test.cc
	$(CPP) -c $< $(CFLAGS)

async-file-writer.o: async-file-writer.cc
	$(CPP) -c $< $(CFLAGS)

sync-io-test: sync-io-test.o async-file-writer.o
	$(CPP) -o $@ $^ $(LDFLAGS)

sync-io-test.o: sync-io-test.cc
	$(CPP) -c $< $(CFLAGS)

async-cp: async-cp.o async-file-writer.o
	$(CPP) -o $@ $^ $(LDFLAGS)

async-cp.o: async-cp.cc
	$(CPP) -c $< $(CFLAGS)

sync-cp: sync-cp.o async-file-writer.o
	$(CPP) -o $@ $^ $(LDFLAGS)

sync-cp.o: sync-cp.cc
	$(CPP) -c $< $(CFLAGS)

clean:
	rm -f *.o async-io-test sync-io-test async-cp sync-cp test-file.txt
