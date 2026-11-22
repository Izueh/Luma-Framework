<#
.SYNOPSIS
    Checks that a project made from the "Luma 2.0 Game Mod" Visual Studio template builds, with and without its examples.

.DESCRIPTION
    Does what Visual Studio does with the template (unpacks it with its parameters replaced, in Luma2/Source/Games), builds the
    new project (the first build copies Luma2/Resources/_Template to its Resources folder), then turns on the examples section of Main.cpp
    (LUMA_EXAMPLES=1 in Main.cpp) and builds again, so the stubbed events and callbacks are checked against the current game API. Removes
    everything it made. Run it after any change to the game API or to the template, and before committing one.

.EXAMPLE
    .\Scripts\Test-Luma2Template.ps1
    .\Scripts\Test-Luma2Template.ps1 -Config Publishing-Release -Keep
#>
param(
    [ValidateSet("Development-Debug", "Development-Release", "Test-Release", "Publishing-Release")]
    [string]$Config = "Development-Release",
    [string]$Name = "TemplateCheck",
    # Leave the project, its resources and its binaries in place, to look at them
    [switch]$Keep
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$luma2 = Join-Path $repoRoot "Luma2"
$projectDir = Join-Path $luma2 "Source\Games\$Name"
$resourcesDir = Join-Path $luma2 "Resources\$Name"

foreach ($path in @($projectDir, $resourcesDir)) {
    if (Test-Path $path) { throw "$path exists: it is not made by this script, pick another -Name or remove it" }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found" }

$staging = Join-Path ([IO.Path]::GetTempPath()) ("luma2-template-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $staging | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)

function Build([string]$label) {
    Write-Host "== Building ($label)"
    $log = Join-Path $staging "build-$label.log"
    & $msbuild (Join-Path $projectDir "$Name.vcxproj") /nologo /m /v:minimal /p:Configuration=$Config /p:Platform=x64 /fl "/flp:logfile=$log;verbosity=normal" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Build ($label) failed, log: $log" }
    $warnings = Select-String -Path $log -Pattern "warning " | Where-Object { $_.Line -match [regex]::Escape($projectDir) } | Select-Object -ExpandProperty Line -Unique
    if ($warnings) {
        Write-Host "Warnings in the template's files:"
        $warnings | ForEach-Object { Write-Host "  $_" }
    }
}

try {
    # Unpack as Visual Studio would: the files of the template with their target names, the parameters replaced where it says so
    $zip = Join-Path $staging "template.zip"
    & (Join-Path $PSScriptRoot "Install-Luma2Template.ps1") -NoInstall -OutputZip $zip | Out-Null
    $unpacked = Join-Path $staging "unpacked"
    Expand-Archive $zip $unpacked
    [xml]$template = Get-Content (Join-Path $unpacked "MyTemplate.vstemplate") -Raw
    $ns = New-Object Xml.XmlNamespaceManager($template.NameTable)
    $ns.AddNamespace("t", "http://schemas.microsoft.com/developer/vstemplate/2005")
    $parameters = @{
        '$projectname$'     = $Name
        '$safeprojectname$' = $Name
        '$guid1$'           = [Guid]::NewGuid().ToString().ToUpper()
    }
    New-Item -ItemType Directory -Path $projectDir | Out-Null
    $items = @($template.SelectSingleNode("//t:Project", $ns)) + @($template.SelectNodes("//t:ProjectItem", $ns))
    foreach ($item in $items) {
        $sourceName = if ($item.Name -eq "Project") { $item.GetAttribute("File") } else { $item.InnerText }
        $targetName = $item.GetAttribute("TargetFileName")
        $replace = $item.GetAttribute("ReplaceParameters") -eq "true"
        $text = Get-Content (Join-Path $unpacked $sourceName) -Raw
        foreach ($parameter in $parameters.Keys) {
            $targetName = $targetName.Replace($parameter, $parameters[$parameter])
            if ($replace) { $text = $text.Replace($parameter, $parameters[$parameter]) }
        }
        [IO.File]::WriteAllText((Join-Path $projectDir $targetName), $text, $utf8)
    }

    Build "template"

    # The examples: every event, callback and patcher of the examples section compiled against the current API
    $examples = Join-Path $projectDir "Main.cpp"
    $text = Get-Content $examples -Raw
    if ($text -notmatch "#define LUMA_EXAMPLES 0") { throw "Main.cpp has no '#define LUMA_EXAMPLES 0'" }
    [IO.File]::WriteAllText($examples, $text.Replace("#define LUMA_EXAMPLES 0", "#define LUMA_EXAMPLES 1"), $utf8)
    Build "examples"

    Write-Host "The template builds."
}
finally {
    if (-not $Keep) {
        foreach ($path in @($projectDir, $resourcesDir,
                (Join-Path $luma2 "Binaries\$Config\$Name"), (Join-Path $luma2 "Binaries\Intermediate\$Name"))) {
            Remove-Item $path -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
}
