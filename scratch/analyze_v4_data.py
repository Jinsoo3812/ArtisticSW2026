import os
import glob
import re
import csv
import numpy as np

def parse_profile_gpu_file(file_path):
    if not os.path.exists(file_path) or os.path.getsize(file_path) == 0:
        return {}
        
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

    lines = text.splitlines()
    events_data = []
    
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
            events_data.append({
                "indent": indent,
                "name": event_name,
                "ms_list": [float(x) for x in ms_all],
                "raw": line
            })
            
    metrics = {}
    for ev in events_data:
        name = ev["name"]
        ms = ev["ms_list"]
        
        if "SingleLayerWater" in name and "Depth" not in name:
            metrics["SingleLayerWater"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "SLW::Draw" in name:
            metrics["SLW::Draw"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "SLW::LumenReflections" in name:
            metrics["SLW::LumenReflections"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "PostProcessing" in name:
            metrics["PostProcessing"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "ShadowDepths" in name:
            metrics["ShadowDepths"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "RenderVirtualShadowMaps" in name and "Non-Nanite" not in name:
            metrics["VSM_Nanite"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "Batched VSM" in name or ("VirtualShadowMaps" in name and "Non-Nanite" in name):
            metrics["VSM_NonNanite"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "VolumetricCloud" in name:
            metrics["VolumetricCloud"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "SingleLayerWaterDepthPrepass" in name:
            metrics["SLW_DepthPrepass"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "RenderDeferredLighting" in name:
            metrics["RenderDeferredLighting"] = ms[-1] if len(ms) > 1 else ms[0]
        elif name == "Slate":
            metrics["Slate"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "Nanite::VisBuffer" in name:
            metrics["Nanite_VisBuffer"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "BasePass" in name:
            metrics["BasePass"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "SkyAtmosphere" in name:
            metrics["SkyAtmosphere"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "DiffuseIndirectAndAO" in name:
            metrics["DiffuseIndirectAndAO"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "LumenScreenProbeGather" in name:
            metrics["LumenScreenProbeGather"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "UpdateRadianceCaches" in name:
            metrics["UpdateRadianceCaches"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "TranslucencyVolumeLighting" in name:
            metrics["TranslucencyVolumeLighting"] = ms[-1] if len(ms) > 1 else ms[0]
        elif name == "Integrate":
            metrics["Integrate"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "LumenSceneLighting" in name:
            metrics["LumenSceneLighting"] = ms[-1] if len(ms) > 1 else ms[0]
        elif name == "Radiosity":
            metrics["Radiosity"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "DirectLighting" in name:
            metrics["DirectLighting"] = ms[-1] if len(ms) > 1 else ms[0]
        elif "TemporalSuperResolution" in name:
            metrics["TSR"] = ms[-1] if len(ms) > 1 else ms[0]
            
    return metrics

def parse_csv_file(file_path):
    targets = [
        "FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime",
        "RHI/DrawCalls", "RHI/PrimitivesDrawn", "Exclusive/GameThread/EventWait",
        "Exclusive/RenderThread/EventWait", "DrawSceneCommand_StartDelay",
        "Exclusive/AllWorkers/Slate", "Exclusive/RenderThread/Slate"
    ]
    
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        indices = {t: header.index(t) for t in targets if t in header}
        
        data = {t: [] for t in indices}
        for row in reader:
            if not row or len(row) <= max(indices.values()): continue
            for t, idx in indices.items():
                try:
                    data[t].append(float(row[idx]))
                except ValueError:
                    pass
                    
    results = {}
    if "FrameTime" in data and data["FrameTime"]:
        ft_arr = np.array(data["FrameTime"])
        fps_arr = 1000.0 / ft_arr
        results["Frames"] = len(ft_arr)
        results["FrameTime_Mean"] = np.mean(ft_arr)
        results["FrameTime_Std"] = np.std(ft_arr)
        results["FPS_Mean"] = np.mean(fps_arr)
        results["FPS_P5"] = np.percentile(fps_arr, 5)
        
    for k in targets:
        if k in data and data[k]:
            results[k] = np.mean(data[k])
            
    return results

v4_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V4_Ripple_pixel_gradient"
v3_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V3_Kelvin_Capacity_and_CPU_Copy"

print("=== V4 Test Runs Analysis ===")
v4_csvs = []
v4_logs = []
for s in sorted(glob.glob(os.path.join(v4_dir, "*"))):
    if not os.path.isdir(s): continue
    name = os.path.basename(s)
    csv_file = glob.glob(os.path.join(s, "*.csv"))[0]
    log_file = glob.glob(os.path.join(s, "*.[tT][xX][tT]"))[0]
    
    c_res = parse_csv_file(csv_file)
    l_res = parse_profile_gpu_file(log_file)
    v4_csvs.append(c_res)
    v4_logs.append(l_res)
    
    print(f"\n[{name}]")
    print(f"  CSV: FPS={c_res.get('FPS_Mean', 0):.2f}, FrameTime={c_res.get('FrameTime_Mean', 0):.2f}ms, GPUTime={c_res.get('GPUTime', 0):.2f}ms, GT={c_res.get('GameThreadTime', 0):.2f}ms, DrawCalls={c_res.get('RHI/DrawCalls', 0):.1f}")
    print(f"  Log: SLW={l_res.get('SingleLayerWater', 0):.3f}ms (Draw: {l_res.get('SLW::Draw', 0):.3f}ms), Shadow={l_res.get('ShadowDepths', 0):.3f}ms, Post={l_res.get('PostProcessing', 0):.3f}ms")

# Compute V4 Averages
print("\n" + "="*50)
print("=== V4 Averages (3 Runs) ===")
v4_csv_avg = {}
for k in ["FrameTime_Mean", "FPS_Mean", "FPS_P5", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime", "RHI/DrawCalls", "RHI/PrimitivesDrawn", "Exclusive/GameThread/EventWait", "Exclusive/RenderThread/EventWait", "DrawSceneCommand_StartDelay"]:
    vals = [r[k] for r in v4_csvs if k in r]
    v4_csv_avg[k] = np.mean(vals) if vals else 0.0
    print(f"  {k:<32}: {v4_csv_avg[k]:.3f}")

print("\n=== V4 ProfileGPU Log Average (3 Runs) ===")
v4_log_avg = {}
all_log_keys = sorted(set(k for l in v4_logs for k in l.keys()))
for k in all_log_keys:
    vals = [l[k] for l in v4_logs if k in l]
    v4_log_avg[k] = np.mean(vals) if vals else 0.0
    print(f"  {k:<28}: {v4_log_avg[k]:.3f} ms")

