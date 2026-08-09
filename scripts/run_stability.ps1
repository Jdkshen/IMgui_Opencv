param(
    [ValidateRange(1, 1440)]
    [int]$DurationMinutes = 480,
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$testExecutable = Join-Path $repoRoot "Test\x64\$Configuration\RegressionTests.exe"
if (-not (Test-Path -LiteralPath $testExecutable)) {
    throw "Regression test executable not found: $testExecutable"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $localData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $localData "IMgui_Opencv\stability\$stamp"
}
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null

$deadline = (Get-Date).AddMinutes($DurationMinutes)
$samples = [System.Collections.Generic.List[object]]::new()
$iteration = 0
$failureCount = 0

while ((Get-Date) -lt $deadline) {
    $iteration++
    $stdoutPath = Join-Path $resolvedOutput ("iteration-{0:D6}.stdout.log" -f $iteration)
    $stderrPath = Join-Path $resolvedOutput ("iteration-{0:D6}.stderr.log" -f $iteration)
    $startedAt = Get-Date
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $testExecutable
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start regression test process"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()

    $peakWorkingSet = 0L
    $peakPrivateBytes = 0L
    $peakThreads = 0
    $peakHandles = 0
    while (-not $process.HasExited) {
        $process.Refresh()
        $peakWorkingSet = [Math]::Max($peakWorkingSet, [int64]$process.WorkingSet64)
        $peakPrivateBytes = [Math]::Max($peakPrivateBytes, [int64]$process.PrivateMemorySize64)
        $peakThreads = [Math]::Max($peakThreads, $process.Threads.Count)
        $peakHandles = [Math]::Max($peakHandles, $process.HandleCount)
        Start-Sleep -Milliseconds 200
    }
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    [System.IO.File]::WriteAllText($stdoutPath, $stdoutTask.Result)
    [System.IO.File]::WriteAllText($stderrPath, $stderrTask.Result)
    $elapsedMs = ((Get-Date) - $startedAt).TotalMilliseconds
    if ($null -eq $exitCode) {
        throw "Regression process exited without a readable exit code"
    }
    if ($exitCode -ne 0) {
        $failureCount++
    }

    $samples.Add([pscustomobject]@{
        Iteration = $iteration
        StartedAt = $startedAt.ToString("o")
        ElapsedMs = [Math]::Round($elapsedMs, 3)
        ExitCode = $exitCode
        PeakWorkingSetBytes = $peakWorkingSet
        PeakPrivateBytes = $peakPrivateBytes
        PeakThreadCount = $peakThreads
        PeakHandleCount = $peakHandles
        CpuSeconds = [Math]::Round($process.TotalProcessorTime.TotalSeconds, 3)
    })
    $samples | Export-Csv -LiteralPath (Join-Path $resolvedOutput "samples.csv") `
        -NoTypeInformation -Encoding UTF8

    if ($exitCode -ne 0) {
        break
    }
}

$ordered = @($samples.ElapsedMs | Sort-Object)
function Get-Percentile([double[]]$values, [double]$percentile) {
    if ($values.Count -eq 0) { return 0.0 }
    $index = [Math]::Ceiling($percentile * $values.Count) - 1
    return $values[[Math]::Max(0, [Math]::Min($index, $values.Count - 1))]
}

$summary = [ordered]@{
    startedAt = if ($samples.Count) { $samples[0].StartedAt } else { $null }
    completedAt = (Get-Date).ToString("o")
    requestedDurationMinutes = $DurationMinutes
    iterations = $iteration
    failures = $failureCount
    averageIterationMs = if ($samples.Count) {
        [Math]::Round(($samples.ElapsedMs | Measure-Object -Average).Average, 3)
    } else { 0 }
    p95IterationMs = [Math]::Round((Get-Percentile $ordered 0.95), 3)
    p99IterationMs = [Math]::Round((Get-Percentile $ordered 0.99), 3)
    maximumWorkingSetBytes = if ($samples.Count) {
        ($samples.PeakWorkingSetBytes | Measure-Object -Maximum).Maximum
    } else { 0 }
    maximumPrivateBytes = if ($samples.Count) {
        ($samples.PeakPrivateBytes | Measure-Object -Maximum).Maximum
    } else { 0 }
    maximumThreadCount = if ($samples.Count) {
        ($samples.PeakThreadCount | Measure-Object -Maximum).Maximum
    } else { 0 }
    maximumHandleCount = if ($samples.Count) {
        ($samples.PeakHandleCount | Measure-Object -Maximum).Maximum
    } else { 0 }
    totalCpuSeconds = if ($samples.Count) {
        [Math]::Round(($samples.CpuSeconds | Measure-Object -Sum).Sum, 3)
    } else { 0 }
}
$summary | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $resolvedOutput "summary.json") -Encoding UTF8

Write-Host ("Stability run complete: {0} iterations, {1} failures" -f `
    $iteration, $failureCount)
Write-Host ("Results: {0}" -f $resolvedOutput)
if ($failureCount -gt 0) { exit 1 }
