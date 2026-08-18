import os, glob, re
import numpy as np

def parse_profile_gpu_file(file_path):
    if not os.path.exists(file_path) or os.path.getsize(file_path) == 0:
        return {}
        
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

    lines = text.splitlines()
    metrics = {}
    
    for line in lines:
        if "LogRHI: Display:" not in line:
            continue
        
        ms_all = re.findall(r'([0-9]+\.[0-9]+)\s*ms', line)
        if not ms_all:
            continue
            
        last_ms = [m.end() for m in re.finditer(r'ms', line)][-1]
        after = line[last_ms:]
        event_str = re.sub(r'^[^\w\<\/\.\-\s]+', '', after)
        event_str = re.sub(r'[^\w\<\/\.\-\s]+$', '', event_str).rstrip().strip()
        
        ms_val = float(ms_all[-1]) if len(ms_all) > 1 else float(ms_all[0])
        
        if "SingleLayerWater" in event_str and "Depth" not in event_str:
            metrics["SingleLayerWater"] = ms_val
        elif "SLW::Draw" in event_str:
            metrics["SLW::Draw"] = ms_val
        elif "SLW::LumenReflections" in event_str:
            metrics["SLW::LumenReflections"] = ms_val
            
    return metrics

data_root = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data"

folders = [
    ("02_ON_OFF_Pure (Baseline)", "Pure"),
    ("02_ON_OFF_No_Kelvin", "No_Kelvin"),
    ("02_ON_OFF_No_Ripple", "No_Ripple"),
    ("02_ON_OFF_No_OceanFoam", "No_OceanFoam"),
    ("02_ON_OFF_No_Godot", "No_Godot"),
    ("03_Doc_Gradient_Kelvin", "Gradient_Kelvin"),
    ("04_Doc_V3_Capacity256_PartialCopy", "V3_Kelvin_Capacity_and_CPU_Copy"),
    ("05_Doc_V4_Ripple_ddx_ddy", "V4_Ripple_pixel_gradient"),
]

print(f"{'Folder / Phase':<38} | {'SingleLayerWater':<18} | {'SLW::Draw':<18} | {'SLW::LumenReflections':<22}")
print("=" * 105)

results = {}
for label, fname in folders:
    fpath = os.path.join(data_root, fname)
    log_files = glob.glob(os.path.join(fpath, "*", "*.[tT][xX][tT]"))
    if not log_files:
        log_files = glob.glob(os.path.join(fpath, "*.[tT][xX][tT]"))
        
    slw_list = []
    draw_list = []
    lumen_list = []
    
    for lf in log_files:
        p = parse_profile_gpu_file(lf)
        if p and "SingleLayerWater" in p:
            slw_list.append(p["SingleLayerWater"])
        if p and "SLW::Draw" in p:
            draw_list.append(p["SLW::Draw"])
        if p and "SLW::LumenReflections" in p:
            lumen_list.append(p["SLW::LumenReflections"])
            
    slw_avg = np.mean(slw_list) if slw_list else 0.0
    draw_avg = np.mean(draw_list) if draw_list else 0.0
    lumen_avg = np.mean(lumen_list) if lumen_list else 0.0
    
    results[fname] = (slw_avg, draw_avg, lumen_avg)
    print(f"{label:<38} | {slw_avg:10.3f} ms        | {draw_avg:10.3f} ms        | {lumen_avg:10.3f} ms")

