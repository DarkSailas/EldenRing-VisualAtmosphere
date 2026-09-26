import os
import subprocess
import sys
import shutil

DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(DIR, "build")
OUT_DLL = os.path.join(DIR, "VisualAtmosphere.dll")

os.makedirs(BUILD_DIR, exist_ok=True)

c_sources = [
    os.path.join(DIR, "Minhook", "buffer.c"),
    os.path.join(DIR, "Minhook", "hde64.c"),
    os.path.join(DIR, "Minhook", "hook.c"),
    os.path.join(DIR, "Minhook", "trampoline.c"),
]

cpp_sources = [
    os.path.join(DIR, "VisualAtmosphere.cpp"),
    os.path.join(DIR, "Overlay.cpp"),
    os.path.join(DIR, "dllmain.cpp"),
]

obj_files = []

# Compile C files
for src in c_sources:
    obj_name = os.path.splitext(os.path.basename(src))[0] + ".o"
    obj = os.path.join(BUILD_DIR, obj_name)
    cmd = [
        "gcc", "-O3", "-c", src, "-o", obj,
        f"-I{DIR}",
        f"-I{os.path.join(DIR, 'Minhook')}",
    ]
    print(f"  [C]   {os.path.basename(src)}")
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print("Failed C:", src)
        sys.exit(1)
    obj_files.append(obj)

# Compile C++ files
for src in cpp_sources:
    obj_name = os.path.splitext(os.path.basename(src))[0] + ".o"
    obj = os.path.join(BUILD_DIR, obj_name)
    cmd = [
        "g++", "-O3", "-std=c++20", "-c", src, "-o", obj,
        f"-I{DIR}",
        f"-I{os.path.join(DIR, 'Minhook')}",
        f"-I{os.path.join(DIR, 'Ini')}",
    ]
    print(f"  [CPP] {os.path.basename(src)}")
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print("Failed CPP:", src)
        sys.exit(1)
    obj_files.append(obj)

# Link
link_cmd = [
    "g++", "-O3", "-shared",
    "-o", OUT_DLL,
    *obj_files,
    "-static", "-static-libgcc", "-static-libstdc++",
    "-s",  # Strip symbols
    "-lkernel32", "-luser32", "-lcomctl32", "-ldwmapi", "-lgdi32",
]
print("\nLinking VisualAtmosphere.dll...")
res = subprocess.run(link_cmd)
if res.returncode != 0:
    print("Linking failed!")
    sys.exit(1)

print(f"SUCCESS: Built {OUT_DLL} ({os.path.getsize(OUT_DLL)} bytes)")

rel_dll = os.path.join(DIR, "release", "VisualAtmosphere.dll")
if os.path.exists(os.path.dirname(rel_dll)):
    shutil.copy2(OUT_DLL, rel_dll)
    print(f"Updated {rel_dll}")
