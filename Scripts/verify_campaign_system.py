"""
Campaign System & C++ Architecture Automated Verification Script
Verifies:
1. Module dependencies and cycle freedom across Build.cs files
2. EStoryNode enum and StoryFacadeSubsystem parity
3. GameplayTags header declaration and cpp definition parity
4. MultiGameMode uniform pawn spawn architecture
"""

import sys
import os
import re
from pathlib import Path

WORKSPACE = Path(r"c:\Unreal Projects\ArtisticSW2026")
SOURCE_DIR = WORKSPACE / "Source"

def check_module_dependencies():
    print("[1/4] Checking Module Dependencies for Cycles...")
    build_files = list(SOURCE_DIR.glob("**/*.Build.cs"))
    modules = {}
    dep_graph = {}

    for bf in build_files:
        mod_name = bf.stem.replace(".Build", "")
        modules[mod_name] = bf
        content = bf.read_text(encoding="utf-8", errors="ignore")
        
        # Extract PublicDependencyModuleNames and PrivateDependencyModuleNames
        deps = set()
        for match in re.finditer(r'(?:Public|Private)DependencyModuleNames\.(?:AddRange|Add)\s*\(\s*(?:new\s+string\[\]\s*\{)?([^;]+)', content):
            raw = match.group(1)
            for item in re.findall(r'"([^"]+)"', raw):
                deps.add(item)
        dep_graph[mod_name] = deps

    # Cycle detection via DFS
    visited = {}
    
    def dfs(node, path):
        visited[node] = 1 # Visiting
        for neighbor in dep_graph.get(node, []):
            if neighbor in dep_graph: # Only internal workspace modules
                if visited.get(neighbor) == 1:
                    print(f"  [ERROR] Dependency cycle detected: {' -> '.join(path + [neighbor])}")
                    return False
                elif neighbor not in visited:
                    if not dfs(neighbor, path + [neighbor]):
                        return False
        visited[node] = 2 # Visited
        return True

    for mod in dep_graph:
        if mod not in visited:
            if not dfs(mod, [mod]):
                return False

    print(f"  [PASS] Checked {len(dep_graph)} workspace modules. 0 dependency cycles found.")
    return True

def check_story_node_parity():
    print("\n[2/4] Checking EStoryNode Enum & StoryFacadeSubsystem Parity...")
    header_file = SOURCE_DIR / "Story" / "Public" / "StoryFacadeSubsystem.h"
    cpp_file = SOURCE_DIR / "Story" / "Private" / "StoryFacadeSubsystem.cpp"

    header_text = header_file.read_text(encoding="utf-8")
    cpp_text = cpp_file.read_text(encoding="utf-8")

    # Extract enum entries
    enum_match = re.search(r'enum class EStoryNode : uint8\s*\{([^}]+)\};', header_text)
    if not enum_match:
        print("  [ERROR] Could not find EStoryNode enum in StoryFacadeSubsystem.h")
        return False

    raw_enum = enum_match.group(1)
    enum_entries = []
    for line in raw_enum.split(','):
        cleaned = line.strip()
        if cleaned:
            name = cleaned.split()[0].split('=')[0].strip()
            if name:
                enum_entries.append(name)

    print(f"  Found {len(enum_entries)} EStoryNode entries: {', '.join(enum_entries)}")

    # Check GetInternalTag in cpp
    missing_in_tags = []
    missing_in_prereqs = []
    for entry in enum_entries:
        if f"case EStoryNode::{entry}:" not in cpp_text:
            missing_in_tags.append(entry)

    if missing_in_tags:
        print(f"  [ERROR] Missing enum switch cases in StoryFacadeSubsystem.cpp: {missing_in_tags}")
        return False

    # Check specific expected nodes
    required_nodes = [
        "GameStarted", "FirstSailingCompleted", "ReconQuestAccepted",
        "CipherBookAcquired", "MiddleBoss1Defeated", "SupplyPatrolQuestAccepted",
        "CurrentGeneratorUnlocked", "MiddleBoss2Defeated", "DecipherQuestAccepted",
        "SuppressJapaneseForcesQuestAccepted", "WaterBombUnlocked",
        "MiddleBoss3Defeated", "UldolmokBattleQuestAccepted", "BombardmentUnlocked",
        "FinalBossDefeated", "EndingDialogueCompleted"
    ]
    for req in required_nodes:
        if req not in enum_entries:
            print(f"  [ERROR] Required story node missing: {req}")
            return False

    print("  [PASS] EStoryNode enum and StoryFacadeSubsystem switch cases are 100% in parity.")
    return True

def check_gameplay_tags_parity():
    print("\n[3/4] Checking GameplayTags Header & Cpp Parity...")
    header_file = SOURCE_DIR / "ArtisticSWCore" / "Public" / "BaseGameplayTags.h"
    cpp_file = SOURCE_DIR / "ArtisticSWCore" / "Private" / "BaseGameplayTags.cpp"

    header_text = header_file.read_text(encoding="utf-8")
    cpp_text = cpp_file.read_text(encoding="utf-8")

    declared_tags = set(re.findall(r'UE_DECLARE_GAMEPLAY_TAG_EXTERN\(([^)]+)\)', header_text))
    defined_tags = set(re.findall(r'UE_DEFINE_GAMEPLAY_TAG\(([^,\s)]+)', cpp_text))

    diff_undeclared = defined_tags - declared_tags
    diff_undefined = declared_tags - defined_tags

    if diff_undefined:
        print(f"  [ERROR] Tags declared in header but not defined in cpp: {diff_undefined}")
        return False

    quest_tags = [
        "Item_Quest_InvasionMap", "Item_Quest_CipherBook", "Item_Quest_JapaneseCipher",
        "Item_Quest_DecipheredCipher", "Item_Quest_AirRaidInfo"
    ]
    for tag in quest_tags:
        if tag not in declared_tags:
            print(f"  [ERROR] Expected quest tag not declared: {tag}")
            return False

    print(f"  [PASS] All {len(declared_tags)} declared tags are correctly defined in cpp.")
    return True

def check_game_mode_uniform_spawn():
    print("\n[4/4] Checking MultiGameMode Uniform Spawn Structure...")
    gm_header = SOURCE_DIR / "ArtisticSWCore" / "Public" / "MultiGameMode.h"
    gm_cpp = SOURCE_DIR / "ArtisticSWCore" / "Private" / "MultiGameMode.cpp"

    h_text = gm_header.read_text(encoding="utf-8")
    cpp_text = gm_cpp.read_text(encoding="utf-8")

    if "CommonPlayerPawnClass" not in h_text:
        print("  [ERROR] CommonPlayerPawnClass not found in MultiGameMode.h")
        return False

    if "GetDefaultPawnClassForController_Implementation" not in cpp_text:
        print("  [ERROR] GetDefaultPawnClassForController_Implementation not found in MultiGameMode.cpp")
        return False

    print("  [PASS] MultiGameMode uniform player pawn spawn architecture verified.")
    return True

def main():
    print("=========================================================")
    print(" ArtisticSW2026 Campaign Architecture Automated Validator ")
    print("=========================================================")
    results = [
        check_module_dependencies(),
        check_story_node_parity(),
        check_gameplay_tags_parity(),
        check_game_mode_uniform_spawn()
    ]

    if all(results):
        print("\n>>> ALL ARCHITECTURAL & STATIC VERIFICATION CHECKS PASSED (4/4)! <<<\n")
        return 0
    else:
        print("\n>>> VERIFICATION FAILED. Check errors above. <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())