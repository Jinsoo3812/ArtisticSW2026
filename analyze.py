import json

def analyze():
    with open('graph.json', 'r', encoding='utf-8') as f:
        nodes = json.load(f)

    # Build forward edges (who uses this node's output)
    outputs = {k: [] for k in nodes.keys()}
    for k, v in nodes.items():
        for inp in v.get('Inputs', []):
            if inp in outputs:
                outputs[inp].append(k)
            else:
                outputs[inp] = [k]

    # Target nodes to check
    targets = {
        "ComponentMask_4": "MaterialExpressionComponentMask_4",
        "FunctionCall_22": "MaterialExpressionMaterialFunctionCall_22",
        "V3_Wave_Basis_Installed": "MaterialExpressionScalarParameter_46",
        "V3_Base_Wave_Normal_Blend_Installed": "MaterialExpressionScalarParameter_59",
        "SetMaterialAttributes_2": "MaterialExpressionSetMaterialAttributes_2"
    }

    print("=== Direct Outputs ===")
    for name, node_id in targets.items():
        if node_id in outputs:
            out_nodes = set(outputs[node_id])
            if out_nodes:
                print(f"{name} ({node_id}) outputs directly to:")
                for o in out_nodes:
                    print(f"  - {o} (Class: {nodes.get(o, {}).get('Class', 'Unknown')})")
            else:
                print(f"{name} ({node_id}) has NO outputs. It is completely disconnected.")
        else:
            print(f"{name} ({node_id}) not found in graph.")

    print("\n=== Reachability to SetMaterialAttributes_2 ===")
    # DFS to see if they can reach SetMaterialAttributes_2 or if they reach a dead end
    def get_all_reachable(start_node):
        visited = set()
        stack = [start_node]
        while stack:
            curr = stack.pop()
            if curr not in visited:
                visited.add(curr)
                for nxt in outputs.get(curr, []):
                    stack.append(nxt)
        return visited

    for name, node_id in targets.items():
        if node_id == "MaterialExpressionSetMaterialAttributes_2":
            continue
        if node_id in outputs:
            reachable = get_all_reachable(node_id)
            if "MaterialExpressionSetMaterialAttributes_2" in reachable:
                print(f"{name} eventually reaches SetMaterialAttributes_2.")
            else:
                print(f"{name} DOES NOT reach SetMaterialAttributes_2.")
                
                # Check where it actually ends up (the ultimate sinks)
                sinks = [n for n in reachable if not outputs.get(n, [])]
                print(f"  Instead, it dead-ends at: {sinks}")

analyze()
