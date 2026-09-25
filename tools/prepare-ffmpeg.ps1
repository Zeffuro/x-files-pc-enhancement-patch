#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Prefix)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit code $LASTEXITCODE)." }
}

$cache = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.cache/xfiles-enhancement'
$Prefix = [IO.Path]::GetFullPath($Prefix)
if ("$cache$Prefix" -match '\s') {
    $cache = Join-Path (Split-Path $Prefix) 'ffmpeg-source-cache'
}
if ("$cache$Prefix" -match '\s') { throw 'Use -FFmpegRoot with a path without spaces, such as C:/xfiles-deps/ffmpeg.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio Build Tools with Desktop development with C++ and a Windows SDK.'
}
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC x86 build tools were not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$git = (Get-Command git -ErrorAction Stop).Source
$gitRoot = Split-Path (Split-Path $git)
$bash = Join-Path $gitRoot 'bin/bash.exe'
if (-not (Test-Path -LiteralPath $bash)) {
    $execPath = & git --exec-path
    $gitRoot = Split-Path (Split-Path (Split-Path $execPath))
    $bash = Join-Path $gitRoot 'bin/bash.exe'
}
if (-not (Test-Path -LiteralPath $bash)) { throw 'Install Git for Windows, including Git Bash.' }

$makeDirectory = Join-Path $cache 'msys-tools'
$make = Join-Path $makeDirectory 'usr/bin/make.exe'
if (-not (Test-Path -LiteralPath $make)) {
    New-Item -ItemType Directory -Path $makeDirectory -Force | Out-Null
    $archive = Join-Path $makeDirectory 'make.tar.zst'
    $url = 'https://repo.msys2.org/msys/x86_64/make-4.4.1-2-x86_64.pkg.tar.zst'
    Write-Host 'Downloading GNU make from MSYS2...'
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $archive
    $expected = '2408af61717dae87b00c855b132769a125c708907fc94a46bb16dae076113e5c'
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $expected) {
        throw 'GNU make download failed its checksum check.'
    }
    Invoke-Checked 'tar.exe' @('-xf', $archive, '-C', $makeDirectory)
}

$previous = @{}
Get-ChildItem Env: | ForEach-Object { $previous[$_.Name] = $_.Value }
try {
    $environment = & $env:ComSpec /d /c "`"$vcvars`" x64_x86 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the MSVC x86 environment.' }
    foreach ($line in $environment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    $env:MAKE = $make.Replace('\', '/')
    $recipe = Join-Path $PSScriptRoot 'build-ffmpeg.sh'
    New-Item -ItemType Directory -Path $Prefix -Force | Out-Null
    $marker = Join-Path $Prefix 'build-incomplete'
    Set-Content -LiteralPath $marker -Value 'Retry the dependency build before using this prefix.'
    Write-Host 'Building the pinned FFmpeg dependency (first build can take several minutes)...'
    Invoke-Checked $bash @($recipe.Replace('\', '/'), $cache.Replace('\', '/'), $Prefix.Replace('\', '/'))
    Remove-Item -LiteralPath $marker
} finally {
    Get-ChildItem Env: | Where-Object { -not $previous.ContainsKey($_.Name) } |
        ForEach-Object { [Environment]::SetEnvironmentVariable($_.Name, $null, 'Process') }
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
