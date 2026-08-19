# Cinch Godfrey Loose_Biker_Boots in Blender (headless).
# Run after Scripts/export_godfrey_boots_for_cinch.py has written Saved/BootCinch/*.fbx

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$InFbx = Join-Path $ProjectRoot "Saved\BootCinch\MHC_CaptainGodfrey_Outfits.fbx"
$OutFbx = Join-Path $ProjectRoot "Saved\BootCinch\MHC_CaptainGodfrey_Outfits_CinchedBoots.fbx"
$BlenderPy = Join-Path $PSScriptRoot "blender_cinch_godfrey_boots.py"
$Amount = if ($args.Count -ge 1) { $args[0] } else { "0.58" }

if (-not (Test-Path -LiteralPath $InFbx)) {
    throw "Missing $InFbx. Run Tools > Execute Python Script > Scripts/export_godfrey_boots_for_cinch.py first."
}
if (-not (Test-Path -LiteralPath $BlenderPy)) {
    throw "Missing $BlenderPy"
}

$blender = $null
$explicit = @(
    "D:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    "D:\Program Files\Blender Foundation\Blender.exe"
)
foreach ($path in $explicit) {
    if (Test-Path -LiteralPath $path) {
        $blender = $path
        break
    }
}
$candidates = @()
foreach ($root in @(
        "D:\Program Files\Blender Foundation",
        "${env:ProgramFiles}\Blender Foundation",
        "${env:ProgramFiles(x86)}\Blender Foundation",
        "$env:LOCALAPPDATA\Programs\Blender Foundation"
    )) {
    if (Test-Path $root) {
        $candidates += Get-ChildItem $root -Recurse -Filter blender.exe -ErrorAction SilentlyContinue
    }
}
if (-not $blender) {
    $cmd = Get-Command blender -ErrorAction SilentlyContinue
    if ($cmd) { $blender = $cmd.Source }
}
if (-not $blender -and $candidates) {
    $blender = ($candidates | Sort-Object FullName -Descending | Select-Object -First 1).FullName
}
if (-not $blender) {
    throw "Blender not found. Expected D:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
}

Write-Host "Blender: $blender"
Write-Host "In:      $InFbx"
Write-Host "Out:     $OutFbx"
Write-Host "Amount:  $Amount  (smaller = tighter)"

& $blender -b --python $BlenderPy -- --in $InFbx --out $OutFbx --amount $Amount
if ($LASTEXITCODE -ne 0) {
    throw "Blender cinch failed (exit $LASTEXITCODE)"
}
if (-not (Test-Path -LiteralPath $OutFbx)) {
    throw "Expected output missing: $OutFbx"
}
Write-Host "PASS $OutFbx"
Write-Host "Next: Tools > Execute Python Script > Scripts/apply_godfrey_cinched_boots.py"
