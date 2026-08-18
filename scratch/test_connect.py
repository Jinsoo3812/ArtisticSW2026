import unreal

out_path = r"c:\Unreal Projects\ArtisticSW2026\scratch\mel_funcs.txt"
mel = unreal.MaterialEditingLibrary
funcs = [d for d in dir(mel) if not d.startswith("_")]

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(funcs))

print("Wrote methods to " + out_path)
