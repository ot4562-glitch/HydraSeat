#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$BuildRoot,
    [string]$OutputDirectory,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$SigningManifestPath = Join-Path $RepositoryRoot "config\release-signing-manifest.json"
$ExpectedArtifacts = @(
    [ordered]@{ id = "setup-bootstrap"; kind = "cmake-executable"; target = "HydraSeatSetup"; fileName = "HydraSeatSetup.exe" },
    [ordered]@{ id = "main-ui"; kind = "cmake-executable"; target = "HydraSeat"; fileName = "HydraSeat.exe" },
    [ordered]@{ id = "host"; kind = "cmake-executable"; target = "hydra_host"; fileName = "hydra_host.exe" },
    [ordered]@{ id = "seat-ui"; kind = "cmake-executable"; target = "hydra_seat_ui"; fileName = "hydra_seat_ui.exe" },
    [ordered]@{ id = "watchdog"; kind = "cmake-executable"; target = "hydra_watchdog"; fileName = "hydra_watchdog.exe" },
    [ordered]@{ id = "reset"; kind = "cmake-executable"; target = "hydra_reset"; fileName = "hydra_reset.exe" },
    [ordered]@{ id = "profile-cli"; kind = "cmake-executable"; target = "hydraseat_profilectl"; fileName = "hydraseat_profilectl.exe" },
    [ordered]@{ id = "community-validator"; kind = "cmake-executable"; target = "hydraseat_community_validate"; fileName = "hydraseat_community_validate.exe" },
    [ordered]@{ id = "installer-script"; kind = "powershell-script"; sourcePath = "tools/install_hydraseat.ps1"; fileName = "install_hydraseat.ps1" }
)

function Resolve-UnderRoot {
    param([string]$Root, [string]$Child)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $candidate = [IO.Path]::GetFullPath((Join-Path $Root $Child))
    if (-not $candidate.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Installer package path escapes its reviewed root"
    }
    return $candidate
}

function Assert-NormalDirectory {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label is missing"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a reparse point"
    }
}

function Assert-NormalFile {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must be a normal file and not a reparse point"
    }
}

function Assert-X64PortableExecutable {
    param([string]$Path)
    Assert-NormalFile -Path $Path -Label "Reviewed x64 release executable"
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 256 -or $stream.Length -gt 1073741824) {
            throw "Reviewed release executable size is invalid"
        }
        $reader = New-Object IO.BinaryReader($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5A4D) {
                throw "Reviewed release executable is not a PE image"
            }
            $stream.Position = 0x3c
            $peOffset = [UInt32]$reader.ReadInt32()
            if ($peOffset -gt ($stream.Length - 6)) {
                throw "Reviewed release executable PE header is out of bounds"
            }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) {
                throw "Reviewed release executable PE signature is invalid"
            }
            if ($reader.ReadUInt16() -ne 0x8664) {
                throw "Reviewed release executable is not x64"
            }
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Assert-ExactReviewedManifest {
    param($Manifest)
    if ($null -eq $Manifest -or $Manifest.schemaVersion -ne 1) {
        throw "Signing manifest schemaVersion must be 1"
    }
    $artifacts = @($Manifest.artifacts)
    if ($artifacts.Count -ne $ExpectedArtifacts.Count) {
        throw "Signing manifest does not contain the exact installer package artifact set"
    }
    for ($index = 0; $index -lt $ExpectedArtifacts.Count; ++$index) {
        $actual = $artifacts[$index]
        $expected = $ExpectedArtifacts[$index]
        foreach ($field in @("id", "kind", "fileName")) {
            if ([string]$actual.$field -ne [string]$expected.$field) {
                throw "Signing manifest installer package artifact order/identity drifted"
            }
        }
        $architectures = @($actual.architectures)
        if ($architectures.Count -ne 1 -or [string]$architectures[0] -ne "x64") {
            throw "Installer package supports only the reviewed x64 architecture"
        }
        if ([string]$expected.kind -eq "cmake-executable") {
            if ([string]$actual.target -ne [string]$expected.target) {
                throw "Signing manifest CMake target differs from the reviewed installer package set"
            }
        } else {
            if ([string]$actual.sourcePath -ne [string]$expected.sourcePath) {
                throw "Signing manifest installer script source differs from the reviewed path"
            }
        }
    }
}

function Assert-ExactEntrySet {
    param([string]$Directory, [string[]]$ExpectedNames, [string]$Label)
    Assert-NormalDirectory -Path $Directory -Label $Label
    $entries = @(Get-ChildItem -LiteralPath $Directory -Force)
    if ($entries.Count -ne $ExpectedNames.Count) {
        throw "$Label contains missing or unexpected entries"
    }
    $actual = @($entries | ForEach-Object { $_.Name.ToLowerInvariant() } | Sort-Object)
    $expected = @($ExpectedNames | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object)
    if ([string]::Join('|', $actual) -ne [string]::Join('|', $expected)) {
        throw "$Label contains missing or unexpected entries"
    }
}

function Invoke-ContractSelfTest {
    $good = [ordered]@{
        schemaVersion = 1
        artifacts = @($ExpectedArtifacts | ForEach-Object {
            $copy = [ordered]@{
                id = $_.id
                kind = $_.kind
                fileName = $_.fileName
                architectures = @("x64")
            }
            if ($_.kind -eq "cmake-executable") {
                $copy.target = $_.target
            } else {
                $copy.sourcePath = $_.sourcePath
            }
            [pscustomobject]$copy
        })
    }
    Assert-ExactReviewedManifest -Manifest ([pscustomobject]$good)

    $missing = [ordered]@{ schemaVersion = 1; artifacts = @($good.artifacts | Select-Object -Skip 1) }
    try {
        Assert-ExactReviewedManifest -Manifest ([pscustomobject]$missing)
        throw "Self-test failed to reject a missing reviewed package artifact"
    } catch {
        if ($_.Exception.Message -eq "Self-test failed to reject a missing reviewed package artifact") { throw }
    }

    $extraArtifacts = @($good.artifacts) + @([pscustomobject]@{
        id = "unexpected"; kind = "cmake-executable"; target = "unexpected";
        fileName = "unexpected.exe"; architectures = @("x64")
    })
    $extra = [ordered]@{ schemaVersion = 1; artifacts = $extraArtifacts }
    try {
        Assert-ExactReviewedManifest -Manifest ([pscustomobject]$extra)
        throw "Self-test failed to reject an unexpected package artifact"
    } catch {
        if ($_.Exception.Message -eq "Self-test failed to reject an unexpected package artifact") { throw }
    }

    $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("HydraSeatInstallerPackageSelfTest-" + [Guid]::NewGuid().ToString("N"))
    $fixtureX64 = Join-Path $fixtureRoot "x64"
    New-Item -ItemType Directory -Path $fixtureX64 -Force | Out-Null
    try {
        $expectedNames = @($ExpectedArtifacts | ForEach-Object { [string]$_.fileName })
        foreach ($name in $expectedNames) {
            Set-Content -LiteralPath (Join-Path $fixtureX64 $name) -Value "fixture" -Encoding ASCII
        }
        Assert-ExactEntrySet -Directory $fixtureX64 -ExpectedNames $expectedNames `
            -Label "Self-test exact x64 package"

        Remove-Item -LiteralPath (Join-Path $fixtureX64 "HydraSeatSetup.exe") -Force
        try {
            Assert-ExactEntrySet -Directory $fixtureX64 -ExpectedNames $expectedNames `
                -Label "Self-test missing bootstrapper package"
            throw "Self-test failed to reject a missing staged package artifact"
        } catch {
            if ($_.Exception.Message -eq "Self-test failed to reject a missing staged package artifact") { throw }
        }

        Set-Content -LiteralPath (Join-Path $fixtureX64 "HydraSeatSetup.exe") -Value "fixture" -Encoding ASCII
        Set-Content -LiteralPath (Join-Path $fixtureX64 "unexpected.exe") -Value "fixture" -Encoding ASCII
        try {
            Assert-ExactEntrySet -Directory $fixtureX64 -ExpectedNames $expectedNames `
                -Label "Self-test unexpected package artifact"
            throw "Self-test failed to reject an unexpected staged package artifact"
        } catch {
            if ($_.Exception.Message -eq "Self-test failed to reject an unexpected staged package artifact") { throw }
        }
    } finally {
        if (Test-Path -LiteralPath $fixtureRoot) {
            Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
        }
    }

    Write-Host "Installer package builder contract self-test passed."
}

if ($SelfTest) {
    Invoke-ContractSelfTest
    exit 0
}

if ([string]::IsNullOrWhiteSpace($BuildRoot) -or
    [string]::IsNullOrWhiteSpace($OutputDirectory)) {
    throw "BuildRoot and OutputDirectory are required outside SelfTest"
}

$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
Assert-NormalDirectory -Path $BuildRoot -Label "Explicit Release build root"
$releaseDirectory = Resolve-UnderRoot -Root $BuildRoot -Child "Release"
Assert-NormalDirectory -Path $releaseDirectory -Label "Explicit Release configuration directory"
Assert-NormalFile -Path $SigningManifestPath -Label "Reviewed release signing manifest"
$manifest = Get-Content -LiteralPath $SigningManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-ExactReviewedManifest -Manifest $manifest

if (Test-Path -LiteralPath $OutputDirectory) {
    Assert-NormalDirectory -Path $OutputDirectory -Label "Installer package output directory"
    if (@(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count -ne 0) {
        throw "Installer package output directory must be empty; refusing to delete or merge unrelated files"
    }
} else {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

$architectureOutput = Resolve-UnderRoot -Root $OutputDirectory -Child "x64"
New-Item -ItemType Directory -Path $architectureOutput | Out-Null

foreach ($artifact in $ExpectedArtifacts) {
    $fileName = [string]$artifact.fileName
    if ([string]$artifact.kind -eq "cmake-executable") {
        $source = Resolve-UnderRoot -Root $releaseDirectory -Child $fileName
        Assert-X64PortableExecutable -Path $source
    } else {
        $source = Resolve-UnderRoot -Root $RepositoryRoot -Child ([string]$artifact.sourcePath)
        Assert-NormalFile -Path $source -Label "Reviewed PowerShell installer source"
    }
    $destination = Resolve-UnderRoot -Root $architectureOutput -Child $fileName
    Copy-Item -LiteralPath $source -Destination $destination
    Assert-NormalFile -Path $destination -Label "Staged reviewed installer package file"
}

Assert-ExactEntrySet -Directory $architectureOutput `
    -ExpectedNames @($ExpectedArtifacts | ForEach-Object { [string]$_.fileName }) `
    -Label "Staged x64 installer package"
Assert-ExactEntrySet -Directory $OutputDirectory -ExpectedNames @("x64") `
    -Label "Installer package root"

Write-Host "Staged $($ExpectedArtifacts.Count) reviewed unsigned installer package files."
Write-Host "No signatures or signing provenance were manufactured by this builder."
