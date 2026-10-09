# TTPlayerLrcsh

依据原版 `ttp_lrcsh.dll` 伪代码、x86 反汇编和运行对照独立重建的歌词搜索插件。

- 保留 ordinal 1 / `ttpGetSoundAddIn`、两个工厂及原宿主的搜索、下载、回调接口。
- 恢复查询规范化、UTF-16LE 十六进制参数、原版下载 Code、代理与服务器 INI。
- 兼容 NCAB 的裸 `&`、重复 ID、原始 UTF-8 签名和搜索/下载 Cookie 会话。
- 重建播放器的在线搜索全部通过本 DLL；新增可选服务器指定及取消接口，原宿主无需修改。
- HTTPS 优先使用同目录支持 ABI 5 的 `ttp_https.dll`，否则使用系统 WinINet，始终验证证书。

## 构建

在项目目录运行 `./build.ps1 -Package`。需要 Visual Studio 2026 的 x86 C++ 工具和 CMake；构建下载固定版本 VC-LTL 5.3.1、YY-Thunks 1.2.2，进行 XP/Win7 导入检查。

输出：`build/Release/ttp_lrcsh.dll`、`ttp_lrcsh-yyyy.MM.dd[pN].zip`。ZIP 只含 `AddIn/ttp_lrcsh.dll` 和 `SHA256SUMS.txt`。运行时无需安装现代 VC Redistributable；使用系统 MSVCRT，CPU 要求 SSE2。

Action 独立构建，`Release a Version` 使用北京时间日期和递增 `pN`。测试只保留在工作区外部的 `../rebuild/tests/lrcsh_rebuild`，Action 不构建、不运行测试。

## 安装

关闭播放器，将 DLL 放到 `AddIn`。保留原版 DLL 的备份。服务器文件为 DLL 同名 `.ini`，UTF-8 XML，例如：

```xml
<ttp_lrcsvr>
  <server name="NCAB" url="http://localhost:99/"/>
</ttp_lrcsvr>
```

旧宿主保留两个工厂限制；重建版通过 `Control::Configure` 可以使用编辑器内任意条目，而不修改旧接口结构。

兼容范围及有意修正见 [重建说明](docs/RECONSTRUCTION.md)，实际结果和未完成项目见 [验证记录](docs/VALIDATION.md)。不声称逐字节复刻或所有系统均已实测。

最新的逐项差异、原 DLL 对照证据和建议补全顺序见 [原版差异审计](docs/ORIGINAL_PARITY_AUDIT_20261009.md)。

审计后修复、XP / Win7 实测和保留差异见 [兼容修复记录](docs/PARITY_FIXES_20261009.md)。
