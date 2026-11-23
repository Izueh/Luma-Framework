<#
.SYNOPSIS
    Enforces Luma 2.0 architectural and style rules from Luma2/Docs/Guidelines.md.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$luma2Root = Join-Path $repoRoot "Luma2"
$coreRoot = Join-Path $luma2Root "Source/Core"
$sourceRoot = Join-Path $luma2Root "Source"
$templatesRoot = Join-Path $luma2Root "Templates"

$violations = New-Object System.Collections.Generic.List[string]

function Get-RelPath([string]$fullPath) {
    $normFull = $fullPath -replace '\\', '/'
    $normRoot = ($repoRoot -replace '\\', '/').TrimEnd('/') + '/'
    if ($normFull.StartsWith($normRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $normFull.Substring($normRoot.Length)
    }
    return $normFull
}

$coreFiles = Get-ChildItem -Path $coreRoot -Recurse -File | Where-Object { $_.Extension -in @('.hpp', '.cpp', '.h', '.c') -and $_.FullName -notmatch '[/\\]vcpkg_installed[/\\]' }
$allLuma2CodeFiles = @()
$allLuma2CodeFiles += Get-ChildItem -Path $sourceRoot -Recurse -File | Where-Object { $_.Extension -in @('.hpp', '.cpp', '.h', '.c') -and $_.FullName -notmatch '[/\\]vcpkg_installed[/\\]' }
$allLuma2CodeFiles += Get-ChildItem -Path $templatesRoot -Recurse -File | Where-Object { $_.Extension -in @('.hpp', '.cpp', '.h', '.c') -and $_.FullName -notmatch '[/\\]vcpkg_installed[/\\]' }

# Rule 1: No #include "Luma.hpp" inside Luma2/Source/Core/ except Startup.cpp
foreach ($file in $coreFiles) {
    $rel = Get-RelPath $file.FullName
    if ($rel -eq 'Luma2/Source/Core/Startup.cpp' -or $rel -eq 'Luma2/Source/Core/Luma.hpp') {
        continue
    }
    $lines = Get-Content -Path $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*#\s*include\s*[<"]Luma\.hpp[>"]') {
            $violations.Add("[Rule 1: Core must not include Luma.hpp] ${rel}:$($i + 1)")
        }
    }
}

# Rule 2: No D3D/DXGI/FXC/DXC headers in API-agnostic Core files
$apiAllowedPatterns = @(
    '/D3D11/',
    '/D3D12/',
    '/DXGI/',
    '^Luma2/Source/Core/Game/(GameCallbacks|GameEvents)\.hpp$',
    '^Luma2/Source/Core/Hooks/(Detours|DeviceProxy|HookManager|ObjectLifetime|VtableSlots)\.(hpp|cpp|h|c)$',
    '^Luma2/Source/Core/ReShade/ReShade\.cpp$',
    '^Luma2/Source/Core/Resources/ResourceUpgrades\.hpp$',
    '^Luma2/Source/Core/Shaders/(ShaderCompiler|ShaderDatabase)\.cpp$',
    '^Luma2/Source/Core/System/MonitorDisplay\.cpp$',
    '^Luma2/Source/Core/Trace/DebugDraw\.(hpp|cpp)$',
    '^Luma2/Source/Core/UI/(Overlay|OverlayEncoding)\.(hpp|cpp)$'
)
foreach ($file in $coreFiles) {
    $rel = Get-RelPath $file.FullName
    $allowed = $false
    foreach ($pat in $apiAllowedPatterns) {
        if ($rel -match $pat) {
            $allowed = $true
            break
        }
    }
    if ($allowed) { continue }
    $lines = Get-Content -Path $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*#\s*include\s*[<"](d3d11|d3d12|dxgi|d3dcompiler|dxcapi)') {
            $violations.Add("[Rule 2: API header in API-agnostic file] ${rel}:$($i + 1): $($lines[$i].Trim())")
        }
    }
}

# Rule 3: No namespace Detail or namespace Internal anywhere in Luma2/Source
foreach ($file in $allLuma2CodeFiles) {
    $rel = Get-RelPath $file.FullName
    $lines = Get-Content -Path $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '\bnamespace\s+(Detail|Internal)\b') {
            $violations.Add("[Rule 3: Forbidden Detail/Internal namespace] ${rel}:$($i + 1): $($lines[$i].Trim())")
        }
    }
}

# Rule 4: Unique PascalCase filenames under Luma2/Source/Core
$seenBasenames = @{}
foreach ($file in $coreFiles) {
    $rel = Get-RelPath $file.FullName
    if ($file.Name -cnotmatch '^[A-Z][A-Za-z0-9]*\.(hpp|cpp|h|c)$') {
        $violations.Add("[Rule 4: Non-PascalCase filename] $rel")
    }
    if ($file.Extension -in @('.cpp', '.c')) {
        $lower = $file.Name.ToLowerInvariant()
        if ($seenBasenames.ContainsKey($lower)) {
            $violations.Add("[Rule 4: Duplicate Core translation unit basename] $rel collides with $($seenBasenames[$lower])")
        } else {
            $seenBasenames[$lower] = $rel
        }
    }
}

# Rule 5: No references to Guidelines.md, Plan.md, or Step <N> in code comments
foreach ($file in $allLuma2CodeFiles) {
    $rel = Get-RelPath $file.FullName
    $lines = Get-Content -Path $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '(Guidelines\.md|Plan\.md|\bStep\s+[0-9])') {
            $violations.Add("[Rule 5: Forbidden doc/plan reference in code] ${rel}:$($i + 1): $($lines[$i].Trim())")
        }
    }
}

# Rule 6: No Source/External/reshade references anywhere in Luma2/
$luma2TextFiles = Get-ChildItem -Path $luma2Root -Recurse -File | Where-Object {
    $_.FullName -notmatch '[/\\]External[/\\]' -and
    $_.FullName -notmatch '[/\\]Binaries[/\\]' -and
    $_.FullName -notmatch '[/\\]Docs[/\\]' -and
    $_.FullName -notmatch '[/\\]vcpkg_installed[/\\]' -and
    $_.Extension -in @('.hpp', '.cpp', '.h', '.c', '.props', '.vcxproj', '.vcxitems', '.hlsl', '.rc')
}
foreach ($file in $luma2TextFiles) {
    $rel = Get-RelPath $file.FullName
    $lines = Get-Content -Path $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match 'External[/\\]reshade') {
            $violations.Add("[Rule 6: ReShade submodule reference in Luma2] ${rel}:$($i + 1): $($lines[$i].Trim())")
        }
    }
}

# Rule 7: No non-COM, non-RAII class definitions in Luma2/Source/Core
$raiiClassAllowlist = @(
    'Luma2/Source/Core/Hooks/DepthGuard.hpp:DepthGuard',
    'Luma2/Source/Core/Hooks/HookScope.hpp:Guard',
    'Luma2/Source/Core/Shaders/ShaderCompiler.hpp:Lock'
)
foreach ($file in $coreFiles) {
    $rel = Get-RelPath $file.FullName
    $lines = Get-Content -Path $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)\b(.*)$') {
            $name = $Matches[1]
            $tail = $Matches[2]
            if ($tail -match '^\s*;') { continue } # forward declaration
            if ($tail -match ':\s*public\s+(IUnknown|ID3D|IDXGI|ID3DInclude|IDxc)') { continue } # COM interface
            if ("${rel}:${name}" -in $raiiClassAllowlist) { continue }
            $violations.Add("[Rule 7: Non-COM/non-RAII class in Core] ${rel}:$($i + 1): $($lines[$i].Trim())")
        }
    }
}

if ($violations.Count -gt 0) {
    Write-Host "Luma2 rule check FAILED with $($violations.Count) violation(s):" -ForegroundColor Red
    foreach ($v in $violations) {
        Write-Host "  $v" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Luma2 rule check PASSED ($($coreFiles.Count) Core files, $($allLuma2CodeFiles.Count) total Luma2 source/template files checked)." -ForegroundColor Green
exit 0
