import struct
import json
import os
import unreal

SOURCE_DIR = r"C:\Users\vlvkr\OneDrive\Desktop\Ship Wave\Python Bake\golden_images"
DEST_DIR = r"c:\Unreal Projects\ArtisticSW2026\Content\New\Water\Realistic_Water\Kelvin"
os.makedirs(DEST_DIR, exist_ok=True)

PROFILES = [
    ("Fr0.30", "fr030", 0.031517344625400685),
    ("Fr0.50", "fr050", 1.387197138296635),
    ("Fr0.70", "fr070", 3.9109640445285847),
    ("Fr1.00", "fr100", 9.651471204792845),
]

for src_tag, dst_tag, peak in PROFILES:
    npy_file = os.path.join(SOURCE_DIR, f"golden_{src_tag}_elevation.npy")
    if not os.path.exists(npy_file):
        unreal.log_error(f"Missing source: {npy_file}")
        continue
        
    with open(npy_file, "rb") as f:
        f.seek(128) # skip 128-byte NPY header
        raw_bytes = f.read()
        
    count = len(raw_bytes) // 4
    floats = struct.unpack(f"<{count}f", raw_bytes)
    
    # Normalize by peak so values are in [-1.0, 1.0]
    inv_peak = 1.0 / peak if peak > 0 else 1.0
    normalized_floats = [x * inv_peak for x in floats]
    
    out_bin = os.path.join(DEST_DIR, f"kelvin_wake_golden_{dst_tag}_fp16.bin")
    packed_fp16 = struct.pack(f"<{count}e", *normalized_floats)
    
    with open(out_bin, "wb") as out_f:
        out_f.write(packed_fp16)
        
    unreal.log_warning(
        f"[Bake] {src_tag} -> {dst_tag}: count={count}, raw_peak={peak:.4f}, "
        f"norm_min={min(normalized_floats):.4f}, norm_max={max(normalized_floats):.4f}, "
        f"bytes={len(packed_fp16)} saved to {out_bin}"
    )

unreal.log_warning("[Bake] All 4 Golden Image FP16 Normalized Binaries Generated Successfully!")
