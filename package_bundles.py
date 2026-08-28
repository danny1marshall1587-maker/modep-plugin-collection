import os
import shutil
import tarfile
import zipfile

def package_plugin_bundles():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    plugins_dir = os.path.join(base_dir, 'plugins')
    dist_dir = os.path.join(base_dir, 'dist')
    os.makedirs(dist_dir, exist_ok=True)

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

    print("================================================================")
    print("       Packaging MODEP Bundles for MODEP Cloud / Store          ")
    print("================================================================")

    for p_name, bundle_name in plugins:
        src_p = os.path.join(plugins_dir, p_name)
        target_bundle = os.path.join(dist_dir, bundle_name)
        
        # Clean and create bundle folder
        if os.path.exists(target_bundle):
            shutil.rmtree(target_bundle)
        os.makedirs(target_bundle, exist_ok=True)

        for root, dirs, files in os.walk(src_p):
            rel = os.path.relpath(root, src_p)
            if 'src' in rel.split(os.sep):
                continue
            dest_dir = os.path.join(target_bundle, rel) if rel != '.' else target_bundle
            os.makedirs(dest_dir, exist_ok=True)
            for f in files:
                if f.endswith(('.ttl', '.so', '.dll', '.png', '.html', '.css', '.js', '.ttf', '.txt')):
                    shutil.copy2(os.path.join(root, f), os.path.join(dest_dir, f))

        print(f"Created bundle: dist/{bundle_name}")

    # Create All-in-one ZIP and TAR.GZ for MODEP Cloud distribution
    zip_path = os.path.join(base_dir, 'modep_guitar_plugins_bundle.zip')
    tar_path = os.path.join(base_dir, 'modep_guitar_plugins_bundle.tar.gz')

    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(dist_dir):
            for f in files:
                full_path = os.path.join(root, f)
                rel_path = os.path.relpath(full_path, dist_dir)
                zf.write(full_path, rel_path)
    print(f"Generated: {os.path.basename(zip_path)}")

    with tarfile.open(tar_path, 'w:gz') as tf:
        for root, dirs, files in os.walk(dist_dir):
            for f in files:
                full_path = os.path.join(root, f)
                rel_path = os.path.relpath(full_path, dist_dir)
                tf.add(full_path, arcname=rel_path)
    print(f"Generated: {os.path.basename(tar_path)}")

    print("\nPackage generation complete! Ready for MODEP Cloud & Patchbox OS.")

if __name__ == '__main__':
    package_plugin_bundles()
