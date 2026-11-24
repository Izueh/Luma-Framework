<#
.SYNOPSIS
    Generates Luma2/compile_commands.json for running clang-tidy over Luma 2.0 sources.
#>
[CmdletBinding()]
param(
    [string]$OutFile = (Join-Path (Split-Path -Parent $PSScriptRoot) "Luma2\compile_commands.json"),
    [string]$Game = "Final Fantasy VII Remake"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$luma2Root = Join-Path $repoRoot "Luma2"
$coreDir = Join-Path $luma2Root "Source\Core"
$gameDir = Join-Path $luma2Root "Source\Games\$Game"
$gameResources = Join-Path $luma2Root "Resources\$Game\Shaders"
$globalResources = Join-Path $luma2Root "Resources\Global\Shaders"
$externalDir = Join-Path $luma2Root "External"
$sharedExternalDir = Join-Path $repoRoot "Source\External"

$includes = @(
    $coreDir
    $gameResources
    $globalResources
    (Join-Path $externalDir "imgui")
    (Join-Path $externalDir "glaze\include")
    (Join-Path $externalDir "concurrentqueue")
    (Join-Path $externalDir "Fonts\FontAwesome")
    (Join-Path $externalDir "DirectXTex")
    (Join-Path $externalDir "DirectXTex\Shim")
    (Join-Path $externalDir "doctest")
    (Join-Path $sharedExternalDir "safetyhook\include")
    (Join-Path $sharedExternalDir "zydis\include")
    (Join-Path $sharedExternalDir "zydis\dependencies\zycore\include")
    (Join-Path $sharedExternalDir "NVAPI")
)

$commonArgs = @(
    "clang-cl"
    "/std:c++latest"
    "/EHsc"
    "/W4"
    "/DDEVELOPMENT=1"
    "/DUNICODE"
    "/D_UNICODE"
    "/DWIN32_LEAN_AND_MEAN"
    "/DNOMINMAX"
    "/D_CRT_SECURE_NO_WARNINGS"
    "/DZYDIS_STATIC_BUILD"
    "/DZYCORE_STATIC_BUILD"
    "/DLUMA_USE_NVAPI=1"
    "/Dglz=luma_glz"
    "/DLUMA_PROJECT_NAME=`"$Game`""
    "/DLUMA_RESOURCES_DIR=L`"$($luma2Root.Replace('\', '/'))/Resources`""
)

foreach ($inc in $includes) {
    if (Test-Path $inc) {
        $commonArgs += "/I$inc"
    }
}

$files = @()
$files += Get-ChildItem -Path $coreDir -Recurse -File -Filter "*.cpp" | Where-Object { $_.FullName -notmatch '[/\\]vcpkg_installed[/\\]' }
if (Test-Path $gameDir) {
    $files += Get-ChildItem -Path $gameDir -Recurse -File -Filter "*.cpp" | Where-Object { $_.FullName -notmatch '[/\\]vcpkg_installed[/\\]' }
}

$entries = New-Object System.Collections.Generic.List[object]
foreach ($file in $files) {
    $args = @($commonArgs) + @("/c", $file.FullName)
    $entries.Add([ordered]@{
        directory = $luma2Root
        file      = $file.FullName
        arguments = $args
    })
}

$json = $entries | ConvertTo-Json -Depth 4
Set-Content -Path $OutFile -Value $json -Encoding UTF8
Write-Host "Wrote $($entries.Count) compile commands to $OutFile"
