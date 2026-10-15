[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Build", "Rebuild", "Flash")]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [string]$Project,

    [Parameter(Mandatory = $true)]
    [string]$Target
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$KeilExe = "D:\keliv5\UV4\UV4.exe"

function Get-KeilTargetInfo {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$TargetName
    )

    [xml]$projectXml = Get-Content -LiteralPath $ProjectPath -Raw
    $targetNode = @($projectXml.Project.Targets.Target) |
        Where-Object { $_.TargetName -eq $TargetName } |
        Select-Object -First 1

    if ($null -eq $targetNode) {
        $availableTargets = @($projectXml.Project.Targets.Target) |
            ForEach-Object { $_.TargetName }
        throw "Keil target '$TargetName' was not found. Available targets: $($availableTargets -join ', ')"
    }

    $projectDirectory = Split-Path -Parent $ProjectPath
    $outputDirectory = [string]$targetNode.TargetOption.TargetCommonOption.OutputDirectory
    $outputName = [string]$targetNode.TargetOption.TargetCommonOption.OutputName

    if ([string]::IsNullOrWhiteSpace($outputDirectory) -or
        [string]::IsNullOrWhiteSpace($outputName)) {
        throw "Target '$TargetName' does not contain a valid Keil output path."
    }

    [pscustomobject]@{
        TargetName = $TargetName
        AxFPath = Join-Path $projectDirectory (Join-Path $outputDirectory ($outputName + ".axf"))
        HexPath = Join-Path $projectDirectory (Join-Path $outputDirectory ($outputName + ".hex"))
    }
}

function Invoke-KeilCommand {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("-b", "-r", "-f")]
        [string]$Mode,

        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$TargetName,

        [Parameter(Mandatory = $true)]
        [string]$LogPrefix
    )

    $projectDirectory = Split-Path -Parent $ProjectPath
    $logDirectory = Join-Path $projectDirectory "MDK-ARM"
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null

    $safeTargetName = $TargetName -replace '[^A-Za-z0-9_.-]', '_'
    $logFileName = "VSCode_{0}_{1}.log" -f $LogPrefix, $safeTargetName
    $logPath = Join-Path $logDirectory $logFileName
    $relativeLogPath = Join-Path "MDK-ARM" $logFileName

    if (Test-Path -LiteralPath $logPath) {
        Remove-Item -LiteralPath $logPath -Force
    }

    $arguments = @(
        $Mode,
        ('"{0}"' -f $ProjectPath),
        "-t",
        ('"{0}"' -f $TargetName),
        "-j0",
        "-o",
        ('"{0}"' -f $relativeLogPath)
    )

    $process = Start-Process `
        -FilePath $KeilExe `
        -ArgumentList $arguments `
        -WorkingDirectory $projectDirectory `
        -WindowStyle Hidden `
        -Wait `
        -PassThru

    for ($attempt = 0; $attempt -lt 20 -and -not (Test-Path -LiteralPath $logPath); $attempt++) {
        Start-Sleep -Milliseconds 100
    }

    if (-not (Test-Path -LiteralPath $logPath)) {
        throw "Keil did not create the expected log: $logPath (process exit code: $($process.ExitCode))"
    }

    $logText = Get-Content -LiteralPath $logPath -Raw -Encoding Default
    Write-Host $logText

    [pscustomobject]@{
        ExitCode = $process.ExitCode
        LogPath = $logPath
        Text = $logText
    }
}

function Assert-BuildSucceeded {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Result,

        [Parameter(Mandatory = $true)]
        [object]$TargetInfo
    )

    if ($Result.Text -match '(?m)([1-9][0-9]*) Error\(s\)' -or
        $Result.Text -notmatch '0 Error\(s\)') {
        throw "Keil compilation failed. See: $($Result.LogPath)"
    }

    if (-not (Test-Path -LiteralPath $TargetInfo.AxFPath)) {
        throw "Keil reported success, but the AXF file is missing: $($TargetInfo.AxFPath)"
    }
}

function Assert-FlashSucceeded {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Result
    )

    $knownFailures = @(
        'No target connected',
        'Flash Download failed',
        'Target DLL has been cancelled',
        'Error: Flash'
    )

    foreach ($failure in $knownFailures) {
        if ($Result.Text -match [regex]::Escape($failure)) {
            throw "Keil download failed: $failure. See: $($Result.LogPath)"
        }
    }

    if ($Result.Text -notmatch 'Flash Load finished') {
        throw "Keil did not report 'Flash Load finished'. See: $($Result.LogPath)"
    }
}

try {
    if (-not (Test-Path -LiteralPath $KeilExe -PathType Leaf)) {
        throw "Keil uVision was not found: $KeilExe"
    }

    $projectPath = [System.IO.Path]::GetFullPath($Project)
    if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
        throw "Keil project was not found: $projectPath"
    }

    $targetInfo = Get-KeilTargetInfo -ProjectPath $projectPath -TargetName $Target
    Write-Host "[Keil] Action : $Action"
    Write-Host "[Keil] Project: $projectPath"
    Write-Host "[Keil] Target : $Target"

    switch ($Action) {
        "Build" {
            $buildResult = Invoke-KeilCommand -Mode "-b" -ProjectPath $projectPath -TargetName $Target -LogPrefix "Build"
            Assert-BuildSucceeded -Result $buildResult -TargetInfo $targetInfo
            Write-Host "[Keil] BUILD PASSED: $($targetInfo.AxFPath)" -ForegroundColor Green
        }
        "Rebuild" {
            $buildResult = Invoke-KeilCommand -Mode "-r" -ProjectPath $projectPath -TargetName $Target -LogPrefix "Rebuild"
            Assert-BuildSucceeded -Result $buildResult -TargetInfo $targetInfo
            Write-Host "[Keil] REBUILD PASSED: $($targetInfo.AxFPath)" -ForegroundColor Green
        }
        "Flash" {
            Write-Host "[Keil] Step 1/2: build the latest source"
            $buildResult = Invoke-KeilCommand -Mode "-b" -ProjectPath $projectPath -TargetName $Target -LogPrefix "BeforeFlash"
            Assert-BuildSucceeded -Result $buildResult -TargetInfo $targetInfo

            Write-Host "[Keil] Step 2/2: download and verify with ST-Link"
            $flashResult = Invoke-KeilCommand -Mode "-f" -ProjectPath $projectPath -TargetName $Target -LogPrefix "Flash"
            Assert-FlashSucceeded -Result $flashResult
            Write-Host "[Keil] FLASH PASSED: $Target" -ForegroundColor Green
        }
    }
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}

exit 0
