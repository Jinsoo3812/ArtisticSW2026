import glob, os, re

v4_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\V4_Ripple_pixel_gradient"
log_file = glob.glob(os.path.join(v4_dir, "1회차", "*.[tT][xX][tT]"))[0]

with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

lines = text.splitlines()

print("=== Major GPU Events (>0.05 ms) in 1st Run ===")
for line in lines:
    if "LogRHI: Display:" not in line: continue
    ms_matches = re.findall(r'([0-9]+\.[0-9]+)\s*ms', line)
    if not ms_matches: continue
    
    val = float(ms_matches[-1])
    if val >= 0.05:
        last_ms = [m.end() for m in re.finditer(r'ms', line)][-1]
        after = line[last_ms:].strip()
        print(f"  {val:6.3f} ms : {after}")
