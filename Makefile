# ==============================================================================
# Makefile for Thava Compiler (thavac)
# Supports Linux and Unix environments
# ==============================================================================

CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wno-unused-parameter -Isrc
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

TARGET = thavac
SRC = src/thavac.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)
	chmod +x $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe _thava_temp.cpp

.PHONY: all install uninstall clean
