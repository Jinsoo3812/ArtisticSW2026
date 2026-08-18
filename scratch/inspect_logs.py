import os
import glob
import re

def parse_profile_log(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    lines = content.splitlines()
    data = {}
    
    # We want to extract key events and their times.
    # Lines often look like:
    # ... | Time | ... | Time | EventName
    # Let's inspect all lines containing 'ms' and an event name.
    
    # In UE ProfileGPU dump:
    # Column format often has:
    # [Draws] [Dsptch] [Prim] [Vert] [Percent] [Time] [Draws] [Dsptch] [Prim] [Vert] [Percent] [Time] [EventName]
    
    for line in lines:
        if "LogRHI: Display:" not in line:
            continue
        
        # Regex to find time in ms and event name at the end
        # Example: ' 7.04 ms ' or ' 0.38 ms '
        # Look for the last event name
        match = re.search(r'([0-9]+\.[0-9]+)\s*ms\s*\|\s*([0-9]+\.[0-9]+)\s*ms\s*\|\s*(.*?)\s*\|?$', line)
        if not match:
            # Maybe with special characters or different column formatting
            # Let's search for ms pattern
            ms_matches = re.findall(r'([0-9]+\.[0-9]+)\s*ms', line)
            if ms_matches:
                # The event name is usually at the end of the line
                # Let's split by double bar or vertical bars
                tokens = [t.strip() for t in re.split(r'[\u2551\u2502\|\xa6]', line) if t.strip()]
                # If last token is the event name
                if len(tokens) >= 2:
                    event_name = tokens[-1]
                    # Clean event name
                    event_name = re.sub(r'^[^\w\<\/\.\-]+', '', event_name).strip()
                    # Times are among tokens
                    times = [float(m) for m in ms_matches]
                    data[event_name] = times
    return data

# Let's write a targeted parser and tester
pure_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Pure"
grad_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\Gradient_Kelvin"

def process_dir(dir_path):
    results = {}
    for sub in sorted(glob.glob(os.path.join(dir_path, "*"))):
        if not os.path.isdir(sub): continue
        log_files = glob.glob(os.path.join(sub, "*.[tT][xX][tT]"))
        if not log_files: continue
        log_file = log_files[0]
        sub_name = os.path.basename(sub)
        with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
        results[sub_name] = lines
    return results

pure_logs = process_dir(pure_dir)
grad_logs = process_dir(grad_dir)

print(f"Pure runs found: {list(pure_logs.keys())}")
print(f"Gradient_Kelvin runs found: {list(grad_logs.keys())}")

# Let's print some sample lines from Pure 1st run
print("Sample lines from Pure 1st run:")
for l in pure_logs[list(pure_logs.keys())[0]][:35]:
    print(l)
