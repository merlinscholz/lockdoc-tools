UNAME=$(shell uname -s)
ifeq ($(UNAME),FreeBSD)
LIBS=-lutil
endif


all: kcovlib.so kcovtrace sortuniq

sortuniq: sortuniq.cc
	g++ -O2 -o $@ $<

kcovtrace: kcovtrace.c
	gcc -Wall -o $@ $<

kcovlib.so: kcov-lib.cc
	g++ -Wall -fPIC -shared -o $@ $<  -ldl $(LIBS)

clean:
	rm -f sortuniq kcov*.so kcovtrace

.PHONY: all clean
