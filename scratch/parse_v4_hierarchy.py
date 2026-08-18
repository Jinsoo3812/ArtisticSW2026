import os
import glob
import re
import csv
import numpy as np

# 1. Parse ProfileGPU with full hierarchy
def parse_profile_gpu_tree(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

    lines = text.splitlines()
    tree = []
    
    for line in lines:
        if "LogRHI: Display:" not in line:
            continue
        
        ms_all = re.findall(r'([0-9]+\.[0-9]+)\s*ms', line)
        if not ms_all:
            continue
            
        last_ms = [m.end() for m in re.finditer(r'ms', line)][-1]
        after = line[last_ms:]
        event_str = re.sub(r'^[^\w\<\/\.\-\s]+', '', after)
        event_str = re.sub(r'[^\w\<\/\.\-\s]+$', '', event_str).rstrip()
        
        lstripped = event_str.lstrip()
        indent = len(event_str) - len(lstripped)
        event_name = lstripped.strip()
        
        if event_name:
            tree.append({
                "indent": indent,
                "name": event_name,
                "ms": float(ms_all[-1]) if len(ms_all) > 1 else float(ms_all[0]),
                "raw": line
            })
    return tree

# 2. Parse all CSV columns
def parse_full_csv(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        data = {h: [] for h in header}
        
        for row in reader:
            if not row or len(row) < len(header): continue
            for i, h in enumerate(header):
                try:
                    data[h].append(float(row[i]))
                except ValueError:
                    pass
    return {k: np.mean(v) for k, v in data.items() if v}

v4_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\V4_Ripple_pixel_gradient"

subdirs = sorted([s for s in glob.glob(os.path.join(v4_dir, "*")) if os.path.isdir(s)])

all_csvs = [parse_full_csv(glob.glob(os.path.join(s, "*.csv"))[0]) for s in subdirs]
all_trees = [parse_profile_gpu_tree(glob.glob(os.path.join(s, "*.[tT][xX][tT]"))[0]) for s in subdirs]

# Print tree of 1st run
print("=== V4 1st Run ProfileGPU Tree (Top 30 items) ===")
for item in all_trees[0][:35]:
    space = "  " * (item["indent"] // 2)
    print(f"{space}{item['name']:<45}: {item['ms']:.3f} ms")

print("\n=== V4 CSV Metrics (Average of 3 Runs) ===")
keys_to_show = [
    "FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime",
    "Exclusive/GameThread/EventWait", "Exclusive/GameThread/UI", "Exclusive/GameThread/TickActors",
    "Exclusive/GameThread/SyncBodies", "Exclusive/GameThread/Tickables",
    "Exclusive/RenderThread/EventWait", "Exclusive/RenderThread/EventWait/Visibility",
    "DrawSceneCommand_StartDelay", "Exclusive/RenderThread/RenderOther",
    "Exclusive/RenderThread/RDG_CollectResources", "Exclusive/RenderThread/RDG",
    "Exclusive/RenderThread/RenderShadows", "Exclusive/RenderThread/RenderLighting",
    "Exclusive/AllWorkers/Physics", "Exclusive/AllWorkers/Slate", "Exclusive/RenderThread/Slate",
    "RHI/DrawCalls", "RHI/PrimitivesDrawn"
]

avg_csv = {}
for k in keys_to_show:
    vals = [c[k] for c in all_csvs if k in c]
    if vals:
        avg_csv[k] = np.mean(vals)
        print(f"  {k:<48}: {avg_csv[k]:.3f} ms" if "Calls" not in k and "Primitives" not in k else f"  {k:<48}: {avg_csv[k]:.1f}")

