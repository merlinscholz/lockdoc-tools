#CXXFLAGS += -O2
CXXFLAGS += -g
CXXFLAGS += -std=c++17 -Wall -Wextra $(shell libpng-config --cflags)

LDFLAGS += $(shell libpng-config --ldflags)

sfctool: sfctool.cc
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f sfctool

.PHONY: clean
