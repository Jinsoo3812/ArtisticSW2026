import struct
import unreal

bin_path = "c:/Unreal Projects/ArtisticSW2026/Content/New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr050_fp16.bin"
with open(bin_path, "rb") as f:
    raw = f.read()

unreal.log_warning(f"File size: {len(raw)} bytes (expected: {512*256*2} = {131072*2})")

# Unpack float16 using struct 'e' format
floats = struct.unpack(f"<{len(raw)//2}e", raw)
min_v = min(floats)
max_v = max(floats)
avg_v = sum(floats) / len(floats)
unreal.log_warning(f"Stats: count={len(floats)}, min={min_v:.4f}, max={max_v:.4f}, avg={avg_v:.4f}")

# Sample some values at Apex (U=0) and Downstream
# Structure: 512 rows (U), 256 cols (V)
apex_slice = [floats[0 * 256 + v] for v in range(120, 136)]
unreal.log_warning(f"Apex slice (U=0, V=120..135): {[round(x, 4) for x in apex_slice]}")

mid_slice = [floats[100 * 256 + v] for v in range(120, 136)]
unreal.log_warning(f"Mid slice (U=100, V=120..135): {[round(x, 4) for x in mid_slice]}")
