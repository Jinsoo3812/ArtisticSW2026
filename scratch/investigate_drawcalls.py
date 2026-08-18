import os
import glob
import csv
import numpy as np

def inspect_csv(csv_path):
    with open(csv_path, 'r', encoding='utf-8', errors='ignore') as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        
        # Search for all columns related to Draw, Slate, Mesh, Primitives, View
        interesting = [h for h in header if any(k in h.lower() for k in ["draw", "primitive", "mesh", "slate", "actor", "view", "instanc"])]
        
        indices = {t: header.index(t) for t in interesting}
        data = {t: [] for t in interesting}
        
        for row in reader:
            if not row or len(row) <= max(indices.values()): continue
            for t, idx in indices.items():
                try:
                    data[t].append(float(row[idx]))
                except ValueError:
                    pass
                    
    means = {k: np.mean(v) for k, v in data.items() if v}
    return means

# Compare B-commit vs V3
b_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\Gradient_Kelvin"
v3_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V3_Kelvin_Capacity_and_CPU_Copy"

b_csvs = glob.glob(os.path.join(b_dir, "*", "*.csv"))
v3_csvs = glob.glob(os.path.join(v3_dir, "*", "*.csv"))

b_means = inspect_csv(b_csvs[0])
v3_means = inspect_csv(v3_csvs[0])

print(f"{'Metric':<45} | {'B-Commit':<12} | {'V3':<12} | {'Delta':<12}")
print("-" * 85)

all_keys = sorted(set(list(b_means.keys()) + list(v3_means.keys())))
for k in all_keys:
    b_val = b_means.get(k, 0.0)
    v3_val = v3_means.get(k, 0.0)
    delta = v3_val - b_val
    if abs(b_val) > 0.001 or abs(v3_val) > 0.001:
        # Filter interesting ones
        if any(x in k for x in ["DrawCalls", "Primitives", "Slate", "Instance", "View/Pos", "View/Speed"]):
            print(f"{k:<45} | {b_val:<12.2f} | {v3_val:<12.2f} | {delta:<+12.2f}")
