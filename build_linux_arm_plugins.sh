#!/usr/bin/env bash
# ==============================================================================
# build_linux_arm_plugins.sh
# Native & Cross-Compilation Build Script for Patchbox OS / MODEP (ARM32 / ARM64)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ARCH=$(uname -m)
echo "================================================================"
echo "   Building MODEP LV2 Plugins for Linux / ARM ($ARCH)           "
echo "================================================================"

CXX="${CXX:-g++}"
CXXFLAGS="-O3 -fPIC -shared -static-libstdc++ -static-libgcc -I plugins/include"

PLUGINS=(
  "harmonic-tremolo:src/harmonic_tremolo_lv2.cpp:harmonic_tremolo.so:harmonic-tremolo.lv2"
  "cyber-denoiser:src/cyber_denoiser_lv2.cpp:cyber_denoiser.so:cyber-denoiser.lv2"
  "galaxy-strobe-tune:src/galaxy_strobe_tune_lv2.cpp:galaxy_strobe_tune.so:galaxy-strobe-tune.lv2"
  "dimension-c:src/dimension_c_lv2.cpp:dimension_c.so:dimension-c.lv2"
  "Dimension_IV.lv2:src/dimension_iv_lv2.cpp:Dimension_IV_dsp.so:Dimension_IV.lv2"
  "Dearmondo610.lv2:src/dearmondo610_lv2.cpp:DeArmondo610_dsp.so:Dearmondo610.lv2"
  "guitar-midi:src/guitar_midi_lv2.cpp:guitar_midi.so:guitar-midi.lv2"
  "bluesbreaker.lv2:src/bluesbreaker_lv2.cpp:bluesbreaker.so:bluesbreaker.lv2"
  "aether.lv2:src/aether_lv2.cpp:aether_dsp.so:aether.lv2"
  "nam-loader.lv2:src/nam_loader_lv2.cpp:nam_loader.so:nam-loader.lv2"
  "cyber-hum-killer.lv2:src/cyber_hum_killer_lv2.cpp:cyber_hum_killer.so:cyber-hum-killer.lv2"
  "smart-fizz-killer.lv2:src/smart_fizz_killer_lv2.cpp:smart_fizz_killer.so:smart-fizz-killer.lv2"
)

mkdir -p dist

for entry in "${PLUGINS[@]}"; do
  IFS=":" read -r pdir psrc pbin bname <<< "$entry"
  echo "[*] Compiling $pdir -> $pbin..."
  $CXX $CXXFLAGS -I "plugins/$pdir/src" "plugins/$pdir/$psrc" -o "plugins/$pdir/$pbin"
  echo " [+] SUCCESS: plugins/$pdir/$pbin"

  # Copy into dist bundle
  target_b="dist/$bname"
  mkdir -p "$target_b"
  cp -r plugins/$pdir/*.ttl "$target_b/" 2>/dev/null || true
  cp -r plugins/$pdir/*.so "$target_b/" 2>/dev/null || true
  if [ -d "plugins/$pdir/modgui" ]; then
    cp -r "plugins/$pdir/modgui" "$target_b/"
  fi
done

echo ""
echo "[*] Packaging dist/modep_plugins_${ARCH}.tar.gz..."
cd dist
tar -czvf "modep_plugins_${ARCH}.tar.gz" *.lv2
cd ..

echo "================================================================"
echo " [OK] All plugins compiled for ARM / Linux ($ARCH) successfully!"
echo " Deploy to Patchbox OS by copying dist/*.lv2 to /var/modep/lv2/  "
echo "================================================================"
