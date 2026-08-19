# Restore Casual Formal outfit files to the package paths baked into the .uassets.
# OS-moving them to Content/Outfits/casual_formal broke soft refs, so MHC won't wear WI.

$ErrorActionPreference = "Stop"
$proj = "D:\UE Projects\MetaHuman_Baseline_UE58_Test"
$src  = Join-Path $proj "Content\Outfits\casual_formal"
$costumeRoot = Join-Path $proj "Content\MetaHumans\Costume"
$nested = Join-Path $costumeRoot "casual_formal"

function Ensure-Dir([string]$path) {
    if (-not (Test-Path $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
}

function Move-Asset([string]$from, [string]$toDir) {
    Ensure-Dir $toDir
    $name = Split-Path $from -Leaf
    $dest = Join-Path $toDir $name
    if (-not (Test-Path $from)) {
        Write-Host "SKIP missing: $from"
        return
    }
    if ((Test-Path $dest) -and ((Resolve-Path $from).Path -ne (Resolve-Path $dest).Path)) {
        Remove-Item $dest -Force
    }
    Move-Item -LiteralPath $from -Destination $dest -Force
    Write-Host "MOVED $name -> $toDir"
}

Write-Host "Source: $src"
if (-not (Test-Path $src)) { throw "Source folder missing: $src" }

Ensure-Dir $costumeRoot
Ensure-Dir $nested

# OA lives at Costume root (declared /Game/MetaHumans/Costume/OA_Casual_formal)
Move-Asset (Join-Path $src "OA_Casual_formal.uasset") $costumeRoot
$oaUe = Join-Path $src "OA_Casual_formal.uexp"
if (Test-Path $oaUe) { Move-Asset $oaUe $costumeRoot }

# Nested content declared under /Game/MetaHumans/Costume/casual_formal/...
foreach ($sub in @("ClothAssets", "Materials", "Meshes", "textures")) {
    $fromSub = Join-Path $src $sub
    $toSub = Join-Path $nested $sub
    if (Test-Path $fromSub) {
        Ensure-Dir (Split-Path $toSub -Parent)
        if (Test-Path $toSub) {
            # merge
            Get-ChildItem $fromSub -Recurse -File | ForEach-Object {
                $rel = $_.FullName.Substring($fromSub.Length).TrimStart("\")
                $destFile = Join-Path $toSub $rel
                Ensure-Dir (Split-Path $destFile -Parent)
                Move-Item -LiteralPath $_.FullName -Destination $destFile -Force
            }
            Remove-Item $fromSub -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "MERGED $sub -> $toSub"
        } else {
            Move-Item -LiteralPath $fromSub -Destination $toSub -Force
            Write-Host "MOVED folder $sub -> $nested"
        }
    }
}

Move-Asset (Join-Path $src "DF_Casual_formal.uasset") $nested
$dfUe = Join-Path $src "DF_Casual_formal.uexp"
if (Test-Path $dfUe) { Move-Asset $dfUe $nested }

# WI package path is already /Game/Outfits/casual_formal/WI_OA_Casual_formal — leave it.
# Its PrincipalAsset soft ref still targets /Game/MetaHumans/Costume/OA_Casual_formal (restored above).

Write-Host ""
Write-Host "Done. Expected:"
Write-Host "  WI:  Content/Outfits/casual_formal/WI_OA_Casual_formal"
Write-Host "  OA:  Content/MetaHumans/Costume/OA_Casual_formal"
Write-Host "  DF+: Content/MetaHumans/Costume/casual_formal/..."
Write-Host "Close/reopen Content Browser, then drag WI_OA_Casual_formal onto Outfit Clothing and click Wear."
