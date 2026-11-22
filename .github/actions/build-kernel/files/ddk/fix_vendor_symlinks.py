#!/usr/bin/env python3
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: fix_vendor_symlinks.py <kernel_platform_dir>")
        sys.exit(1)

    kernel_platform = os.path.abspath(sys.argv[1])
    vendor_root = os.path.join(kernel_platform, "vendor")
    if not os.path.isdir(vendor_root):
        print(f"Vendor root {vendor_root} does not exist!")
        sys.exit(1)

    fixed = 0
    for dirpath, dirnames, filenames in os.walk(kernel_platform):
        if dirpath.startswith(vendor_root):
            continue
        for name in dirnames + filenames:
            p = os.path.join(dirpath, name)
            if os.path.islink(p):
                target = os.readlink(p)
                if "vendor/" in target:
                    idx = target.find("vendor/")
                    subpath = target[idx:]
                    dest_abs = os.path.join(kernel_platform, subpath)
                    new_rel = os.path.relpath(dest_abs, dirpath)
                    if new_rel != target:
                        os.unlink(p)
                        os.symlink(new_rel, p)
                        fixed += 1
    print(f"Fixed {fixed} vendor symlinks under {kernel_platform}")

if __name__ == "__main__":
    main()
