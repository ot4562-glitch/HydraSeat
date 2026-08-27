[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,

    [Parameter(Mandatory = $true)]
    [string]$EvidenceDirectory,

    [string]$CMakePath = 'cmake.exe',
    [string]$DumpBinPath = 'dumpbin.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedCommit = 'd9d6f0f1f967807ffade6598ea9a631ebaf37a56'
$ExpectedVersion = '3.5.1'
$ExpectedMask = [uint32]0x0000B93A

function Get-Sha256Hex([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        } finally {
            $sha.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Quote-ProcessArgument([string]$Value) {
    if ($Value -notmatch '[\s\"]') { return $Value }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    $quoted = @($Arguments | ForEach-Object { Quote-ProcessArgument $_ })
    $process = Start-Process -FilePath $Executable -ArgumentList $quoted -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $Executable $($Arguments -join ' ')"
    }
}

$SourceRoot = [System.IO.Path]::GetFullPath($SourceRoot)
$BuildRoot = [System.IO.Path]::GetFullPath($BuildRoot)
$EvidenceDirectory = [System.IO.Path]::GetFullPath($EvidenceDirectory)

if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot '.git'))) {
    throw "GLFW source root is not a Git worktree: $SourceRoot"
}

function Get-GitHeadCommit([string]$Root) {
    $gitDir = Join-Path $Root '.git'
    if (-not (Test-Path -LiteralPath $gitDir -PathType Container)) {
        throw 'P3-E-01 currently requires a normal Git directory, not a linked worktree.'
    }
    $headText = (Get-Content -LiteralPath (Join-Path $gitDir 'HEAD') -Raw).Trim()
    if (-not $headText.StartsWith('ref: ')) {
        return $headText.ToLowerInvariant()
    }
    $refName = $headText.Substring(5).Trim()
    $looseRef = Join-Path $gitDir ($refName -replace '/', [System.IO.Path]::DirectorySeparatorChar)
    if (Test-Path -LiteralPath $looseRef) {
        return (Get-Content -LiteralPath $looseRef -Raw).Trim().ToLowerInvariant()
    }
    $packedRefs = Join-Path $gitDir 'packed-refs'
    if (Test-Path -LiteralPath $packedRefs) {
        foreach ($line in Get-Content -LiteralPath $packedRefs) {
            if ($line.StartsWith('#') -or $line.StartsWith('^') -or [string]::IsNullOrWhiteSpace($line)) { continue }
            $parts = $line.Split(' ')
            if ($parts.Count -eq 2 -and $parts[1] -eq $refName) {
                return $parts[0].Trim().ToLowerInvariant()
            }
        }
    }
    throw "Unable to resolve Git HEAD ref $refName"
}

$head = Get-GitHeadCommit $SourceRoot
if ($head -ne $ExpectedCommit) {
    throw "GLFW source commit mismatch. Expected $ExpectedCommit, got $head"
}

$trackedDiff = Start-Process -FilePath 'git.exe' -ArgumentList @(
    '-C', (Quote-ProcessArgument $SourceRoot), 'diff', '--quiet', '--ignore-submodules', '--'
) -Wait -PassThru -NoNewWindow
if ($trackedDiff.ExitCode -ne 0) {
    throw 'GLFW tracked worktree changes are not allowed before building P3-E-01 evidence.'
}
$stagedDiff = Start-Process -FilePath 'git.exe' -ArgumentList @(
    '-C', (Quote-ProcessArgument $SourceRoot), 'diff', '--cached', '--quiet', '--ignore-submodules', '--'
) -Wait -PassThru -NoNewWindow
if ($stagedDiff.ExitCode -ne 0) {
    throw 'GLFW staged source changes are not allowed before building P3-E-01 evidence.'
}
$statusFile = [System.IO.Path]::GetTempFileName()
try {
    $statusProcess = Start-Process -FilePath 'git.exe' -ArgumentList @(
        '-C', (Quote-ProcessArgument $SourceRoot), 'status', '--porcelain', '--untracked-files=all'
    ) -Wait -PassThru -NoNewWindow -RedirectStandardOutput $statusFile
    if ($statusProcess.ExitCode -ne 0) {
        throw 'Unable to verify the GLFW source worktree status.'
    }
    if ((Get-Item -LiteralPath $statusFile).Length -ne 0) {
        throw 'GLFW source worktree must be fully clean, including untracked files.'
    }
} finally {
    Remove-Item -LiteralPath $statusFile -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null

Invoke-Checked $CMakePath @(
    '-S', $SourceRoot,
    '-B', $BuildRoot,
    '-A', 'x64',
    '-DGLFW_BUILD_EXAMPLES=OFF',
    '-DGLFW_BUILD_TESTS=ON',
    '-DGLFW_BUILD_DOCS=OFF',
    '-DGLFW_INSTALL=OFF'
)
Invoke-Checked $CMakePath @('--build', $BuildRoot, '--config', 'Release', '--target', 'cursor')

$target = Join-Path $BuildRoot 'tests\Release\cursor.exe'
if (-not (Test-Path -LiteralPath $target)) {
    throw "GLFW cursor target was not produced: $target"
}

$dumpBinOutput = Join-Path $EvidenceDirectory 'cursor-imports.txt'
$dumpBinError = Join-Path $EvidenceDirectory 'cursor-imports.stderr.txt'
$dumpBin = Start-Process -FilePath $DumpBinPath -ArgumentList @(
    '/imports', (Quote-ProcessArgument $target)
) -Wait -PassThru -NoNewWindow -RedirectStandardOutput $dumpBinOutput -RedirectStandardError $dumpBinError
if ($dumpBin.ExitCode -ne 0) {
    throw 'dumpbin /imports failed for the GLFW cursor target.'
}
$imports = @(Get-Content -LiteralPath $dumpBinOutput)

$apiBits = [ordered]@{
    'GetAsyncKeyState' = [uint32]0x00000001
    'GetKeyState' = [uint32]0x00000002
    'GetKeyboardState' = [uint32]0x00000004
    'GetCursorPos' = [uint32]0x00000008
    'SetCursorPos' = [uint32]0x00000010
    'ClipCursor' = [uint32]0x00000020
    'GetClipCursor' = [uint32]0x00000040
    'GetForegroundWindow' = [uint32]0x00000080
    'GetActiveWindow' = [uint32]0x00000100
    'GetFocus' = [uint32]0x00000200
    'GetCapture' = [uint32]0x00000400
    'SetCapture' = [uint32]0x00000800
    'ReleaseCapture' = [uint32]0x00001000
    'RegisterRawInputDevices' = [uint32]0x00002000
    'GetRegisteredRawInputDevices' = [uint32]0x00004000
    'GetRawInputData' = [uint32]0x00008000
    'GetRawInputBuffer' = [uint32]0x00010000
}

$mask = [uint32]0
$matched = New-Object System.Collections.Generic.List[string]
$text = $imports -join "`n"
foreach ($entry in $apiBits.GetEnumerator()) {
    if ($text -match ('(?m)\b' + [regex]::Escape([string]$entry.Key) + '\b')) {
        $mask = $mask -bor [uint32]$entry.Value
        $matched.Add([string]$entry.Key)
    }
}
if ($mask -ne $ExpectedMask) {
    throw ('GLFW cursor controlled API mask mismatch. Expected 0x{0:X8}, got 0x{1:X8}. APIs: {2}' -f $ExpectedMask, $mask, ($matched -join ', '))
}

$manifest = [ordered]@{
    schema_version = 1
    profile_id = 'glfw-3.5.1-cursor-test'
    source = [ordered]@{
        project = 'GLFW'
        version = $ExpectedVersion
        commit = $ExpectedCommit
        license = 'zlib/libpng'
        source_root = $SourceRoot
    }
    target = [ordered]@{
        path = $target
        architecture = 'x64'
        sha256 = Get-Sha256Hex $target
        required_api_mask = ('0x{0:X8}' -f $mask).ToLowerInvariant()
        controlled_imports = @($matched)
    }
    provenance = [ordered]@{
        external_source_vendored = $false
        source_download_performed_by_script = $false
    }
}

$manifestPath = Join-Path $EvidenceDirectory 'glfw-build-manifest.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Output "P3E_GLFW_PREPARED manifest=$manifestPath target=$target sha256=$($manifest.target.sha256) mask=$($manifest.target.required_api_mask)"
