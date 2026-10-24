[CmdletBinding()]
param(
    [string]$Version = '1.0',
    [switch]$SkipPackage,
    [switch]$SkipBuild,
    [switch]$SkipSmoke
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$distDir = Join-Path $repoRoot 'dist'

if (-not $SkipPackage) {
    $packageArgs = @{ Version = $Version }
    if ($SkipBuild) { $packageArgs.SkipBuild = $true }
    if ($SkipSmoke) { $packageArgs.SkipSmoke = $true }
    & (Join-Path $PSScriptRoot 'package-release.ps1') @packageArgs
}

$compilerCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
    (Join-Path $env:ProgramFiles 'NSIS\makensis.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$compiler = $compilerCandidates | Select-Object -First 1
if (-not $compiler) {
    throw 'NSIS 3 não encontrado. Instale com: winget install --id NSIS.NSIS --exact'
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
$installerPath = Join-Path $distDir "KTGameBooster-v$Version-Setup.exe"
$hashPath = "$installerPath.sha256"
$scriptPath = Join-Path $repoRoot 'installer\KTGameBooster.nsi'
$versionParts = $Version -split '[^0-9]+' | Where-Object { $_ -ne '' }
if ($versionParts.Count -eq 0 -or $versionParts.Count -gt 4) {
    throw 'A versão do instalador deve ter entre um e quatro componentes numéricos (ex.: 1.0.1).'
}
$fileVersion = (($versionParts + @('0','0','0','0'))[0..3] -join '.')

Push-Location $repoRoot
try {
    & $compiler /V2 "/DAPP_VERSION=$Version" "/DAPP_FILE_VERSION=$fileVersion" "/DOUTPUT_FILE=$installerPath" $scriptPath
    if ($LASTEXITCODE -ne 0) { throw "Compilação do instalador falhou com código $LASTEXITCODE." }
}
finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $installerPath)) { throw 'O instalador não foi gerado.' }
$hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
Set-Content -LiteralPath $hashPath -Value "$hash  $(Split-Path $installerPath -Leaf)" -Encoding ascii

[pscustomobject]@{
    Installer = $installerPath
    SHA256 = $hash
    Bytes = (Get-Item -LiteralPath $installerPath).Length
}
