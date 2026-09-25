#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$FFmpegRoot,
    [string]$BuildDirectory = 'build',
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,
    [ValidateRange(1, 128)]
    [int]$Jobs = [Math]::Min(8, [Environment]::ProcessorCount),
    [switch]$Clean,
    [switch]$RebuildFFmpeg
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed (exit code $LASTEXITCODE)."
    }
}

function Read-CacheValue {
    param([string]$Name)
    $cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $match = Select-String -LiteralPath $cache -Pattern "^${Name}:[^=]+=(.*)$" |
            Select-Object -First 1
        if ($match) { return $match.Matches[0].Groups[1].Value }
    }
}

function Test-FFmpeg {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath "$Path/include/libavcodec/avcodec.h")) {
        return $false
    }
    if (Test-Path -LiteralPath "$Path/build-incomplete") { return $false }
    foreach ($component in @('avcodec', 'avutil', 'swresample', 'swscale')) {
        if (@(Get-ChildItem "$Path/bin/$component-*.dll" -ErrorAction SilentlyContinue).Count -ne 1) {
            return $false
        }
    }
    return (Test-Path -LiteralPath "$Path/FFmpeg.LICENSE") -and
        (Test-Path -LiteralPath "$Path/source/build-ffmpeg.sh") -and
        (@(Get-ChildItem "$Path/source/ffmpeg-*.tar.xz" -ErrorAction SilentlyContinue).Count -eq 1)
}

Push-Location $PSScriptRoot
try {
    foreach ($tool in @('cmake', 'ctest', 'cpack', 'git')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "Install $tool and add it to PATH. See docs/building.md."
        }
    }
    $python = 'python'
    $pythonArguments = @()
    if (-not (Get-Command $python -ErrorAction SilentlyContinue)) {
        $python = 'py'
        $pythonArguments = @('-3')
        if (-not (Get-Command $python -ErrorAction SilentlyContinue)) {
            throw 'Install Python 3.9 or newer and add it to PATH.'
        }
    }
    Invoke-Checked $python ($pythonArguments + @('-c', 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)'))

    $formatEnvironment = Join-Path $BuildDirectory 'format-tools'
    $formatPython = Join-Path $formatEnvironment 'Scripts/python.exe'
    $clangFormat = Join-Path $formatEnvironment 'Scripts/clang-format.exe'
    if (-not (Test-Path -LiteralPath $formatPython)) {
        Invoke-Checked $python ($pythonArguments + @('-m', 'venv', $formatEnvironment))
    }
    $formatVersion = ''
    if (Test-Path -LiteralPath $clangFormat) { $formatVersion = & $clangFormat --version }
    if ($formatVersion -ne 'clang-format version 22.1.3') {
        Invoke-Checked $formatPython @('-m', 'pip', '--disable-pip-version-check', 'install', 'clang-format==22.1.3')
    }
    $clangFormat = (Resolve-Path -LiteralPath $clangFormat).Path
    $formatSources = @(Get-ChildItem src, tests, tools -Recurse -File -Include '*.cpp', '*.h' |
        ForEach-Object { Resolve-Path -LiteralPath $_.FullName -Relative })
    Write-Host 'Checking C++ formatting (clang-format 22.1.3)...'
    Invoke-Checked $clangFormat (@('--dry-run', '--Werror') + $formatSources)

    if (Get-Process XFiles -ErrorAction SilentlyContinue) {
        throw 'Close The X-Files before building: the display tests need the game closed.'
    }
    if (-not $Version) {
        $match = Select-String -LiteralPath 'CMakeLists.txt' -Pattern '^set\(XFILES_VERSION "([0-9.]+)"'
        if (-not $match) { throw 'Cannot read the default version from CMakeLists.txt.' }
        $Version = $match.Matches[0].Groups[1].Value
    }
    if (-not $FFmpegRoot) {
        $candidates = @(
            $env:FFMPEG_ROOT
            (Read-CacheValue 'FFMPEG_ROOT')
            (Join-Path ([Environment]::GetFolderPath('UserProfile')) '.cache/xfiles-enhancement/ffmpeg-install')
            'C:/dependencies/ffmpeg'
        )
        $FFmpegRoot = $candidates | Where-Object { Test-FFmpeg $_ } | Select-Object -First 1
    }
    if (-not $FFmpegRoot) {
        $profile = [Environment]::GetFolderPath('UserProfile')
        if ($profile -match '\s') { $profile = $env:PUBLIC }
        $FFmpegRoot = Join-Path $profile '.cache/xfiles-enhancement/ffmpeg-install'
    }
    $FFmpegRoot = [IO.Path]::GetFullPath($FFmpegRoot)
    $recipe = (Get-Content -LiteralPath 'tools/build-ffmpeg.sh' -Raw).Replace("`r`n", "`n")
    $installedRecipe = ''
    if (Test-FFmpeg $FFmpegRoot) {
        $installedRecipe = (Get-Content -LiteralPath "$FFmpegRoot/source/build-ffmpeg.sh" -Raw).Replace("`r`n", "`n")
    }
    if ($RebuildFFmpeg -or $recipe -cne $installedRecipe) {
        & "$PSScriptRoot/tools/prepare-ffmpeg.ps1" -Prefix $FFmpegRoot
        if (-not (Test-FFmpeg $FFmpegRoot)) { throw 'The FFmpeg build did not produce the required files.' }
    }

    Write-Host "Building version $Version with FFmpeg from $FFmpegRoot"
    # Clear cached library paths when the dependency prefix changes.
    Invoke-Checked 'cmake' @('-S', '.', '-B', $BuildDirectory, '-A', 'Win32',
        '-Uffmpeg_*', "-DFFMPEG_ROOT=$FFmpegRoot", "-DXFILES_VERSION=$Version",
        '-DXFILES_CODE_ANALYSIS=ON', "-DCLANG_FORMAT=$clangFormat")
    $buildArguments = @('--build', $BuildDirectory, '--config', 'Release', '--parallel', "$Jobs")
    if ($Clean) { $buildArguments += '--clean-first' }
    Invoke-Checked 'cmake' $buildArguments
    Invoke-Checked 'ctest' @('--test-dir', $BuildDirectory, '-C', 'Release', '--output-on-failure', '--timeout', '180')
    Invoke-Checked 'cpack' @('--config', "$BuildDirectory/CPackConfig.cmake", '-C', 'Release')
    $package = Join-Path $BuildDirectory "packages/xfiles-enhancement-$Version-windows-x86.zip"
    Invoke-Checked $python ($pythonArguments + @('tools/check-package.py', $package, '--version', $Version))
    $hash = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $(Split-Path $package -Leaf)" |
        Set-Content -LiteralPath "$package.sha256" -Encoding ascii
    Write-Host "`nBuild verified: $((Resolve-Path -LiteralPath $package).Path)"
    Write-Host "SHA-256: $hash"
} finally {
    Pop-Location
}
