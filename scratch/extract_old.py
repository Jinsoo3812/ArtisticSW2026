import subprocess
import os

# Extract previous uasset from git
git_cmd = r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe"
uasset_rel = "Content/New/Water/Realistic_Water/M_Realistic_Water.uasset"
temp_uasset = r"c:\Unreal Projects\ArtisticSW2026\scratch\M_Realistic_Water_Old.uasset"

with open(temp_uasset, "wb") as f:
    subprocess.run([git_cmd, "show", f"HEAD:{uasset_rel}"], stdout=f, cwd=r"c:\Unreal Projects\ArtisticSW2026")

print(f"Extracted old uasset ({os.path.getsize(temp_uasset)} bytes)")
