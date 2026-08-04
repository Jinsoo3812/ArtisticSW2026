import os
import re
import json

t3d_dir = r"C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials"

def parse_t3d_file(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # If Instance
    if "_MI_" in file_path:
        scalars = re.findall(r'ScalarParameterValues\(.*?\)=\(ParameterInfo=\(Name="([^"]+)"\).*?ParameterValue=([0-9.\-]+)', content)
        vectors = re.findall(r'VectorParameterValues\(.*?\)=\(ParameterInfo=\(Name="([^"]+)"\).*?ParameterValue=\(([^)]+)\)', content)
        textures = re.findall(r'TextureParameterValues\(.*?\)=\(ParameterInfo=\(Name="([^"]+)"\).*?ParameterValue=[^\']+\'([^\']+)\'', content)
        return {"Type": "Instance", "Scalars": scalars, "Vectors": vectors, "Textures": textures}
    
    # If Master Material
    if "_M_" in file_path:
        # Find Custom Nodes (often the most important logic)
        custom_nodes = []
        blocks = content.split("Begin Object Class=/Script/Engine.MaterialExpressionCustom")
        if len(blocks) == 1:
            blocks = content.split("Begin Object Class=MaterialExpressionCustom")
        for block in blocks[1:]:
            end_idx = block.find("End Object")
            obj_content = block[:end_idx]
            desc_match = re.search(r'Description="([^"]+)"', obj_content)
            desc = desc_match.group(1) if desc_match else "CustomNode"
            code_match = re.search(r'Code="([^"]+)"', obj_content)
            code = code_match.group(1).replace('\\r\\n', '\n').replace('\\n', '\n') if code_match else ""
            inputs = re.findall(r'Inputs\(\d+\)=\(InputName="([^"]+)"', obj_content)
            custom_nodes.append({"Description": desc, "Code": code, "Inputs": inputs})
        
        # Find standard nodes count
        expressions = re.findall(r'Begin Object Class=[^\s]+(MaterialExpression[A-Za-z0-9_]+)', content)
        counts = {}
        for exp in expressions:
            counts[exp] = counts.get(exp, 0) + 1
            
        return {"Type": "Master", "CustomNodes": custom_nodes, "NodeCounts": counts}
    
    return {"Type": "Unknown"}

versions = ["V3", "V4", "V5", "V6"]
report = []

for v in versions:
    report.append(f"# {v} 분석 결과")
    for f in os.listdir(t3d_dir):
        if f.startswith(v) and f.endswith(".t3d"):
            data = parse_t3d_file(os.path.join(t3d_dir, f))
            formatted_dir = t3d_dir.replace('\\', '/')
            report.append(f"## [{f}](file:///{formatted_dir}/{f})")
            if data["Type"] == "Instance":
                report.append("### 파라미터 연결 상태 (오버라이드된 값)")
                if data["Scalars"]: report.append("- **Scalars**: " + ", ".join([f"`{k}`={v}" for k, v in data["Scalars"]]))
                if data["Vectors"]: report.append("- **Vectors**: " + ", ".join([f"`{k}`=({vals})" for k, vals in data["Vectors"]]))
                if data["Textures"]: report.append("- **Textures**: " + ", ".join([f"`{k}`={v.split('/')[-1].split('.')[0]}" for k, v in data["Textures"]]))
            elif data["Type"] == "Master":
                report.append("### 노드 통계 (주요 로직 블록)")
                top_nodes = sorted(data["NodeCounts"].items(), key=lambda x: x[1], reverse=True)[:10]
                report.append("- **사용된 상위 노드들**: " + ", ".join([f"{k.replace('MaterialExpression', '')}({v}개)" for k, v in top_nodes]))
                if data["CustomNodes"]:
                    report.append("### 커스텀 코드 노드 (HLSL)")
                    for node in data["CustomNodes"]:
                        report.append(f"#### 노드명: `{node['Description']}`")
                        report.append(f"- **입력 핀(Inputs)**: {', '.join(node['Inputs'])}")
                        if "#include" in node["Code"]:
                            report.append(f"- **Include 구문**: " + ", ".join(re.findall(r'#include\s+"([^"]+)"', node["Code"])))
            report.append("\n")

with open(r"C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\summary.md", "w", encoding='utf-8') as out:
    out.write("\n".join(report))
print("Summary generated!")
