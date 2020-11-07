UNAME=$(shell uname -s)
ifeq ($(UNAME),FreeBSD)
LIBS=-lutil
endif


all: kcovlib32.so kcovlib64.so kcovtrace32 kcovtrace64 sortuniq

sortuniq: sortuniq.cc
	g++ -O2 -o $@ $<

kcovtrace32: kcovtrace.c
	gcc -Wall -o $@ $<

kcovtrace64: kcovtrace.c
	gcc -DKERNEL64 -Wall -o $@ $<

kcovlib32.so: kcov-lib.cc
	g++ -Wall -fPIC -shared -o $@ $<  -ldl $(LIBS)

kcovlib64.so: kcov-lib.cc
	g++ -DKERNEL64 -Wall -fPIC -shared -o $@ $<  -ldl $(LIBS)

clean:
	rm sortuniq kcov*.so kcovtrace32 kcovtrace64

.PHONY: all clean
