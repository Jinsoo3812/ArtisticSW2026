$t3dPath = "c:\Unreal Projects\ArtisticSW2026\M_Realistic_Water_utf8.t3d"
$outPath = "c:\Unreal Projects\ArtisticSW2026\graph_full.json"

$lines = Get-Content $t3dPath
$objects = @{}
$currentObject = $null
$inCode = $false
$codeStr = ""

foreach ($line in $lines) {
    $trimmed = $line.Trim()
    
    if ($trimmed -match '^Begin Object Name="([^"]+)"') {
        $currentObject = $matches[1]
        $objects[$currentObject] = @{
            "Class" = ""
            "Inputs" = @{}
            "Properties" = @{}
        }
        if ($trimmed -match 'Class=/Script/Engine\.([^ ]+)') {
            $objects[$currentObject]["Class"] = $matches[1]
        }
    }
    elseif ($trimmed -match '^End Object') {
        $currentObject = $null
    }
    elseif ($currentObject -ne $null) {
        if ($trimmed -match '^Code="(.*)') {
            $inCode = $true
            $codeStr = $matches[1]
            if ($codeStr.EndsWith('"') -and -not $codeStr.EndsWith('\"')) {
                $inCode = $false
                $objects[$currentObject]["Properties"]["Code"] = $codeStr.Substring(0, $codeStr.Length - 1)
            }
            continue
        }
        
        if ($inCode) {
            $codeStr += "`n" + $line
            if ($line.TrimEnd().EndsWith('"') -and -not $line.TrimEnd().EndsWith('\"')) {
                $inCode = $false
                $trimmedCodeStr = $codeStr.TrimEnd()
                $objects[$currentObject]["Properties"]["Code"] = $trimmedCodeStr.Substring(0, $trimmedCodeStr.Length - 1)
            }
            continue
        }
        
        if ($trimmed -match '^([A-Za-z0-9_]+(\([0-9]+\))?)=(.*)') {
            $propName = $matches[1]
            $propVal = $matches[3]
            
            # Match Expression="/Script/Engine.MaterialExpressionAdd'M_Realistic_Water:MaterialExpressionAdd_0'"
            if ($propVal -match 'Expression="[^'']+\''([^'']+)\''"') {
                $targetNode = $matches[1]
                if ($targetNode -match ':') {
                    $targetNode = $targetNode.Split(':')[1]
                }
                
                # Try to get InputName
                $inputName = $propName
                if ($propVal -match 'InputName="([^"]+)"') {
                    $inputName = $matches[1]
                }
                
                $objects[$currentObject]["Inputs"][$inputName] = $targetNode
            }
            else {
                $objects[$currentObject]["Properties"][$propName] = $propVal
            }
        }
    }
}

$objects | ConvertTo-Json -Depth 5 | Set-Content $outPath
Write-Host "Done parsing to $outPath"
