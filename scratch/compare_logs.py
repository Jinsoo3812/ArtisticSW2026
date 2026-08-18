import os
import glob
import re

def parse_full_profile(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
        
    lines = content.splitlines()
    
    # Let's search for lines containing specific major keywords
    keywords = [
        "SingleLayerWater",
        "SLW::Draw",
        "SLW::LumenReflections",
        "Slate",
        "PostProcessing",
        "ShadowDepths",
        "RenderVirtualShadowMaps",
        "Batched VSM",
        "VolumetricCloud",
        "SingleLayerWaterDepthPrepass",
        "RenderDeferredLighting",
        "Nanite::VisBuffer",
        "BasePass",
        "SkyAtmosphere",
        "DiffuseIndirectAndAO",
        "LumenScreenProbeGather",
        "UpdateRadianceCaches",
        "TranslucencyVolumeLighting",
        "Integrate",
        "LumenSceneLighting",
        "Radiosity",
        "DirectLighting",
        "TemporalSuperResolution",
        "Total",
        "Scene",
        "ViewFamilies"
    ]
    
    found_events = {}
    
    for line in lines:
        if "LogRHI: Display:" not in line:
            continue
        
        for kw in keywords:
            if kw in line:
                # Extract all ms values in this line
                ms_vals = re.findall(r'([0-9]+\.[0-9]+)\s*ms', line)
                if ms_vals:
                    # Let's store line and ms_vals
                    # Usually in UE ProfileGPU:
                    # First ms is self/exclusive or graphics, last ms is inclusive or compute
                    found_events[kw] = {
                        "raw": line.strip(),
                        "ms": [float(x) for x in ms_vals]
                    }
    return found_events

pure_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Pure"
grad_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Gradient_Kelvin"

def analyze_dataset(dir_path, label):
    print(f"\n=======================================================")
    print(f"               DATASET: {label}")
    print(f"=======================================================")
    subdirs = sorted(glob.glob(os.path.join(dir_path, "*")))
    all_runs = []
    
    for s in subdirs:
        if not os.path.isdir(s): continue
        log_files = glob.glob(os.path.join(s, "*.[tT][xX][tT]"))
        if not log_files: continue
        res = parse_full_profile(log_files[0])
        all_runs.append((os.path.basename(s), res))
        
    for name, res in all_runs:
        print(f"\n--- Run: {name} ---")
        for kw, val in res.items():
            print(f"  {kw:<30}: {val['ms']}  --> {val['raw'][-60:]}")

analyze_dataset(pure_dir, "PURE (Baseline)")
analyze_dataset(grad_dir, "GRADIENT_KELVIN (B Commit)")
