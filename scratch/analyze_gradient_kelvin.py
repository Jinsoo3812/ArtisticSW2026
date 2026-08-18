import os
import glob
import csv
import numpy as np

DATA_DIR = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\Gradient_Kelvin"

subdirs = sorted(glob.glob(os.path.join(DATA_DIR, "*")))

results = []

for s in subdirs:
    if not os.path.isdir(s):
        continue
    csvs = glob.glob(os.path.join(s, "*.csv"))
    if not csvs:
        continue
    csv_file = csvs[0]
    folder_name = os.path.basename(s)
    
    with open(csv_file, 'r', encoding='utf-8', errors='ignore') as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        
        col_indices = {}
        for target in ["FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHI/DrawCalls"]:
            if target in header:
                col_indices[target] = header.index(target)
                
        data = {k: [] for k in col_indices}
        for row in reader:
            if not row or len(row) <= max(col_indices.values()):
                continue
            for k, idx in col_indices.items():
                try:
                    val = float(row[idx])
                    data[k].append(val)
                except ValueError:
                    pass

    metrics = {"Folder": folder_name, "CSV": os.path.basename(csv_file), "Frames": len(data.get("FrameTime", []))}
    if "FrameTime" in data and data["FrameTime"]:
        arr_ft = np.array(data["FrameTime"])
        fps_arr = 1000.0 / arr_ft
        metrics["FrameTime_Mean"] = np.mean(arr_ft)
        metrics["FrameTime_Std"] = np.std(arr_ft)
        metrics["FrameTime_Med"] = np.median(arr_ft)
        metrics["FPS_Mean"] = np.mean(fps_arr)
        metrics["FPS_Med"] = np.median(fps_arr)
        metrics["FPS_P5"] = np.percentile(fps_arr, 5)
        
    if "GPUTime" in data and data["GPUTime"]:
        arr_gpu = np.array(data["GPUTime"])
        metrics["GPUTime_Mean"] = np.mean(arr_gpu)
        metrics["GPUTime_Std"] = np.std(arr_gpu)
        metrics["GPUTime_Med"] = np.median(arr_gpu)

    if "GameThreadTime" in data and data["GameThreadTime"]:
        metrics["GameThreadTime_Mean"] = np.mean(data["GameThreadTime"])

    if "RenderThreadTime" in data and data["RenderThreadTime"]:
        metrics["RenderThreadTime_Mean"] = np.mean(data["RenderThreadTime"])
        
    if "RHI/DrawCalls" in data and data["RHI/DrawCalls"]:
        metrics["DrawCalls_Mean"] = np.mean(data["RHI/DrawCalls"])
        
    results.append(metrics)

print(f"{'Folder':<10} | {'Frames':<7} | {'FPS Mean':<10} | {'FPS P5':<10} | {'FrameTime':<12} | {'GPUTime':<12} | {'DrawCalls':<10}")
print("-" * 85)
for r in results:
    print(f"{r['Folder']:<10} | {r['Frames']:<7} | {r['FPS_Mean']:<10.2f} | {r['FPS_P5']:<10.2f} | {r['FrameTime_Mean']:<6.2f} (±{r['FrameTime_Std']:.2f}) | {r['GPUTime_Mean']:<6.2f} (±{r['GPUTime_Std']:.2f}) | {r.get('DrawCalls_Mean', 0):<10.1f}")

avg_frames = sum(r['Frames'] for r in results)
avg_fps = np.mean([r['FPS_Mean'] for r in results])
avg_fps_p5 = np.mean([r['FPS_P5'] for r in results])
avg_ft = np.mean([r['FrameTime_Mean'] for r in results])
avg_gpu = np.mean([r['GPUTime_Mean'] for r in results])

print("-" * 85)
print(f"3-Run Total Frames: {avg_frames}")
print(f"3-Run Average FPS: {avg_fps:.2f} FPS")
print(f"3-Run Average FPS (p5): {avg_fps_p5:.2f} FPS")
print(f"3-Run Average FrameTime: {avg_ft:.2f} ms")
print(f"3-Run Average GPUTime: {avg_gpu:.2f} ms")
