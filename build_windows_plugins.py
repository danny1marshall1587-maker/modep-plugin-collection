#!/usr/bin/env python3
"""
build_windows_plugins.py
Compiles custom LV2 plugins into native 64-bit Windows dynamic libraries (.dll)
using the portable GCC toolchain and deploys them into the MODEP Desktop LV2 library.
"""

import os
import sys
import subprocess
import shutil

def build_and_deploy():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    gpp = os.path.join(base_dir, 'tools', 'w64devkit', 'bin', 'g++.exe')

    if not os.path.exists(gpp):
        print(f"[!] Error: Compiler not found at {gpp}")
        return False

    plugins = [
        ('harmonic-tremolo', 'src/harmonic_tremolo_lv2.cpp', 'harmonic_tremolo.dll'),
        ('cyber-denoiser', 'src/cyber_denoiser_lv2.cpp', 'cyber_denoiser.dll'),
        ('galaxy-strobe-tune', 'src/galaxy_strobe_tune_lv2.cpp', 'galaxy_strobe_tune.dll'),
        ('dimension-c', 'src/dimension_c_lv2.cpp', 'dimension_c.dll'),
        ('Dimension_IV.lv2', 'src/dimension_iv_lv2.cpp', 'Dimension_IV_dsp.dll'),
        ('Dearmondo610.lv2', 'src/dearmondo610_lv2.cpp', 'DeArmondo610_dsp.dll'),
        ('guitar-midi', 'src/guitar_midi_lv2.cpp', 'guitar_midi.dll'),
        ('bluesbreaker.lv2', 'src/bluesbreaker_lv2.cpp', 'bluesbreaker.dll'),
        ('aether.lv2', 'src/aether_lv2.cpp', 'aether_dsp.dll'),
        ('nam-loader.lv2', 'src/nam_loader_lv2.cpp', 'nam_loader.dll'),
        ('cyber-hum-killer.lv2', 'src/cyber_hum_killer_lv2.cpp', 'cyber_hum_killer.dll'),
        ('smart-fizz-killer.lv2', 'src/smart_fizz_killer_lv2.cpp', 'smart_fizz_killer.dll')
    ]

    print("================================================================")
    print("      Building Custom LV2 Plugins for Windows 64-bit            ")
    print("================================================================")

    inc_dir = os.path.join(base_dir, 'plugins', 'include')

    compiler_bin = os.path.join(base_dir, 'tools', 'w64devkit', 'bin')
    env = os.environ.copy()
    env['PATH'] = compiler_bin + os.pathsep + env.get('PATH', '')

    for p_dir, src_rel, dll_name in plugins:
        work_dir = os.path.join(base_dir, 'plugins', p_dir)
        src_dir = os.path.join(work_dir, 'src')
        src_full = os.path.join(work_dir, src_rel)
        out_dll = os.path.join(work_dir, dll_name)

        cmd = [
            gpp,
            '-O3',
            '-shared',
            '-static',
            '-static-libgcc',
            '-static-libstdc++',
            f'-I{inc_dir}',
            f'-I{src_dir}',
            src_full,
            '-o',
            out_dll
        ]

        print(f"[*] Compiling {p_dir}...")
        res = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if res.returncode != 0:
            print(f" [!] ERROR compiling {p_dir}:")
            print(res.stderr)
            return False
        else:
            dll_size = os.path.getsize(out_dll)
            print(f" [+] SUCCESS: {dll_name} ({dll_size:,} bytes)")

    print("\nAll plugin binaries compiled successfully!")

    # Automatically deploy to desktop LV2 directory
    deploy_script = os.path.join(base_dir, 'deploy_plugins_to_desktop.py')
    if os.path.exists(deploy_script):
        print("\nDeploying compiled plugins into MODEP Desktop LV2 library...")
        subprocess.run([sys.executable, deploy_script], check=True, cwd=base_dir)

    return True

if __name__ == '__main__':
    build_and_deploy()
