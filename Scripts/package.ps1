<#
.SYNOPSIS
    Package a Luma game mod locally, replicating the CI packaging
    (.github/workflows/build_and_release.yml "Create ZIP per mod").

.DESCRIPTION
    Produces the same layout the CI ships: a zip containing the mod's dxgi.dll
    at the root plus a "Luma/" folder (the shaders mount, filtered to this project),
    with the project-level (Core/Textures) folders only included when the
    project opts in via UseLumaFastNoise, plus the runtime DLLs the project
    needs (d3dcompiler_47.dll, NGX DLSS and the FidelityFX DX12 dlls for FSR 4
    when opted in).

.EXAMPLE
    .\scripts\package.ps1 -Project "Final Fantasy XV" -Config "Development-Release" -Platform "x64"
    .\scripts\package.ps1 -Project "Metro Redux" -Config "Publishing-Release" -Platform "x64"
#>
param(
    [string]$Project,
    [ValidateSet("Development-Debug", "Development-Release", "Test-Release", "Publishing-Release")]
    [string]$Config = "Development-Release",
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [string]$OutDir = "",
    [string]$AddonPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent

function Test-PropEnabled([string]$vcxprojPath, [string]$propName) {
    $content = Get-Content $vcxprojPath -Raw
    return $content -match "<$propName>\s*true\s*</$propName>"
}

function Find-ProjectDir {
    param([string]$RepoRoot, [string]$ProjectName)
    foreach ($rel in @("Source\Games\$ProjectName", "Source\Games\_$ProjectName", "Source\$ProjectName")) {
        $dir = Join-Path $RepoRoot $rel
        if (Test-Path $dir -PathType Container) { return $dir }
    }
    return $null
}

function Copy-PackageShaders {
    param([string]$Source, [string]$Destination, [string]$Mount, [string]$RelativePath = "")

    # Enumerate one level at a time so excluded directories are never entered.
    foreach ($entry in Get-ChildItem -LiteralPath $Source) {
        $relative = if ($RelativePath) { Join-Path $RelativePath $entry.Name } else { $entry.Name }
        $destinationPath = Join-Path $Destination $entry.Name
        if ($entry.PSIsContainer) {
            if ($Mount -eq $Project -and $entry.Name -like "Dump*") { continue }
            if ($entry.Name -in @("Unused", "Dev", "Sample")) { continue }
            if (-not $RelativePath -and $Mount -eq "Global" -and $entry.Name -eq "Textures" -and -not $useLumaFastNoise) { continue }
            Copy-PackageShaders -Source $entry.FullName -Destination $destinationPath -Mount $Mount -RelativePath $relative
            continue
        }

        # Exclude compiled dumps even when they sit inside a texture directory.
        if ($entry.Extension -ieq ".cso") { continue }
        # ".h" too, as some shaders include headers (e.g. Deus Ex Mankind Divided's "shared.h")
        $isShader = $entry.Extension -in @(".hlsl", ".hlsli", ".h")
        $isRecipe = $entry.Name.EndsWith('.recipe.yml', [System.StringComparison]::OrdinalIgnoreCase)
        $isTexture = $relative -match '(^|[\\/])Textures[\\/]'
        if (-not ($isShader -or $isRecipe -or $isTexture)) { continue }

        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        Copy-Item -LiteralPath $entry.FullName -Destination $destinationPath -Force
    }
}

$projectDir = Find-ProjectDir -RepoRoot $repoRoot -ProjectName $Project
if (-not $projectDir) {
    Write-Error "Project folder not found (or no vcxproj): Source\Games\$Project"
    exit 1
}
# The vcxproj filename doesn't always match the display name (e.g. _Template, _Generic Mod)
$vcxproj = Get-ChildItem -Path $projectDir -Filter "*.vcxproj" -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vcxproj) {
    Write-Error "Project folder not found (or no vcxproj): $projectDir"
    exit 1
}
$vcxproj = $vcxproj.FullName

# Feature opt-ins, mirroring the CI scan
$useLumaFastNoise = Test-PropEnabled $vcxproj "UseLumaFastNoise"
$useLumaDXP = Test-PropEnabled $vcxproj "UseLumaDXP"
$useLumaNGX = Test-PropEnabled $vcxproj "UseLumaNGX"
$useLumaFSR4 = (Test-PropEnabled $vcxproj "UseLumaFSR") -and (Test-PropEnabled $vcxproj "UseLumaFSR4")
$useLumaXeSS = Test-PropEnabled $vcxproj "UseLumaXeSS"
$useLumaSRBridge = ($Platform -eq "Win32") -and (Test-PropEnabled $vcxproj "UseLumaSRBridge")
Write-Host "Opt-ins: UseLumaFastNoise=$useLumaFastNoise UseLumaDXP=$useLumaDXP UseLumaNGX=$useLumaNGX UseLumaFSR4=$useLumaFSR4 UseLumaXeSS=$useLumaXeSS UseLumaSRBridge=$useLumaSRBridge"

# Locate the mod's dxgi.dll. When packaging runs from the LumaPackage build target the
# exact build output is passed in (-AddonPath = $(TargetPath)); standalone runs
# look in the solution output, "Binaries\<platform>-<config>\<ProjectName>\".
$addonFile = $null
if (-not [string]::IsNullOrEmpty($AddonPath) -and (Test-Path -LiteralPath $AddonPath)) {
    $addonFile = Get-Item -LiteralPath $AddonPath
} else {
    $projectOutputName = Split-Path $projectDir -Leaf
    foreach ($name in @($projectOutputName, $Project, "_$Project") | Select-Object -Unique) {
        $candidate = Join-Path $repoRoot "Binaries\$Platform-$Config\$name\dxgi.dll"
        if (Test-Path -LiteralPath $candidate) { $addonFile = Get-Item -LiteralPath $candidate; break }
    }
}
if (-not $addonFile) {
    Write-Error "dxgi.dll not found for '$Project' (build the project first)"
    exit 1
}
Write-Host "Using mod: $($addonFile.FullName) ($($addonFile.LastWriteTime))"

# Zip name, mirroring the CI
$zipName = "Luma-$Project"
if ($Config -eq "Test-Release") { $zipName += "-Test" }
if ($Config -eq "Development-Release") { $zipName += "-Dev" }
if ($Config -eq "Development-Debug") { $zipName += "-Dev-Dbg" }
if ($Platform -eq "Win32") { $zipName += "-x32" }
$zipName = $zipName -replace ' ', '_'
$zipName += ".zip"

# Temp staging dir (unique per run, in the system temp so interrupted builds
# don't pollute the repo tree)
$tempDir = Join-Path $env:TEMP ("Luma-Package-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path "$tempDir\Luma" -Force | Out-Null

    # Copy only the three package mounts, filtering before copying any files.
    # Never enumerate other games or descend into this game's Dump directories.
    foreach ($mount in @("Global", "Includes", $Project) | Select-Object -Unique) {
        $source = Join-Path $repoRoot "Shaders\$mount"
        if (Test-Path -LiteralPath $source -PathType Container) {
            Write-Host "Staging shaders: $mount"
            Copy-PackageShaders -Source $source -Destination (Join-Path "$tempDir\Luma" $mount) -Mount $mount
        }
    }

    # Runtime DLLs (dxcompiler.dll is intentionally not shipped: Luma only uses the
    #    SM5 recipe path, which is native — DXC/DXIL is only needed once SM6 is used)
    $d3dCompilerSrc = if ($Platform -eq "Win32") { Join-Path $repoRoot "Shaders\Decompiler\d3dcompiler_47_x32.dll" }
                      else { Join-Path $repoRoot "Shaders\Decompiler\d3dcompiler_47.dll" }
    if (Test-Path $d3dCompilerSrc) { Copy-Item $d3dCompilerSrc -Destination "$tempDir\Luma" -Force }
    # The SR bridge's x64 helper (a Luma.sln project, built next to the Win32 addons), which loads the dlls below
    if ($useLumaSRBridge) {
        $helperSrc = Join-Path $repoRoot "Binaries\Win32-$Config\Luma-Upscaler.exe"
        if (-not (Test-Path $helperSrc)) {
            Write-Error "Luma-Upscaler.exe not found: $helperSrc (build the ""SR Bridge Helper"" project)"
            exit 1
        }
        Copy-Item $helperSrc -Destination $tempDir -Force
    }
    if ($useLumaNGX -or $useLumaSRBridge) {
        $ngxSrc = Join-Path $repoRoot "Source\External\NGX\bin\dev\nvngx_dlss.dll"
        if ($Config -notlike "Development*") { $ngxSrc = Join-Path $repoRoot "Source\External\NGX\bin\rel\nvngx_dlss.dll" }
        if (Test-Path $ngxSrc) { Copy-Item $ngxSrc -Destination $tempDir -Force }
    }
    # FSR 4 loads them from next to the addon (x64 only), and so does the SR bridge's helper
    if (($useLumaFSR4 -and $Platform -ne "Win32") -or $useLumaSRBridge) {
        $ffxDir = Join-Path $repoRoot "Source\External\FidelityFX\bin"
        Copy-Item (Join-Path $ffxDir "*.dll") -Destination $tempDir -Force
        New-Item -ItemType Directory -Path "$tempDir\Licenses" -Force | Out-Null
        Copy-Item (Join-Path $ffxDir "FidelityFX_LICENSE.md") -Destination "$tempDir\Licenses" -Force
    }
    # XeSS loads them from next to the addon (x64 only), and so does the SR bridge's helper
    if (($useLumaXeSS -and $Platform -ne "Win32") -or $useLumaSRBridge) {
        $xessDir = Join-Path $repoRoot "Source\External\XeSS"
        Copy-Item (Join-Path $xessDir "bin\libxess.dll"), (Join-Path $xessDir "bin\libxess_dx11.dll") -Destination $tempDir -Force
        New-Item -ItemType Directory -Path "$tempDir\Licenses" -Force | Out-Null
        Copy-Item (Join-Path $xessDir "LICENSE.txt") -Destination "$tempDir\Licenses\XeSS_LICENSE.txt" -Force
        Copy-Item (Join-Path $xessDir "third-party-programs.txt") -Destination "$tempDir\Licenses\XeSS_third-party-programs.txt" -Force
    }

    # The mod at the zip root, as the game loads it
    Copy-Item -LiteralPath $addonFile.FullName -Destination (Join-Path $tempDir "dxgi.dll") -Force

    # Zip — next to the mod by default
    if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = $addonFile.DirectoryName }
    $OutDir = $OutDir.TrimEnd('\')
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    $zipPath = Join-Path $OutDir $zipName
    # Same archive as "Compress-Archive", just faster
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    [IO.Compression.ZipFile]::CreateFromDirectory($tempDir, $zipPath)
    Write-Host "Packaged: $zipPath"
} finally {
    # Only remove the unique staging directory created by this invocation.
    $resolvedTempDir = [IO.Path]::GetFullPath($tempDir)
    $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolvedTempDir.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path $resolvedTempDir -Leaf) -match '^Luma-Package-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolvedTempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
