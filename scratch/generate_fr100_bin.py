import struct
import unreal

npy_path = r"C:\Users\vlvkr\OneDrive\Desktop\Ship Wave\Python Bake\golden_images\golden_Fr1.00_elevation.npy"

with open(npy_path, "rb") as f:
    f.seek(128) # skip header
    data = f.read()

count = len(data) // 4 # float32 (f4)
floats = struct.unpack(f"<{count}f", data)
unreal.log_warning(f"Fr1.00 Float32 Count={len(floats)}, Min={min(floats):.4f}, Max={max(floats):.4f}")

out_path = "c:/Unreal Projects/ArtisticSW2026/Content/New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr100_fp16.bin"
packed_fp16 = struct.pack(f"<{count}e", *floats)
with open(out_path, "wb") as out_f:
    out_f.write(packed_fp16)
unreal.log_warning(f"Successfully generated {out_path} ({len(packed_fp16)} bytes)")
