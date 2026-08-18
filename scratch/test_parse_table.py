import os
import glob
import re

p = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Pure\1회차\Log.txt"

def parse_gpu_table(filepath):
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        
    in_graphics = False
    results = {}
    
    for line in lines:
        if "GPU Profile for Frame" in line and "Graphics 0 - GPU 0" in line:
            in_graphics = True
            continue
        if in_graphics and "LogStats: Single Frame" in line:
            in_graphics = False
            break
            
        if in_graphics and "┃" in line:
            parts = [p.strip() for p in line.split("┃")]
            # parts: ['', exclusive_cols, inclusive_cols, event_name, '']
            if len(parts) >= 4:
                inclusive_part = parts[2]
                event_name = parts[3].strip()
                # find time at the end of inclusive_part: e.g. "73.1% ┊ █████▊   │ 11.916 ms"
                m = re.search(r"([\d\.]+)\s*ms\s*$", inclusive_part)
                if m:
                    time_ms = float(m.group(1))
                    results[event_name] = time_ms
                    
    return results

print(parse_gpu_table(p))
