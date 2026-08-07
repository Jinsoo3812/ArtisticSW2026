import json
import re

file_path = "c:\\Unreal Projects\\ArtisticSW2026\\M_Realistic_Water_utf8.t3d"
output_path = "c:\\Unreal Projects\\ArtisticSW2026\\graph_full.json"

objects = {}
current_object = None
current_code = []
in_code = False

with open(file_path, "r", encoding="utf-8") as f:
    for line in f:
        line_stripped = line.strip()
        if line_stripped.startswith("Begin Object"):
            m = re.search(r'Name="([^"]+)"', line_stripped)
            if m:
                current_object = m.group(1)
                objects[current_object] = {
                    "Class": "",
                    "Inputs": {},
                    "Properties": {}
                }
                m_class = re.search(r'Class=/Script/Engine\.([^ ]+)', line_stripped)
                if m_class:
                    objects[current_object]["Class"] = m_class.group(1)
        elif line_stripped.startswith("End Object"):
            current_object = None
        elif current_object:
            # Check for multi-line code string
            if line_stripped.startswith('Code="'):
                in_code = True
                current_code = [line[line.find('Code="')+6:]]
                if current_code[0].rstrip().endswith('"') and not current_code[0].rstrip().endswith('\\"'):
                    in_code = False
                    objects[current_object]["Properties"]["Code"] = current_code[0].rstrip()[:-1]
                continue
            
            if in_code:
                current_code.append(line)
                if line.rstrip().endswith('"') and not line.rstrip().endswith('\\"'):
                    in_code = False
                    objects[current_object]["Properties"]["Code"] = "".join(current_code).rstrip()[:-1]
                continue
                
            # Match property assignments like PropName=Value
            m_prop = re.match(r'([A-Za-z0-9_]+)=(.*)', line_stripped)
            if m_prop:
                prop_name = m_prop.group(1)
                prop_val = m_prop.group(2)
                
                # Check if it's an input connection
                m_input = re.search(r'Expression=[^ \']+\'\"([^\"]+)\"\'', prop_val)
                if m_input:
                    target_node = m_input.group(1)
                    if ":" in target_node:
                        target_node = target_node.split(":")[1]
                    # Also keep track of output index if specified OutputIndex=X
                    m_idx = re.search(r'OutputIndex=(\d+)', prop_val)
                    if m_idx:
                        target_node = f"{target_node}:{m_idx.group(1)}"
                    objects[current_object]["Inputs"][prop_name] = target_node
                else:
                    objects[current_object]["Properties"][prop_name] = prop_val

with open(output_path, "w", encoding="utf-8") as f:
    json.dump(objects, f, indent=2)

print(f"Dumped {len(objects)} objects to {output_path}")
