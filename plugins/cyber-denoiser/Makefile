BUNDLE = cyber-denoiser.lv2
TARGET = $(BUNDLE)/cyber_denoiser.so

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -fPIC -Wall -Wextra -I../include -I./src
LDFLAGS ?= -shared -lm

SOURCES = src/cyber_denoiser_lv2.cpp
HEADERS = src/CyberDenoiserEngine.hpp ../include/lv2/lv2.h

all: $(TARGET) bundle

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p $(BUNDLE)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $(TARGET)

bundle: $(TARGET)
	@mkdir -p $(BUNDLE)/modgui
	@cp -f manifest.ttl $(BUNDLE)/
	@cp -f cyber-denoiser.ttl $(BUNDLE)/
	@cp -f modgui.ttl $(BUNDLE)/
	@cp -rf modgui/* $(BUNDLE)/modgui/ 2>/dev/null || true

clean:
	rm -rf $(BUNDLE)

.PHONY: all bundle clean
