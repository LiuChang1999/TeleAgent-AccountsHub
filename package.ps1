param([string]$Executable = (Join-Path $PSScriptRoot 'build-local\v0.7.1\TeleAgentAccounts.exe'))
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $taskVersion = '0.7.1'
    $taskName = "TeleAgent-Account-Desk-v$taskVersion-win64"
    $taskPackage = Join-Path $PSScriptRoot (Join-Path 'dist' $taskName)
    $taskZip = Join-Path $PSScriptRoot (Join-Path 'dist' ($taskName + '.zip'))
    New-Item -ItemType Directory -Path $taskPackage -Force | Out-Null
    # Explicit allowlist: never copy an application directory recursively.
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw '请先运行 build.ps1 生成单文件版本' }
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $taskPackage 'TeleAgentAccounts.exe') -Force
    Copy-Item -LiteralPath 'third_party\webview2\LICENSE.txt' -Destination (Join-Path $taskPackage 'WebView2-LICENSE.txt') -Force
    Copy-Item -LiteralPath "docs\使用说明-v$taskVersion.txt" -Destination (Join-Path $taskPackage '使用说明.txt') -Force
    $taskAllowed = @('TeleAgentAccounts.exe','WebView2-LICENSE.txt','使用说明.txt')
    $taskUnexpected = @(Get-ChildItem -LiteralPath $taskPackage -Force | Where-Object { $_.PSIsContainer -or $_.Name -notin $taskAllowed })
    if ($taskUnexpected.Count) { throw '分发目录含有清单外内容，已停止打包。请使用新的干净输出目录。' }
    Compress-Archive -LiteralPath @($taskAllowed | ForEach-Object { Join-Path $taskPackage $_ }) -DestinationPath $taskZip -Force
    $taskSingleExe = Join-Path $PSScriptRoot (Join-Path 'dist' ($taskName + '.exe'))
    Copy-Item -LiteralPath $Executable -Destination $taskSingleExe -Force
    $taskSums = Join-Path $PSScriptRoot 'dist\SHA256SUMS.txt'
    $taskLines = @($taskZip,$taskSingleExe) | ForEach-Object {
        $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLower()
        "$hash  $(Split-Path -Leaf $_)"
    }
    Set-Content -LiteralPath $taskSums -Value $taskLines -Encoding ASCII
    Get-FileHash -LiteralPath $taskZip -Algorithm SHA256 | Format-List
    Write-Host "已生成 $taskZip"
} finally { Pop-Location }
