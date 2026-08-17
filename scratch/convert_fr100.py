import struct
import unreal

npy_path = r"C:\Users\vlvkr\OneDrive\Desktop\Ship Wave\Python Bake\golden_images\golden_Fr1.00_elevation.npy"

# Read .npy file manually
with open(npy_path, "rb") as f:
    header = f.read(128)
    # Check shape in header
    unreal.log_warning(f"NPY Header: {header[:80]}")
    f.seek(128) # standard 128 byte header for simple npy
    data = f.read()

unreal.log_warning(f"Raw data bytes: {len(data)}")
# Convert to float64 or float32 array
# Let's check size
count = len(data) // 8 # if float64
unreal.log_warning(f"Count if float64: {count} (expected 512*256={512*256})")
if count == 512 * 256:
    floats = struct.unpack(f"<{count}d", data)
    unreal.log_warning(f"Float64 Min: {min(floats)}, Max: {max(floats)}")
    
    # Save as float16 bin
    out_path = "c:/Unreal Projects/ArtisticSW2026/Content/New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr100_fp16.bin"
    packed_fp16 = struct.pack(f"<{count}e", *floats)
    with open(out_path, "wb") as out_f:
        out_f.write(packed_fp16)
    unreal.log_warning(f"Successfully wrote {len(packed_fp16)} bytes to {out_path}")
