import unreal

out_path = r"c:\Unreal Projects\ArtisticSW2026\scratch\inspect_foam_out.txt"
lines = []

MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(MATERIAL_PATH)
prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"

node15 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_15")
node16 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_16")

mel = unreal.MaterialEditingLibrary

lines.append(f"Node15: {node15}")
if node15:
    inputs15 = mel.get_inputs_for_material_expression(mat, node15)
    input_names15 = [str(x.get_editor_property("input_name")) for x in node15.get_editor_property("inputs")]
    lines.append(f"Node15 Inputs ({len(inputs15)}):")
    for name, inp in zip(input_names15, inputs15):
        lines.append(f"  - {name}: {inp.get_name() if inp else 'None'} ({inp.get_class().get_name() if inp else ''})")
        if inp:
            try:
                for prop in ['parameter_name', 'constant', 'r', 'g', 'b', 'a']:
                    try:
                        val = inp.get_editor_property(prop)
                        lines.append(f"      prop '{prop}' = {val}")
                    except: pass
            except: pass

lines.append(f"\nNode16: {node16}")
if node16:
    inputs16 = mel.get_inputs_for_material_expression(mat, node16)
    input_names16 = [str(x.get_editor_property("input_name")) for x in node16.get_editor_property("inputs")]
    lines.append(f"Node16 Inputs ({len(inputs16)}):")
    for name, inp in zip(input_names16, inputs16):
        lines.append(f"  - {name}: {inp.get_name() if inp else 'None'} ({inp.get_class().get_name() if inp else ''})")

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

