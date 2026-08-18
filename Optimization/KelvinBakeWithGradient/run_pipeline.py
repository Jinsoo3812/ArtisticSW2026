"""
run_pipeline.py - Master Pipeline Runner for Kelvin Wake with Analytical Gradients
Executes the full pipeline end-to-end:
  1. Golden Image & Gradient Map Generation (Fr = 0.30, 0.50, 0.70, 1.00)
  2. Multi-Slice Production Atlas Bake (12 Froude slices, RGBA16F / FP16 binary, C++/HLSL header)
  3. Parity, Gradient & Physics Verification Suite
"""

import os
import site
import sys
import time
from pathlib import Path

user_site = site.getusersitepackages()
if os.path.exists(user_site) and user_site not in sys.path:
    sys.path.insert(0, user_site)

from generate_golden_images import generate_all_golden_images
from atlas_baker import bake_kelvin_wake_atlas, DEFAULT_FROUDE_SLICES
from verify_parity_and_physics import run_all_verifications


def main():
    root_dir = Path(__file__).parent
    golden_dir = root_dir / "golden_images"
    atlas_dir = root_dir / "atlas_output"

    print("=" * 75)
    print("  ARTISTIC SW 2026 - KELVIN WAKE WITH ANALYTICAL GRADIENTS BAKER")
    print("  Channels: R=Height, G=dZ/du (Downstream), B=dZ/dv (Lateral), A=Mask")
    print("  Eliminates 3x Finite Difference Loops in Shader -> 1x Single Sampling!")
    print("=" * 75)
    t_start = time.time()

    # Step 1: Golden Image & Gradient Generation
    print("\n>>> [STEP 1/3] Generating Golden Images & Analytical Gradient Maps ...")
    t0 = time.time()
    generate_all_golden_images(
        golden_froude_list=[0.30, 0.50, 0.70, 1.00],
        output_dir_str=str(golden_dir)
    )
    print(f">>> Step 1 Completed in {time.time() - t0:.2f} seconds.")

    # Step 2: Production Atlas Bake
    print("\n>>> [STEP 2/3] Baking Multi-Slice Production Atlas (RGBA16F & FP16) ...")
    t0 = time.time()
    bake_kelvin_wake_atlas(
        froude_slices=DEFAULT_FROUDE_SLICES,
        output_dir_str=str(atlas_dir)
    )
    print(f">>> Step 2 Completed in {time.time() - t0:.2f} seconds.")

    # Step 3: Verification Suite
    print("\n>>> [STEP 3/3] Running Automated Parity, Gradient & Physics Verification ...")
    t0 = time.time()
    run_all_verifications(
        atlas_dir_str=str(atlas_dir),
        output_dir_str=str(root_dir)
    )
    print(f">>> Step 3 Completed in {time.time() - t0:.2f} seconds.")

    total_time = time.time() - t_start
    print("=" * 75)
    print(f"  ALL PIPELINE STEPS COMPLETED SUCCESSFULLY IN {total_time:.2f} SECONDS!")
    print(f"  Output Directory: {root_dir}")
    print("=" * 75)


if __name__ == "__main__":
    main()
