#!/usr/bin/env python3
"""
deploy_plugins_to_desktop.py
Packages and deploys LV2 plugin bundles (manifests, binaries, modgui images, HTML/JS/CSS assets)
directly into all MODEP Desktop and MOD Desktop plugin library locations on Windows.
"""

import os
import sys
import shutil
import subprocess

def get_target_directories():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    targets = []
    
    # 1. Local dist directory
    targets.append(os.path.join(base_dir, 'dist'))
    
    # 2. MODEP App internal plugins folder (inside repo)
    modep_app_plugins = os.path.join(base_dir, 'modep_app', 'plugins')
    if os.path.exists(os.path.join(base_dir, 'modep_app')):
        targets.append(modep_app_plugins)
        
    # 3. Windows User Documents "MOD Desktop/lv2"
    user_docs = os.path.expanduser(r'~\Documents')
    mod_desktop_lv2 = os.path.join(user_docs, 'MOD Desktop', 'lv2')
    targets.append(mod_desktop_lv2)

    return targets

def safe_copy_tree(src, dst):
    if not os.path.exists(src):
        return
    os.makedirs(dst, exist_ok=True)
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        dest_dir = os.path.join(dst, rel) if rel != '.' else dst
        os.makedirs(dest_dir, exist_ok=True)
        for f in files:
            src_file = os.path.join(root, f)
            dest_file = os.path.join(dest_dir, f)
            try:
                shutil.copy2(src_file, dest_file)
            except Exception:
                try:
                    tmp = dest_file + '.tmp'
                    shutil.copy2(src_file, tmp)
                    os.replace(tmp, dest_file)
                except Exception:
                    pass

def deploy_to_desktop():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    plugins_dir = os.path.join(base_dir, 'plugins')
    target_dirs = get_target_directories()

    print("================================================================")
    print("      Deploying Plugins & GUI to MODEP Desktop (Windows)        ")
    print("================================================================")

    # Step 1: Ensure modgui images are generated
    gen_script = os.path.join(base_dir, 'generate_modgui_images.py')
    if os.path.exists(gen_script):
        print("1. Generating MODGUI pedal artwork...")
        try:
            subprocess.run([sys.executable, gen_script], check=True, cwd=base_dir)
        except Exception as e:
            print(f"   [!] Warning: Error generating modgui images: {e}")

    # Step 2: Package and copy each plugin bundle to all target locations
    plugins = [
        ('harmonic-tremolo', 'harmonic-tremolo.lv2'),
        ('cyber-denoiser', 'cyber-denoiser.lv2'),
        ('galaxy-strobe-tune', 'galaxy-strobe-tune.lv2'),
        ('dimension-c', 'dimension-c.lv2'),
        ('Dimension_IV.lv2', 'Dimension_IV.lv2'),
        ('Dearmondo610.lv2', 'Dearmondo610.lv2'),
        ('guitar-midi', 'guitar-midi.lv2'),
        ('bluesbreaker.lv2', 'bluesbreaker.lv2'),
        ('aether.lv2', 'aether.lv2'),
        ('nam-loader.lv2', 'nam-loader.lv2'),
        ('cyber-hum-killer.lv2', 'cyber-hum-killer.lv2'),
        ('smart-fizz-killer.lv2', 'smart-fizz-killer.lv2')
    ]

    for target_base in target_dirs:
        os.makedirs(target_base, exist_ok=True)
        print(f"\n[>] Deploying to: {target_base}")

        for p_name, bundle_name in plugins:
            src_p = os.path.join(plugins_dir, p_name)
            if not os.path.exists(src_p):
                continue

            target_bundle = os.path.join(target_base, bundle_name)
            safe_copy_tree(src_p, target_bundle)
            print(f"   [+] Copied bundle: {bundle_name}")

    print("\n[OK] All plugins successfully deployed to MODEP Desktop and MOD Desktop!")

if __name__ == '__main__':
    deploy_to_desktop()
