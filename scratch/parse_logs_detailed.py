import os
import glob
import re
from collections import defaultdict

def parse_profile_gpu_log(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    lines = content.splitlines()
    events = {}
    
    for line in lines:
        if "LogRHI: Display:" not in line:
            continue
        
        # Look for lines containing box characters or delimiters
        # Typical line format:
        # LogRHI: Display:     ?? Draws ?? Dsptch ?? Prim ?? Vert ?? Percent ?? Time ?? Draws ?? Dsptch ?? Prim ?? Vert ?? Percent ?? Time ?? EventName ??
        # Or:
        # ... 0.38 ms ... EventName
        
        # Extract all floats followed by ms
        ms_matches = re.findall(r'([0-9]+\.[0-9]+)\s*ms', line)
        if not ms_matches:
            continue
        
        # Clean event name from end of line
        # Split by box characters or vertical bars
        tokens = [t.strip() for t in re.split(r'[\u2550-\u257f\|\xa6\?]+', line) if t.strip()]
        if not tokens:
            continue
        
        # The event name is usually the last token (or has indentation/spaces in original line)
        # Let's extract the event name by finding the text after the last 'ms'
        last_ms_pos = line.rfind('ms')
        after_last_ms = line[last_ms_pos + 2:]
        event_name = re.sub(r'[\u2550-\u257f\|\xa6\?\<\>]+', '', after_last_ms).strip()
        
        # If event_name is empty or too short, check tokens
        if not event_name and len(tokens) >= 2:
            event_name = tokens[-1]
            
        # The times in ms: the second time or last time is the cumulative/exclusive time
        times = [float(m) for m in ms_matches]
        if times and event_name:
            # times[-1] is usually the duration
            events[event_name] = {
                "time": times[-1],
                "times": times,
                "raw_line": line.strip()
            }
            
    return events

# Let's run on Pure and Gradient_Kelvin
pure_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Pure"
grad_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Gradient_Kelvin"

def load_group(dir_path):
    group_data = {}
    for sub in sorted(glob.glob(os.path.join(dir_path, "*"))):
        if not os.path.isdir(sub): continue
        log_files = glob.glob(os.path.join(sub, "*.[tT][xX][tT]"))
        if not log_files: continue
        name = os.path.basename(sub)
        group_data[name] = parse_profile_gpu_log(log_files[0])
    return group_data

pure_res = load_group(pure_dir)
grad_res = load_group(grad_dir)

print(f"=== Pure Runs: {list(pure_res.keys())} ===")
for r_name, data in pure_res.items():
    print(f"[{r_name}] Total events parsed: {len(data)}")

print(f"\n=== Gradient_Kelvin Runs: {list(grad_res.keys())} ===")
for r_name, data in grad_res.items():
    print(f"[{r_name}] Total events parsed: {len(data)}")

# Let's inspect all event names in Pure 1
print("\nSample top events in Pure 1회차:")
for k, v in list(pure_res[list(pure_res.keys())[0]].items())[:30]:
    print(f"  {k:<45} : {v['times']}")
