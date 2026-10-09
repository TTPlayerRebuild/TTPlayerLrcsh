# TTPlayerLrcsh

依据原版 `ttp_lrcsh.dll` 伪代码、x86 反汇编和运行对照独立重建的歌词搜索插件。

- 保留 ordinal 1 / `ttpGetSoundAddIn` 及原宿主的工厂、搜索、下载、回调接口；XML 模式按实际数量枚举服务器。
- 恢复查询规范化、UTF-16LE 十六进制参数、原版下载 Code 和代理；服务器目录优先读取 XML，无 XML 时兼容 INI。
- 兼容 NCAB 的裸 `&`、重复 ID、原始 UTF-8 签名和搜索/下载 Cookie 会话。
- 重建播放器的在线搜索全部通过本 DLL；新增可选服务器指定及取消接口，原宿主无需修改。
- HTTPS 使用同目录支持 ABI 6 的 x86 `ttp_https.dll`；缺少或版本不兼容时明确报错，始终验证证书。

## 构建

在项目目录运行 `./build.ps1 -Package`。需要 Visual Studio 2026 的 x86 C++ 工具和 CMake；构建下载固定版本 VC-LTL 5.3.1、YY-Thunks 1.2.2，进行 XP/Win7 导入检查。

输出：`build/Release/ttp_lrcsh.dll`、`ttp_lrcsh-yyyy.MM.dd[pN].zip`。ZIP 只含 `AddIn/ttp_lrcsh.dll` 和 `SHA256SUMS.txt`。运行时无需安装现代 VC Redistributable；使用系统 MSVCRT，CPU 要求 SSE2。

Action 独立构建，`Release a Version` 使用北京时间日期和递增 `pN`。测试只保留在工作区外部的 `../rebuild/tests/lrcsh_rebuild`，Action 不构建、不运行测试。

## 安装

关闭播放器，将 DLL 放到 `AddIn`，保留原版 DLL 的备份。**原版指原版 TTPlayer.exe 播放器**：无需修改 EXE 或替换原版 ttpcomm.dll。要使用 HTTPS，必须同时部署支持 ABI 6 的 `AddIn/ttp_https.dll`；两个独立发行包需配套安装。

服务器文件使用 DLL 同名文件，通常为 `AddIn/ttp_lrcsh.xml`，内容仍为 UTF-8 XML，例如：

```xml
<ttp_lrcsvr>
  <server name="NCAB" url="http://localhost:99/"/>
</ttp_lrcsvr>
```

- 重建版播放器读写 XML；无 XML 且有 INI 时，先逐字节复制 INI 为 XML，保留原 INI。后续编辑只保存 XML。
- 原版播放器搭配本重建 DLL：有 XML 就优先读取并保持其服务器列表，不发出 `?svrlst` 刷新请求；只有无 XML 时才读取 INI 并沿用原有刷新、保存 INI 的路径。DLL 本身不迁移文件。
- 已有 XML 即使为空、损坏或不可读，也不会被 INI 或远程列表覆盖。修改配置后，重启原版播放器以重新加载。

文件优先级、原版伪代码依据和验证范围见 [XML 目录兼容说明](docs/SERVER_CATALOG_XML_20261009.md)。未经替换的原版 DLL 仍只识别 INI。

HTTPS 地址必须由服务端实际提供，且仍实现千千旧歌词协议；不能仅将现有 HTTP 地址改写为 HTTPS。使用公共受信任证书的服务不需要 CA 配置。自建服务可在本地 XML（或无 XML 时的 INI）的对应 `server` 上添加 `ca_file="my-server-ca.pem"`，并将 PEM 放在同一 AddIn 目录。该文件名不能含路径，仅作用于配置的同源地址（协议、主机、端口均相同）；远端目录不能设置 CA，也不能把已有 HTTPS 槽降为 HTTP。不会修改系统证书库或关闭证书验证。

只有 helper 明确要求使用系统代理时才允许原生网络回退；配置了私有 CA 时拒绝该回退，避免改变证书来源。证书或 TLS 失败不会回退重试。

原版播放器搭配本 DLL 时，XML 模式支持 1～128 个服务器，按 XML 中的顺序显示；只有无 XML 的 INI 模式仍保留原版两个工厂及刷新逻辑。接口布局和 GUID 不变，原版 EXE 与 ttpcomm 无需修改。服务器数量、顺序修改后须重启原版播放器；其保存的是数字索引。重建版继续通过 `Control::Configure` 使用编辑器条目。

多服务器实现、原版 GUI 第三／第八项下载及重启记忆验证见 [解除两项限制](docs/MULTI_SERVER_ORIGINAL_HOST_20261009.md)。

兼容范围及有意修正见 [重建说明](docs/RECONSTRUCTION.md)，实际结果和未完成项目见 [验证记录](docs/VALIDATION.md)。不声称逐字节复刻或所有系统均已实测。

最新的逐项差异、原 DLL 对照证据和建议补全顺序见 [原版差异审计](docs/ORIGINAL_PARITY_AUDIT_20261009.md)。

审计后修复、XP / Win7 实测和保留差异见 [兼容修复记录](docs/PARITY_FIXES_20261009.md)。

修复后的剩余差异、原版宿主 HTTPS 可行性和新增本地传输实测见 [HTTPS 与原版差异续审](docs/HTTPS_ORIGINAL_HOST_AUDIT_20261009.md)。

以上续审建议的实现、原版播放器在 XP／Win7／Win11 的实际 HTTPS 验证及配套发行要求见 [原版播放器 HTTPS 修复记录](docs/HTTPS_ORIGINAL_PLAYER_FIXES_20261009.md)。
