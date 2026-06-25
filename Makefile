CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude
SRCDIR   = src

# Build both binaries by default
all: mini-redis-server mini-redis-client

# Server: link all .cpp files except client.cpp
mini-redis-server: $(SRCDIR)/server.cpp $(SRCDIR)/store.cpp $(SRCDIR)/parser.cpp $(SRCDIR)/persistence.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Client: only needs client.cpp
mini-redis-client: $(SRCDIR)/client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f mini-redis-server mini-redis-client dump.rdb

.PHONY: all clean
