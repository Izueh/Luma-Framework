<#
.SYNOPSIS
    Packs the "Luma 2.0 Game Mod" Visual Studio project template and installs it for the current user.

.DESCRIPTION
    The template's source is Luma2/Templates/VisualStudio/Game (a folder of files, not a zip: it is edited and reviewed like code).
    A Visual Studio project template is a zip of those files, which Visual Studio finds in
    "Documents\<Visual Studio folder>\Templates\ProjectTemplates". Restart Visual Studio after installing.

    Its resources (shaders, recipes) come from Luma2/Resources/_Template, which the first build of a new game copies to
    Luma2/Resources/<ProjectName>. Run Scripts/Test-Luma2Template.ps1 to check that a project made from the template builds.

.EXAMPLE
    .\Scripts\Install-Luma2Template.ps1
    .\Scripts\Install-Luma2Template.ps1 -NoInstall -OutputZip C:\Temp\Luma2-Game.zip
#>
param(
    # The folders under "Documents" the template goes into. Luma 2.0 builds with the v145 toolset: Visual Studio 2026 is "Visual Studio 18".
    [string[]]$VsFolders = @("Visual Studio 18"),
    # Where to write the zip when not installing (or in addition to it)
    [string]$OutputZip = "",
    [switch]$NoInstall
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repoRoot "Luma2\Templates\VisualStudio\Game"
$zipName = "Luma2-Game.zip"

if (-not (Test-Path (Join-Path $source "MyTemplate.vstemplate"))) {
    throw "The template's source is not at $source"
}

$staging = Join-Path ([IO.Path]::GetTempPath()) ("luma2-template-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    $zip = Join-Path $staging $zipName
    # The files at the root of the zip, as a template needs them
    Compress-Archive -Path (Join-Path $source "*") -DestinationPath $zip -Force

    if ($OutputZip) {
        $destination = [IO.Path]::GetFullPath($OutputZip)
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item $zip $destination -Force
        Write-Host "Wrote $destination"
    }

    if (-not $NoInstall) {
        $documents = [Environment]::GetFolderPath("MyDocuments")
        foreach ($folder in $VsFolders) {
            $templates = Join-Path $documents "$folder\Templates\ProjectTemplates"
            New-Item -ItemType Directory -Force -Path $templates | Out-Null
            $destination = Join-Path $templates $zipName
            Copy-Item $zip $destination -Force
            Write-Host "Installed $destination"
        }
        Write-Host "Restart Visual Studio, then Add > New Project > 'Luma 2.0 Game Mod', in Luma2\Source\Games."
    }
}
finally {
    Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
}
