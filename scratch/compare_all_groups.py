import os
import glob
import csv
import re

base_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data"

groups = ["Pure", "No_Kelvin", "No_Godot"]

def parse_gpu_table(filepath):
    if not os.path.exists(filepath):
        return {}
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
            if len(parts) >= 4:
                inclusive_part = parts[2]
                event_name = parts[3].strip()
                m = re.search(r"([\d\.]+)\s*ms\s*$", inclusive_part)
                if m:
                    time_ms = float(m.group(1))
                    results[event_name] = time_ms
                    
    return results

def parse_csv(csv_path):
    if not os.path.exists(csv_path):
        return {}
    with open(csv_path, "r", encoding="utf-8-sig") as f:
        reader = csv.reader(f)
        header = next(reader)
        rows = list(reader)
        
    hmap = {name.strip(): i for i, name in enumerate(header)}
    
    metrics = [
        "FrameTime",
        "GPUTime",
        "RenderThreadTime",
        "RHIThreadTime",
        "GameThreadTime",
        "Exclusive/RenderThread/Water",
        "Exclusive/AllWorkers/Water",
        "RHI/DrawCalls",
        "RHI/PrimitivesDrawn"
    ]
    
    res = {"FrameCount": len(rows)}
    for m in metrics:
        if m in hmap:
            idx = hmap[m]
            vals = []
            for r in rows:
                if idx < len(r) and r[idx].strip():
                    try:
                        vals.append(float(r[idx]))
                    except ValueError:
                        pass
            if vals:
                res[m] = sum(vals) / len(vals)
    return res

all_results = {}

print("="*105)
print(f"{'GROUP':<12} | {'TRIAL':<6} | {'FPS':>7} | {'FrameTime':>10} | {'GPUTime':>9} | {'DrawTime':>9} | {'SLW Total':>10} | {'SLW::Draw':>10}")
print("="*105)

for g in groups:
    g_dir = os.path.join(base_dir, g)
    if not os.path.isdir(g_dir):
        continue
    all_results[g] = []
    trial_dirs = sorted([d for d in os.listdir(g_dir) if os.path.isdir(os.path.join(g_dir, d))])
    for td in trial_dirs:
        t_path = os.path.join(g_dir, td)
        csv_files = glob.glob(os.path.join(t_path, "*.csv"))
        log_files = glob.glob(os.path.join(t_path, "*.txt")) + glob.glob(os.path.join(t_path, "*.log"))
        
        csv_data = parse_csv(csv_files[0]) if csv_files else {}
        log_data = parse_gpu_table(log_files[0]) if log_files else {}
        
        ft = csv_data.get("FrameTime", 0)
        fps = 1000.0 / ft if ft > 0 else 0
        gpu = csv_data.get("GPUTime", 0)
        draw = csv_data.get("RenderThreadTime", 0)
        slw = log_data.get("SingleLayerWater", 0)
        slw_draw = log_data.get("SLW::Draw", 0)
        
        all_results[g].append({
            "trial": td,
            "fps": fps,
            "frame_time": ft,
            "gpu_time": gpu,
            "draw_time": draw,
            "rhi_time": csv_data.get("RHIThreadTime", 0),
            "game_time": csv_data.get("GameThreadTime", 0),
            "slw_total": slw,
            "slw_draw": slw_draw,
            "slw_lumen": log_data.get("SLW::LumenReflections", 0),
            "slw_prepass": log_data.get("SingleLayerWaterDepthPrepass", 0),
            "shadows": log_data.get("ShadowDepths", 0),
            "postprocess": log_data.get("PostProcessing", 0),
            "draw_calls": csv_data.get("RHI/DrawCalls", 0)
        })
        
        print(f"{g:<12} | {td:<6} | {fps:>7.2f} | {ft:>8.3f} ms | {gpu:>7.3f} ms | {draw:>7.3f} ms | {slw:>8.3f} ms | {slw_draw:>8.3f} ms")

print("="*105)
print("\n" + "="*105)
print(f"{'GROUP (AVERAGE)':<20} | {'FPS':>7} | {'FrameTime':>10} | {'GPUTime':>9} | {'DrawTime':>9} | {'SLW Total':>10} | {'SLW::Draw':>10}")
print("="*105)

group_averages = {}
for g, trials in all_results.items():
    if not trials:
        continue
    n = len(trials)
    avg_fps = sum(t["fps"] for t in trials) / n
    avg_ft = sum(t["frame_time"] for t in trials) / n
    avg_gpu = sum(t["gpu_time"] for t in trials) / n
    avg_draw = sum(t["draw_time"] for t in trials) / n
    
    slw_trials = [t for t in trials if t["slw_total"] > 0]
    avg_slw = sum(t["slw_total"] for t in slw_trials) / len(slw_trials) if slw_trials else 0
    avg_slw_d = sum(t["slw_draw"] for t in slw_trials) / len(slw_trials) if slw_trials else 0
    avg_slw_lumen = sum(t["slw_lumen"] for t in slw_trials) / len(slw_trials) if slw_trials else 0
    
    group_averages[g] = {
        "fps": avg_fps, "ft": avg_ft, "gpu": avg_gpu, "draw": avg_draw,
        "slw": avg_slw, "slw_draw": avg_slw_d, "slw_lumen": avg_slw_lumen
    }
    print(f"{g:<20} | {avg_fps:>7.2f} | {avg_ft:>8.3f} ms | {avg_gpu:>7.3f} ms | {avg_draw:>7.3f} ms | {avg_slw:>8.3f} ms | {avg_slw_d:>8.3f} ms")

print("="*105)

# Calculate Deltas against Pure
if "Pure" in group_averages:
    pure = group_averages["Pure"]
    print("\n" + "="*105)
    print(f"{'DELTA AGAINST PURE (절감량)':<30} | {'DELTA FPS':>12} | {'DELTA GPU':>14} | {'DELTA SLW::Draw':>18}")
    print("="*105)
    for g, avg in group_averages.items():
        if g == "Pure":
            continue
        d_fps = avg["fps"] - pure["fps"]
        d_gpu = avg["gpu"] - pure["gpu"]
        d_slw = avg["slw_draw"] - pure["slw_draw"]
        print(f"vs {g:<27} | {d_fps:>+10.2f} FPS | {d_gpu:>+11.3f} ms | {d_slw:>+15.3f} ms")
    print("="*105)
