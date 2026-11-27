[CmdletBinding()]
param(
    [string]$Version = '1.0',
    [switch]$SkipBuild,
    [switch]$SkipSmoke
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$releaseDir = Join-Path $repoRoot 'release'
$distDir = Join-Path $repoRoot 'dist'

if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.bat') final
    if ($LASTEXITCODE -ne 0) { throw "Build falhou com código $LASTEXITCODE." }
}

if (-not $SkipSmoke) {
    & (Join-Path $PSScriptRoot 'smoke-engine.ps1') -ReleaseDirectory $releaseDir
}

$required = @(
    'KTGameBooster.exe',
    'ktengine.dll',
    'gamemode.toml',
    'ktgam.ico',
    'README.md'
)
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $releaseDir $name))) {
        throw "Arquivo obrigatório ausente no release: $name"
    }
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
$packageName = "KTGameBooster-v$Version-win-x64"
$stageDir = Join-Path $distDir $packageName
$zipPath = Join-Path $distDir "$packageName.zip"
$hashPath = "$zipPath.sha256"
$exePath = Join-Path $distDir "$packageName.exe"
$exeHashPath = "$exePath.sha256"

if (-not $stageDir.StartsWith($distDir, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Diretório temporário de pacote fora de dist.'
}

if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stageDir | Out-Null

Copy-Item -LiteralPath (Join-Path $releaseDir 'KTGameBooster.exe') -Destination $stageDir -Force
Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $zipPath -CompressionLevel Optimal -Force
Copy-Item -LiteralPath (Join-Path $releaseDir 'KTGameBooster.exe') -Destination $exePath -Force

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
Set-Content -LiteralPath $hashPath -Value "$hash  $packageName.zip" -Encoding ascii
$exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
Set-Content -LiteralPath $exeHashPath -Value "$exeHash  $packageName.exe" -Encoding ascii

[pscustomobject]@{
    Package = $zipPath
    SHA256 = $hash
    StandaloneExe = $exePath
    StandaloneSHA256 = $exeHash
    Bytes = (Get-Item -LiteralPath $zipPath).Length
}
