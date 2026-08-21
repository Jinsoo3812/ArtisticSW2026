import unreal

def inspect_item_data():
    print("=== Inspecting Item Data Assets ===")
    
    # 1. Inspect DA_ItemData
    da_path = "/Game/Blueprints/Item/DA_ItemData"
    if unreal.EditorAssetLibrary.does_asset_exist(da_path):
        da = unreal.EditorAssetLibrary.load_asset(da_path)
        item_defs = da.get_editor_property("item_definitions") if da else {}
        print(f"[{da_path}] Registered Item Tags Count: {len(item_defs)}")
        quest_keys = [str(k) for k in item_defs.keys() if "Quest" in str(k) or "Cipher" in str(k)]
        print(f"  Quest-related keys in DA_ItemData: {quest_keys}")
        all_keys = [str(k) for k in item_defs.keys()]
        print(f"  First 10 keys: {all_keys[:10]}")
    else:
        print(f"[{da_path}] DOES NOT EXIST")

    # 2. Inspect ItemFeatureDataTable
    dt_path = "/Game/Blueprints/Item/ItemFeatureDataTable"
    if unreal.EditorAssetLibrary.does_asset_exist(dt_path):
        dt = unreal.EditorAssetLibrary.load_asset(dt_path)
        rows = dt.get_column_unique_names() if hasattr(dt, "get_column_unique_names") else []
        row_names = dt.get_row_names() if hasattr(dt, "get_row_names") else []
        print(f"[{dt_path}] Total Rows: {len(row_names)}")
        quest_rows = [str(r) for r in row_names if "Quest" in str(r) or "Cipher" in str(r)]
        print(f"  Quest-related row names in ItemFeatureDataTable: {quest_rows}")
        print(f"  First 10 row names: {row_names[:10]}")
    else:
        print(f"[{dt_path}] DOES NOT EXIST")

if __name__ == "__main__":
    inspect_item_data()
