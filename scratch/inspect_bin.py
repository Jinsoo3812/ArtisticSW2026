import struct
import numpy as np

bin_path = "c:/Unreal Projects/ArtisticSW2026/Content/New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr050_fp16.bin"
with open(bin_path, "rb") as f:
    data = f.read()

# 512 (U) x 256 (V) float16
arr = np.frombuffer(data, dtype=np.float16).reshape((512, 256))
print(f"Loaded Golden Bin: shape={arr.shape}, min={arr.min():.4f}, max={arr.max():.4f}, mean={arr.mean():.4f}")

# Check central slice along U (downstream)
print("U=0 (Apex):", arr[0, 120:136])
print("U=50:", arr[50, 120:136])
print("U=100:", arr[100, 120:136])
