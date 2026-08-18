import unreal

out_path = r"c:\Unreal Projects\ArtisticSW2026\scratch\dump_customs_out.txt"
lines = []

MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(MATERIAL_PATH)
prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"

found_customs = {}

for index in range(100):
    node = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if node:
        name = node.get_name()
        desc = str(node.get_editor_property("description"))
        code = str(node.get_editor_property("code"))
        includes = [str(x) for x in node.get_editor_property("include_file_paths")]
        inputs = node.get_editor_property("inputs")
        input_names = [str(inp.get_editor_property("input_name")) for inp in inputs] if inputs else []
        
        found_customs[name] = {
            "node": node,
            "desc": desc,
            "code": code,
            "includes": includes,
            "inputs": input_names
        }
        
        lines.append(f"\n=======================================================")
        lines.append(f"Node: {name} | Desc: '{desc}'")
        lines.append(f"Includes: {includes}")
        lines.append(f"Inputs: {input_names}")
        lines.append(f"Code:\n{code}")

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print(f"Found {len(found_customs)} custom nodes. Dumped to {out_path}")
