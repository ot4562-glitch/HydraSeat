[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildManifest,

    [Parameter(Mandatory = $true)]
    [string]$HarnessPath,

    [Parameter(Mandatory = $true)]
    [string]$BridgePath,

    [Parameter(Mandatory = $true)]
    [string]$EvidenceDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedProfile = 'glfw-3.5.1-cursor-test'
$ExpectedVersion = '3.5.1'
$ExpectedCommit = 'd9d6f0f1f967807ffade6598ea9a631ebaf37a56'
$ExpectedMask = '0x0000b93a'

function Quote-ProcessArgument([string]$Value) {
    if ($Value -notmatch '[\s\"]') { return $Value }
    return '"' + $Value.Replace('"', '\"') + '"'
}

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

function Get-ExactIdentityState([int]$ProcessId, [uint64]$CreationTime100ns) {
    try {
        $process = [System.Diagnostics.Process]::GetProcessById($ProcessId)
        $currentCreation = [uint64]$process.StartTime.ToFileTimeUtc()
        return [ordered]@{
            pid = $ProcessId
            expected_creation_time_100ns = $CreationTime100ns
            pid_exists = $true
            same_identity_alive = ($currentCreation -eq $CreationTime100ns)
            pid_reused = ($currentCreation -ne $CreationTime100ns)
            current_creation_time_100ns = $currentCreation
            current_process_name = $process.ProcessName
        }
    } catch {
        return [ordered]@{
            pid = $ProcessId
            expected_creation_time_100ns = $CreationTime100ns
            pid_exists = $false
            same_identity_alive = $false
            pid_reused = $false
            current_creation_time_100ns = $null
            current_process_name = $null
        }
    }
}

$BuildManifest = [System.IO.Path]::GetFullPath($BuildManifest)
$HarnessPath = [System.IO.Path]::GetFullPath($HarnessPath)
$BridgePath = [System.IO.Path]::GetFullPath($BridgePath)
$EvidenceDirectory = [System.IO.Path]::GetFullPath($EvidenceDirectory)

if (-not (Test-Path -LiteralPath $BuildManifest)) { throw "Build manifest missing: $BuildManifest" }
if (-not (Test-Path -LiteralPath $HarnessPath)) { throw "Harness missing: $HarnessPath" }
if (-not (Test-Path -LiteralPath $BridgePath)) { throw "Bridge missing: $BridgePath" }
$artifactDirectory = Split-Path -Parent $BridgePath
$adapterPath = Join-Path $artifactDirectory 'hydra_gate_c_adapter.dll'
$shimPath = Join-Path $artifactDirectory 'hydra_gate_c_shim.dll'
if (-not (Test-Path -LiteralPath $adapterPath)) { throw "Adapter DLL missing: $adapterPath" }
if (-not (Test-Path -LiteralPath $shimPath)) { throw "Shim DLL missing: $shimPath" }

$build = Get-Content -LiteralPath $BuildManifest -Raw | ConvertFrom-Json
if ([string]$build.profile_id -ne $ExpectedProfile -or
    [string]$build.source.version -ne $ExpectedVersion -or
    [string]$build.source.commit -ne $ExpectedCommit -or
    [string]$build.target.architecture -ne 'x64' -or
    ([string]$build.target.required_api_mask).ToLowerInvariant() -ne $ExpectedMask) {
    throw 'Build manifest does not match the pinned P3-E-01 GLFW profile.'
}
if ([bool]$build.provenance.external_source_vendored -or
    [bool]$build.provenance.source_download_performed_by_script) {
    throw 'P3-E-01 evidence must keep external GLFW source outside the HydraSeat repository and must not hide a download step.'
}

$target = [System.IO.Path]::GetFullPath([string]$build.target.path)
if (-not (Test-Path -LiteralPath $target)) { throw "Target missing: $target" }
$targetShaBefore = Get-Sha256Hex $target
if ($targetShaBefore -ne ([string]$build.target.sha256).ToLowerInvariant()) {
    throw "Target SHA-256 mismatch before acceptance. Expected $($build.target.sha256), got $targetShaBefore"
}

New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null
$runDirectory = Join-Path $EvidenceDirectory ('run-' + (Get-Date).ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

$harnessSha = Get-Sha256Hex $HarnessPath
$bridgeSha = Get-Sha256Hex $BridgePath
$adapterSha = Get-Sha256Hex $adapterPath
$shimSha = Get-Sha256Hex $shimPath

$harnessStdout = Join-Path $runDirectory 'harness.stdout.txt'
$harnessStderr = Join-Path $runDirectory 'harness.stderr.txt'
$harnessProcess = Start-Process -FilePath $HarnessPath -ArgumentList @(
    '--target', (Quote-ProcessArgument $target),
    '--bridge', (Quote-ProcessArgument $BridgePath),
    '--output-dir', (Quote-ProcessArgument $runDirectory)
) -Wait -PassThru -NoNewWindow -RedirectStandardOutput $harnessStdout -RedirectStandardError $harnessStderr
$harnessExitCode = $harnessProcess.ExitCode
if ($harnessExitCode -ne 0) {
    if (Test-Path -LiteralPath $harnessStdout) {
        Write-Host '--- P3-E-01 harness stdout ---'
        Get-Content -LiteralPath $harnessStdout
    }
    if (Test-Path -LiteralPath $harnessStderr) {
        Write-Host '--- P3-E-01 harness stderr ---'
        Get-Content -LiteralPath $harnessStderr
    }
    $failedReportPath = Join-Path $runDirectory 'p3-e-01-report.json'
    if (Test-Path -LiteralPath $failedReportPath) {
        Write-Host '--- P3-E-01 failed report ---'
        Get-Content -LiteralPath $failedReportPath
    }
    throw "P3-E-01 harness failed with exit code $harnessExitCode. Evidence: $runDirectory"
}

$reportPath = Join-Path $runDirectory 'p3-e-01-report.json'
if (-not (Test-Path -LiteralPath $reportPath)) { throw 'Harness did not create p3-e-01-report.json.' }
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json

$reportPass = [bool]$report.pass -and
    [string]$report.profile_id -eq $ExpectedProfile -and
    [string]$report.glfw_version -eq $ExpectedVersion -and
    [string]$report.glfw_commit -eq $ExpectedCommit -and
    ([string]$report.required_api_mask).ToLowerInvariant() -eq $ExpectedMask -and
    [bool]$report.kernel_job_kill_on_close -and
    [int]$report.declared_events_per_seat -eq 4 -and
    [int]$report.seat1_expected_event_count -eq 4 -and
    [int]$report.seat2_expected_event_count -eq 4 -and
    [int]$report.seat1_cross_event_count -eq 0 -and
    [int]$report.seat2_cross_event_count -eq 0 -and
    [int]$report.receiver_verified_events -eq 8 -and
    [bool]$report.adapter_state_separated -and
    [bool]$report.forced_guard_cleanup_pass -and
    [bool]$report.native_relaunch_pass
if (-not $reportPass) { throw 'P3-E-01 report failed one or more exact acceptance conditions.' }

$targetShaAfter = Get-Sha256Hex $target
if ($targetShaAfter -ne $targetShaBefore) {
    throw 'External target bytes changed during P3-E-01 acceptance.'
}

$identityStates = @(
    Get-ExactIdentityState ([int]$report.seat1_pid) ([uint64]$report.seat1_creation_time_100ns)
    Get-ExactIdentityState ([int]$report.seat2_pid) ([uint64]$report.seat2_creation_time_100ns)
    Get-ExactIdentityState ([int]$report.forced_guard_pid) ([uint64]$report.forced_guard_creation_time_100ns)
)
$aliveOwnedIdentity = @($identityStates | Where-Object { [bool]$_.same_identity_alive })
if ($aliveOwnedIdentity.Count -ne 0 -or -not $harnessProcess.HasExited) {
    throw 'P3-E-01 left an exact owned process identity alive after acceptance.'
}

$acceptance = [ordered]@{
    schema_version = 1
    classification = 'REAL OPEN-SOURCE APPLICATION / CONTROLLED EXTERNAL PROCESS'
    profile_id = $ExpectedProfile
    timestamp_local = (Get-Date).ToString('o')
    source = [ordered]@{
        project = [string]$build.source.project
        version = [string]$build.source.version
        commit = [string]$build.source.commit
        license = [string]$build.source.license
        external_source_vendored = [bool]$build.provenance.external_source_vendored
    }
    target = [ordered]@{
        path = $target
        sha256_before = $targetShaBefore
        sha256_after = $targetShaAfter
        required_api_mask = $ExpectedMask
    }
    hydra = [ordered]@{
        harness_path = $HarnessPath
        harness_sha256 = $harnessSha
        bridge_path = $BridgePath
        bridge_sha256 = $bridgeSha
        adapter_path = $adapterPath
        adapter_sha256 = $adapterSha
        shim_path = $shimPath
        shim_sha256 = $shimSha
        kernel_job_kill_on_close = [bool]$report.kernel_job_kill_on_close
    }
    measurement = [ordered]@{
        declared_events_per_seat = [int]$report.declared_events_per_seat
        seat1_expected_event_count = [int]$report.seat1_expected_event_count
        seat2_expected_event_count = [int]$report.seat2_expected_event_count
        seat1_cross_event_count = [int]$report.seat1_cross_event_count
        seat2_cross_event_count = [int]$report.seat2_cross_event_count
        receiver_verified_events = [int]$report.receiver_verified_events
        adapter_state_separated = [bool]$report.adapter_state_separated
        forced_guard_cleanup_pass = [bool]$report.forced_guard_cleanup_pass
        native_relaunch_pass = [bool]$report.native_relaunch_pass
    }
    exact_process_identity_states = $identityStates
    harness_exit_code = $harnessExitCode
    report_path = $reportPath
    pass = $true
}

$acceptancePath = Join-Path $runDirectory 'p3-e-01-acceptance-manifest.json'
$acceptance | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $acceptancePath -Encoding UTF8
Write-Output "P3E_GLFW_PASS manifest=$acceptancePath report=$reportPath target_sha256=$targetShaAfter"
