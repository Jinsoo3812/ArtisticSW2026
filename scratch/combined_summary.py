import os
import glob
import csv
import numpy as np

csv_dir = r"C:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV"
csv_files = sorted(glob.glob(os.path.join(csv_dir, "*.csv")), key=os.path.getmtime)[-5:]

all_fps = []
all_ft = []
all_gpu = []
all_gt = []
all_rt = []

for f in csv_files:
    with open(f, 'r', encoding='utf-8', errors='ignore') as fp:
        reader = csv.reader(fp)
        header = [h.strip() for h in next(reader)]
        targets = ["FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime"]
        indices = {t: header.index(t) for t in targets if t in header}
        for row in reader:
            if not row or len(row) <= max(indices.values()): continue
            try:
                ft = float(row[indices["FrameTime"]])
                if ft > 0:
                    all_ft.append(ft)
                    all_fps.append(1000.0 / ft)
                if "GPUTime" in indices:
                    all_gpu.append(float(row[indices["GPUTime"]]))
                if "GameThreadTime" in indices:
                    all_gt.append(float(row[indices["GameThreadTime"]]))
                if "RenderThreadTime" in indices:
                    all_rt.append(float(row[indices["RenderThreadTime"]]))
            except ValueError:
                pass

print("=== 5-Run Combined Overall Summary ===")
print(f"Total Recorded Frames: {len(all_ft)}")
print(f"Overall Average FPS: {np.mean(all_fps):.2f} FPS (Median: {np.median(all_fps):.2f}, P5: {np.percentile(all_fps, 5):.2f})")
print(f"Overall Average FrameTime: {np.mean(all_ft):.2f} ms (Std: {np.std(all_ft):.2f})")
print(f"Overall Average GPUTime: {np.mean(all_gpu):.2f} ms (Std: {np.std(all_gpu):.2f}, P95: {np.percentile(all_gpu, 95):.2f})")
print(f"Overall Average GameThread: {np.mean(all_gt):.2f} ms")
print(f"Overall Average RenderThread: {np.mean(all_rt):.2f} ms")
