CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread -Ithird_party
# Static C++ runtime: the binary only needs glibc, so it runs on a plain debian:12-slim image.
LDFLAGS  ?= -static-libstdc++ -static-libgcc
SRC      := $(wildcard src/*.cpp)
BIN      := sgw-proxy

$(BIN): $(SRC) $(wildcard src/*.hpp)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: clean
