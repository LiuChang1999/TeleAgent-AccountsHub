param([string]$Executable = (Join-Path $PSScriptRoot 'build-local\v0.7.2\TeleAgentAccounts.exe'),[string]$WebView2Setup = (Join-Path $PSScriptRoot 'build-local\release-tools\MicrosoftEdgeWebview2Setup.exe'))
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $taskVersion = '0.7.2'
    $taskName = "TeleAgent-Account-Desk-v$taskVersion-win64"
    $taskPackage = Join-Path $PSScriptRoot (Join-Path 'dist' $taskName)
    $taskZip = Join-Path $PSScriptRoot (Join-Path 'dist' ($taskName + '.zip'))
    New-Item -ItemType Directory -Path $taskPackage -Force | Out-Null
    # Explicit allowlist: never copy an application directory recursively.
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw '请先运行 build.ps1 生成单文件版本' }
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $taskPackage 'TeleAgentAccounts.exe') -Force
    Copy-Item -LiteralPath 'third_party\webview2\LICENSE.txt' -Destination (Join-Path $taskPackage 'WebView2-LICENSE.txt') -Force
    Copy-Item -LiteralPath "docs\使用说明-v$taskVersion.txt" -Destination (Join-Path $taskPackage '使用说明.txt') -Force
    $taskSignature = Get-AuthenticodeSignature -LiteralPath $WebView2Setup
    if ($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'WebView2 安装器必须具备有效的微软签名' }
    Copy-Item -LiteralPath $WebView2Setup -Destination (Join-Path $taskPackage 'MicrosoftEdgeWebview2Setup.exe') -Force
    Copy-Item -LiteralPath 'third_party\webview2\x64\WebView2Loader.dll' -Destination (Join-Path $taskPackage 'WebView2Loader.dll') -Force
    $taskAllowed = @('TeleAgentAccounts.exe','WebView2-LICENSE.txt','使用说明.txt','MicrosoftEdgeWebview2Setup.exe','WebView2Loader.dll')
    $taskUnexpected = @(Get-ChildItem -LiteralPath $taskPackage -Force | Where-Object { $_.PSIsContainer -or $_.Name -notin $taskAllowed })
    if ($taskUnexpected.Count) { throw '分发目录含有清单外内容，已停止打包。请使用新的干净输出目录。' }
    Compress-Archive -LiteralPath @($taskAllowed | ForEach-Object { Join-Path $taskPackage $_ }) -DestinationPath $taskZip -Force
    if ((Get-Item -LiteralPath $taskZip).Length -ge 10000000) { throw '发布 ZIP 必须小于 10 MB' }
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
