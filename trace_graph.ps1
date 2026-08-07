$jsonPath = "c:\Unreal Projects\ArtisticSW2026\graph_full.json"
$outPath = "c:\Unreal Projects\ArtisticSW2026\trace.txt"

$script:graph = Get-Content $jsonPath | ConvertFrom-Json

$script:visited = @{}
$script:outputLines = @()

function Trace-Node($nodeName, $depth, $connectionName) {
    if (-not $script:graph.psobject.properties.match($nodeName)) {
        $script:outputLines += ("  " * $depth) + "- $connectionName -> $nodeName (NOT FOUND)"
        return
    }
    
    $node = $script:graph.$nodeName
    $className = $node.Class
    $script:outputLines += ("  " * $depth) + "- $connectionName -> $nodeName [$className]"
    
    # Custom Node code
    if ($node.Properties -and $node.Properties.Code) {
        $script:outputLines += ("  " * ($depth + 1)) + "HLSL Code:"
        $codeLines = $node.Properties.Code -split "`n"
        foreach ($cLine in $codeLines) {
            $script:outputLines += ("  " * ($depth + 1)) + "  $cLine"
        }
    }
    
    # Constant values
    if ($node.Properties) {
        $props = @()
        foreach ($prop in $node.Properties.psobject.properties) {
            if ($prop.Name -ne "Code" -and $prop.Name -ne "MaterialExpressionGuid" -and $prop.Name -ne "Material" -and $prop.Name -ne "MaterialExpressionEditorX" -and $prop.Name -ne "MaterialExpressionEditorY") {
                $props += "$($prop.Name)=$($prop.Value)"
            }
        }
        if ($props.Length -gt 0) {
            $script:outputLines += ("  " * ($depth + 1)) + "Properties: " + ($props -join ", ")
        }
    }
    
    if ($script:visited[$nodeName]) {
        $script:outputLines += ("  " * ($depth + 1)) + "(Already traced above)"
        return
    }
    $script:visited[$nodeName] = $true

    if ($node.Inputs) {
        foreach ($inp in $node.Inputs.psobject.properties) {
            Trace-Node $inp.Value ($depth + 1) $inp.Name
        }
    }
}

$script:outputLines += "=== TRACING MaterialAttributes ==="
Trace-Node "MaterialExpressionSetMaterialAttributes_1" 0 "MaterialAttributes"

$script:visited.Clear()
$script:outputLines += ""
$script:outputLines += "=== TRACING Normal ==="
Trace-Node "MaterialExpressionMaterialFunctionCall_23" 0 "Normal"

$script:visited.Clear()
$script:outputLines += ""
$script:outputLines += "=== TRACING Roughness ==="
Trace-Node "MaterialExpressionMax_0" 0 "Roughness"

$script:visited.Clear()
$script:outputLines += ""
$script:outputLines += "=== TRACING Specular ==="
Trace-Node "MaterialExpressionScalarParameter_45" 0 "Specular"

$script:visited.Clear()
$script:outputLines += ""
$script:outputLines += "=== TRACING EmissiveColor ==="
Trace-Node "MaterialExpressionCustom_16" 0 "EmissiveColor"

$script:outputLines | Set-Content $outPath
Write-Host "Done tracing to $outPath"
