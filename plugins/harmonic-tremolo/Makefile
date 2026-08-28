BUNDLE = harmonic-tremolo.lv2
TARGET = $(BUNDLE)/harmonic_tremolo.so

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -fPIC -Wall -Wextra -I../include -I./src
LDFLAGS ?= -shared -lm

SOURCES = src/harmonic_tremolo_lv2.cpp
HEADERS = src/HarmonicTremoloEngine.hpp ../include/lv2/lv2.h

all: $(TARGET) bundle

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p $(BUNDLE)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $(TARGET)

bundle: $(TARGET)
	@mkdir -p $(BUNDLE)/modgui
	@cp -f manifest.ttl $(BUNDLE)/
	@cp -f harmonic-tremolo.ttl $(BUNDLE)/
	@cp -f modgui.ttl $(BUNDLE)/
	@cp -rf modgui/* $(BUNDLE)/modgui/ 2>/dev/null || true

clean:
	rm -rf $(BUNDLE)

.PHONY: all bundle clean
