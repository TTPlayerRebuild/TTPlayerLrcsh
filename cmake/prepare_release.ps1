[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$ArtifactDirectory = 'artifact',
    [string]$ServerUrl = 'https://github.com'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = (Get-LrcshBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-LrcshVersionTag $PreviousVersion)) { throw 'Invalid previous release version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Invalid repository or commit.'
}
$archiveName = "ttp_lrcsh-$version.zip"
$hash = (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $archiveName) -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = (Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'SHA256SUMS.txt') -Encoding UTF8 -Raw).Trim()
if ($expected -cne "$hash  $archiveName") { throw 'LRCSH package SHA-256 verification failed.' }
$repoUrl = "$ServerUrl/$Repository"
$logUrl = if ($PreviousVersion) { "$repoUrl/compare/$PreviousVersion...$version" } else { "$repoUrl/commits/$version" }
$notes = @"
**更新记录**: $logUrl

解压 ttp_lrcsh-$version.zip，关闭播放器后，将 AddIn/ttp_lrcsh.dll 放入安装目录。
恢复原版歌词搜索工厂、搜索、下载、代理、服务器配置及回调接口。
重建版可通过可选接口指定服务器和取消请求，旧版宿主无需修改。
同一 x86 DLL 面向 XP SP3、Win7 及新系统，CPU 需要 SSE2。
HTTPS 建议同目录安装支持 ABI 5 的 ttp_https.dll；缺少该版本时使用系统 WinINet。
ZIP 只包含 AddIn/ttp_lrcsh.dll 和 SHA256SUMS.txt，许可证保留在源码仓库。

[源码、兼容范围及验证记录]($repoUrl/tree/$Commit)
[VC-LTL 许可证]($repoUrl/blob/$Commit/docs/licenses/VC-LTL-LICENSE.txt)
[YY-Thunks 许可证]($repoUrl/blob/$Commit/docs/licenses/YY-Thunks-LICENSE.txt)
"@
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8
