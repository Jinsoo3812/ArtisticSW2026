import json
import re

file_path = "c:\\Unreal Projects\\ArtisticSW2026\\M_Realistic_Water_utf8.t3d"
output_path = "c:\\Unreal Projects\\ArtisticSW2026\\graph.json"

objects = {}
current_object = None

with open(file_path, "r", encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if line.startswith("Begin Object"):
            # e.g. Begin Object Name="MaterialExpressionAdd_0"
            m = re.search(r'Name="([^"]+)"', line)
            if m:
                current_object = m.group(1)
                objects[current_object] = {
                    "Class": "",
                    "Inputs": [],
                    "Text": "",
                    "Function": "",
                    "Desc": ""
                }
                m_class = re.search(r'Class=/Script/Engine\.([^ ]+)', line)
                if m_class:
                    objects[current_object]["Class"] = m_class.group(1)
        elif line.startswith("End Object"):
            current_object = None
        elif current_object:
            # Match connections: something=(Expression=Class'"Name"')
            inputs = re.findall(r'Expression=[^ \']+\'\"([^\"]+)\"\'', line)
            if inputs:
                for inp in inputs:
                    # Sometimes they have MaterialName:NodeName
                    if ":" in inp:
                        inp = inp.split(":")[1]
                    objects[current_object]["Inputs"].append(inp)
            
            # Text for comments
            if line.startswith("Text="):
                m_text = re.search(r'Text="([^"]*)"', line)
                if m_text:
                    objects[current_object]["Text"] = m_text.group(1)
            # Desc for expressions
            if line.startswith("Desc="):
                m_desc = re.search(r'Desc="([^"]*)"', line)
                if m_desc:
                    objects[current_object]["Desc"] = m_desc.group(1)
            # MaterialFunction
            if "MaterialFunction=" in line:
                m_func = re.search(r'MaterialFunction=.*\'"([^"]+)"\'', line)
                if m_func:
                    objects[current_object]["Function"] = m_func.group(1).split(".")[-1]

with open(output_path, "w", encoding="utf-8") as f:
    json.dump(objects, f, indent=2)

print(f"Dumped {len(objects)} objects to {output_path}")
