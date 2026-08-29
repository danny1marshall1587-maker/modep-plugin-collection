# Makefile for MODEP / Patchbox OS (ARM & Linux)
CXX ?= g++
CXXFLAGS += -O3 -fPIC -shared -static-libstdc++ -static-libgcc -I plugins/include

all: plugins

plugins: harmonic-tremolo cyber-denoiser galaxy-strobe-tune dimension-c dimension-iv dearmondo610 guitar-midi bluesbreaker aether nam-loader cyber-hum-killer smart-fizz-killer cyber-expression-cv aelapse

harmonic-tremolo:
	$(CXX) $(CXXFLAGS) -I plugins/harmonic-tremolo/src plugins/harmonic-tremolo/src/harmonic_tremolo_lv2.cpp -o plugins/harmonic-tremolo/harmonic_tremolo.so

cyber-denoiser:
	$(CXX) $(CXXFLAGS) -I plugins/cyber-denoiser/src plugins/cyber-denoiser/src/cyber_denoiser_lv2.cpp -o plugins/cyber-denoiser/cyber_denoiser.so

galaxy-strobe-tune:
	$(CXX) $(CXXFLAGS) -I plugins/galaxy-strobe-tune/src plugins/galaxy-strobe-tune/src/galaxy_strobe_tune_lv2.cpp -o plugins/galaxy-strobe-tune/galaxy_strobe_tune.so

dimension-c:
	$(CXX) $(CXXFLAGS) -I plugins/dimension-c/src plugins/dimension-c/src/dimension_c_lv2.cpp -o plugins/dimension-c/dimension_c.so

dimension-iv:
	$(CXX) $(CXXFLAGS) -I plugins/Dimension_IV.lv2/src plugins/Dimension_IV.lv2/src/dimension_iv_lv2.cpp -o plugins/Dimension_IV.lv2/Dimension_IV_dsp.so

dearmondo610:
	$(CXX) $(CXXFLAGS) -I plugins/Dearmondo610.lv2/src plugins/Dearmondo610.lv2/src/dearmondo610_lv2.cpp -o plugins/Dearmondo610.lv2/DeArmondo610_dsp.so

guitar-midi:
	$(CXX) $(CXXFLAGS) -I plugins/guitar-midi/src plugins/guitar-midi/src/guitar_midi_lv2.cpp -o plugins/guitar-midi/guitar_midi.so

bluesbreaker:
	$(CXX) $(CXXFLAGS) -I plugins/bluesbreaker.lv2/src plugins/bluesbreaker.lv2/src/bluesbreaker_lv2.cpp -o plugins/bluesbreaker.lv2/bluesbreaker.so

aether:
	$(CXX) $(CXXFLAGS) -I plugins/aether.lv2/src plugins/aether.lv2/src/aether_lv2.cpp -o plugins/aether.lv2/aether_dsp.so

nam-loader:
	$(CXX) $(CXXFLAGS) -I plugins/nam-loader.lv2/src plugins/nam-loader.lv2/src/nam_loader_lv2.cpp -o plugins/nam-loader.lv2/nam_loader.so

cyber-hum-killer:
	$(CXX) $(CXXFLAGS) -I plugins/cyber-hum-killer.lv2/src plugins/cyber-hum-killer.lv2/src/cyber_hum_killer_lv2.cpp -o plugins/cyber-hum-killer.lv2/cyber_hum_killer.so

smart-fizz-killer:
	$(CXX) $(CXXFLAGS) -I plugins/smart-fizz-killer.lv2/src plugins/smart-fizz-killer.lv2/src/smart_fizz_killer_lv2.cpp -o plugins/smart-fizz-killer.lv2/smart_fizz_killer.so

cyber-expression-cv:
	$(CXX) $(CXXFLAGS) -I plugins/cyber-expression-cv.lv2/src plugins/cyber-expression-cv.lv2/src/cyber_expression_cv_lv2.cpp -o plugins/cyber-expression-cv.lv2/cyber_expression_cv.so

aelapse:
	$(CXX) $(CXXFLAGS) -I plugins/aelapse.lv2/src plugins/aelapse.lv2/src/aelapse_lv2.cpp -o plugins/aelapse.lv2/aelapse_dsp.so


install:
	mkdir -p /var/modep/lv2/
	cp -r plugins/*.lv2 /var/modep/lv2/ 2>/dev/null || true
	cp -r plugins/harmonic-tremolo /var/modep/lv2/harmonic-tremolo.lv2 2>/dev/null || true
	cp -r plugins/cyber-denoiser /var/modep/lv2/cyber-denoiser.lv2 2>/dev/null || true
	cp -r plugins/galaxy-strobe-tune /var/modep/lv2/galaxy-strobe-tune.lv2 2>/dev/null || true
	cp -r plugins/dimension-c /var/modep/lv2/dimension-c.lv2 2>/dev/null || true
	cp -r plugins/guitar-midi /var/modep/lv2/guitar-midi.lv2 2>/dev/null || true

clean:
	rm -f plugins/*/*.so
