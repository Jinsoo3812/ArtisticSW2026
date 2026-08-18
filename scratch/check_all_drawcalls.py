import os
import glob
import csv
import numpy as np

def check_all_folders():
    base_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data"
    for folder in sorted(glob.glob(os.path.join(base_dir, "*"))):
        if not os.path.isdir(folder): continue
        f_name = os.path.basename(folder)
        csvs = glob.glob(os.path.join(folder, "*", "*.csv"))
        if not csvs:
            csvs = glob.glob(os.path.join(folder, "*.csv"))
            
        print(f"\n[{f_name}] Found {len(csvs)} CSVs")
        draw_calls = []
        gts = []
        fps_list = []
        fts = []
        for c in csvs:
            with open(c, 'r', encoding='utf-8', errors='ignore') as f:
                reader = csv.reader(f)
                header = [h.strip() for h in next(reader)]
                dc_idx = header.index("RHI/DrawCalls") if "RHI/DrawCalls" in header else -1
                gt_idx = header.index("GameThreadTime") if "GameThreadTime" in header else -1
                ft_idx = header.index("FrameTime") if "FrameTime" in header else -1
                
                rows_dc = []
                rows_gt = []
                rows_ft = []
                for row in reader:
                    if not row: continue
                    if dc_idx >= 0 and len(row) > dc_idx:
                        try: rows_dc.append(float(row[dc_idx]))
                        except: pass
                    if gt_idx >= 0 and len(row) > gt_idx:
                        try: rows_gt.append(float(row[gt_idx]))
                        except: pass
                    if ft_idx >= 0 and len(row) > ft_idx:
                        try: rows_ft.append(float(row[ft_idx]))
                        except: pass
                if rows_dc:
                    mean_dc = np.mean(rows_dc)
                    draw_calls.append(mean_dc)
                    print(f"  - {os.path.basename(c)}: DrawCalls={mean_dc:.1f}, GT={np.mean(rows_gt):.2f}ms, FT={np.mean(rows_ft):.2f}ms (FPS={1000.0/np.mean(rows_ft):.1f})")
        if draw_calls:
            print(f"  ==> {f_name} Average DrawCalls: {np.mean(draw_calls):.1f}")

check_all_folders()
