cmake_minimum_required(VERSION 3.10)
project(ModepGuitarPlugins CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# 1. Harmonic Tremolo
add_library(harmonic_tremolo SHARED
    harmonic-tremolo/src/harmonic_tremolo_lv2.cpp
    harmonic-tremolo/src/HarmonicTremoloEngine.hpp
)
target_include_directories(harmonic_tremolo PRIVATE harmonic-tremolo/src)
set_target_properties(harmonic_tremolo PROPERTIES
    PREFIX ""
    OUTPUT_NAME "harmonic_tremolo"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/harmonic-tremolo.lv2"
)

# 2. Cyber-Denoiser PRO
add_library(cyber_denoiser SHARED
    cyber-denoiser/src/cyber_denoiser_lv2.cpp
    cyber-denoiser/src/CyberDenoiserEngine.hpp
)
target_include_directories(cyber_denoiser PRIVATE cyber-denoiser/src)
set_target_properties(cyber_denoiser PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cyber_denoiser"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cyber-denoiser.lv2"
)

# 3. Galaxy Strobe Tuner
add_library(galaxy_strobe_tune SHARED
    galaxy-strobe-tune/src/galaxy_strobe_tune_lv2.cpp
    galaxy-strobe-tune/src/StrobeTunerCore.hpp
)
target_include_directories(galaxy_strobe_tune PRIVATE galaxy-strobe-tune/src)
set_target_properties(galaxy_strobe_tune PROPERTIES
    PREFIX ""
    OUTPUT_NAME "galaxy_strobe_tune"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/galaxy-strobe-tune.lv2"
)

# Test Suite
add_executable(test_all_plugins tests/test_all_plugins.cpp)
