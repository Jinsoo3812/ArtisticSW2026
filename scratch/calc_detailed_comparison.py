import os
import glob
import re
import numpy as np

def parse_profile_gpu_file(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

    lines = text.splitlines()
    
    # In UE ProfileGPU dump, there are rows like:
    # LogRHI: Display: | Draws | Dsptch | Prim | Vert | Percent | Time | Draws | Dsptch | Prim | Vert | Percent | Time | EventName |
    # The first set of columns is Graphics/Self, the second is Inclusive or Compute.
    
    events_data = []
    
    for line in lines:
        if "LogRHI: Display:" not in line:
            continue
        
        # Check if line contains box chars or table formatting
        # Let's split by box chars or bars
        # Usually UE uses unicode box chars: \u2551 (║), \u2502 (│), \u2550 (═), etc.
        # Or in terminal converted to ?? or |
        
        # Let's find event name and times
        # The line has structure: ... Time ... Time ... EventName
        # Let's extract all numbers followed by 'ms'
        ms_all = re.findall(r'([0-9]+\.[0-9]+)\s*ms', line)
        if not ms_all:
            continue
            
        # Get raw event name (indentation reflects hierarchy)
        # Find where the last 'ms' ends
        last_ms = [m.end() for m in re.finditer(r'ms', line)][-1]
        after = line[last_ms:]
        # Remove table borders like ??, |, etc. at the ends
        event_str = re.sub(r'^[^\w\<\/\.\-\s]+', '', after)
        event_str = re.sub(r'[^\w\<\/\.\-\s]+$', '', event_str).rstrip()
        
        # Compute indentation
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
            
    return events_data

pure_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Pure"
grad_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Gradient_Kelvin"

def extract_key_metrics(parsed_events):
    metrics = {}
    for ev in parsed_events:
        name = ev["name"]
        ms = ev["ms_list"]
        
        # Match specific events
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

def analyze_group_logs(dir_path, name):
    subdirs = sorted(glob.glob(os.path.join(dir_path, "*")))
    group_metrics = []
    print(f"\n==================== {name} ====================")
    for s in subdirs:
        if not os.path.isdir(s): continue
        log_files = glob.glob(os.path.join(s, "*.[tT][xX][tT]"))
        if not log_files: continue
        run_name = os.path.basename(s)
        parsed = parse_profile_gpu_file(log_files[0])
        m = extract_key_metrics(parsed)
        m["_run"] = run_name
        group_metrics.append(m)
        print(f"[{run_name}] Extracted {len(m)} metrics")
        for k, v in m.items():
            if k != "_run":
                print(f"  {k:<28}: {v:.3f} ms")
                
    # Calculate average
    all_keys = [k for k in group_metrics[0].keys() if k != "_run"]
    avg = {}
    for k in all_keys:
        vals = [g[k] for g in group_metrics if k in g]
        avg[k] = np.mean(vals) if vals else 0.0
        
    print(f"\n--- {name} 3-Run Average ---")
    for k, v in avg.items():
        print(f"  {k:<28}: {v:.3f} ms")
        
    return group_metrics, avg

pure_runs, pure_avg = analyze_group_logs(pure_dir, "Pure (Baseline)")
grad_runs, grad_avg = analyze_group_logs(grad_dir, "Gradient_Kelvin (B-Commit)")
