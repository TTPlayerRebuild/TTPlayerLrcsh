# ttp_lrcsh 原版差异续审与原版宿主 HTTPS 支持

日期：2026-10-09。基于 `2026.10.09p1` 修复后的源码和实际 DLL。

**后续状态：** 本文保留 p1 的审计证据；列出的 HTTPS 修复建议已在 p2 实施。原版播放器（原 TTPlayer.exe、原 ttpcomm.dll）在 XP／Win7／Win11 的实际 HTTPS 验证及剩余边界见 [修复记录](HTTPS_ORIGINAL_PLAYER_FIXES_20261009.md)。下文的“当前”指审计时的 p1，不应作为 p2 的状态。

本轮完成源码、原版伪代码、反汇编及本地差分检查，新增私有测试和本文；未修改生产实现、替换发行产物或发布版本。此前已修复的问题见 [兼容修复记录](PARITY_FIXES_20261009.md)，不要把 [初次审计](ORIGINAL_PARITY_AUDIT_20261009.md) 的全部问题重新当作当前缺陷。

## 1. 结论

**可以通过重建的 ttp_lrcsh 为原版播放器增加歌词搜索／下载的 HTTPS 支持，不需要修改原版 EXE。当前已经有实际实现，但还不能称为完整验证完成。**

需要同时满足：

1. 原版宿主加载 x86 重建 `AddIn/ttp_lrcsh.dll`，保持原工厂、GUID、虚表、调用约定和内存释放契约。
2. 同目录提供支持 ABI 5 的 x86 `ttp_https.dll`，避免仅依赖旧系统 WinINet 的 TLS 能力。
3. 同名 INI 指向真正提供 HTTPS、且实现千千旧歌词协议的服务端。
4. 补齐本文列出的响应边界，并完成原版 GUI 到 HTTPS 服务的整链路验证。

目前不是只有设计方案：在不调用扩展 Configure 的旧 Search 接口下，已观测到重建插件实际调用 HTTPS 组件发出 TLS ClientHello。此前 XP／Win7 的组件测试完成了 TLS 1.2／1.3 加密请求；此前原版 GUI 完成了重建歌词 DLL 的 HTTP 搜索、下载及显示。**这两组结果不能合并表述为“原版 GUI 的 HTTPS 已全部验证”。**

## 2. 审计基线

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| 原版 `../AddIn/ttp_lrcsh.dll` | 38,400 | `e2b6e0aacb9ef39dca1bb80865e1fe83a4c993a071c5887a4fc32251c8f8021d` |
| 当前 `build/Release/ttp_lrcsh.dll` | 131,072 | `cac155f1ad2f65fea0f884f9585a0a1195e79a422bced6d923fab693e0161cb4` |
| 当前 `../mbedtlsmin/build/Release/AddIn/ttp_https.dll` | 428,544 | `6caac84a71fb79494bde1737ec34e8facf434a2f65a185959d1fc1326fe4c5c6` |

原始证据位于本地：

- `../rebuild/tests/lyrics/ncab/pseudo/ttp_lrcsh.dll.pseudo.c`
- `../rebuild/tests/ttpcomm_analysis_20261007/lrcsh-disassembly.txt`
- `../reverse/decompiled/TTPlayer.exe.pseudo.c`

新增测试及输出均在 `../rebuild/tests/lrcsh_rebuild/`：`https_wire_audit.py`、`https-wire-results.json`、`https_response_probe.cpp`、`https_response_audit.py`、`https-response-results.json`、`catalog_edge_audit.py`、`catalog-edge-results.json`。这些文件不加入插件仓库、不上传、不接入 Action。

## 3. 原版实际如何联网

### 3.1 宿主不负责插件的网络请求

原宿主 `004C88D9 / CSoundAddInModule_CreateInstance` 加载 DLL 并调用 `ttpGetSoundAddIn`。它通过旧 Search 接口提供初始化参数、歌手和歌名，再接收服务器名、候选结果、歌词及错误回调。

服务器地址处理、搜索 URL、签名、请求和正文解析都在歌词插件内。原宿主无需了解 TLS 库，也不需要调用新增 Control 接口。原版 `ttpcomm.dll` 可以继续承担原来的职责；增加歌词 HTTPS 不以替换它为前提。

原 GUI 回调 `0043B332 → 0043BAA3` 和关闭路径 `0043BB8D → 004045F3` 仍有同步消息／生命周期约束，新增传输必须继续遵守。

### 3.2 原 DLL 对直接 HTTPS 地址没有正确启用 TLS

| 原版入口 | 证据 |
|---|---|
| `60351420` | 目录刷新只比较地址前七字节是否为 `http://`，不刷新 `https://` 目录 |
| `60351585` | InternetCrackUrl 后按主机和端口 InternetConnect，但 HttpOpenRequest 只使用调用者标志 `OR 0x00400000` |
| `603514AC` | 目录刷新调用传入 `0x00000200` |
| `60352640`、`60352CE3` | 搜索、下载调用传入 `0x84000200` |

这些直接请求没有添加 `INTERNET_FLAG_SECURE`；`0x00400000` 是 KEEP_CONNECTION，不能因为 URL 包含 `https://` 或使用 443 端口，就认为已启用 TLS。微软明确由 SECURE 标志启用安全请求语义，见 [HttpOpenRequestA 文档](https://learn.microsoft.com/en-us/windows/win32/api/wininet/nf-wininet-httpopenrequesta)。

本轮用回环地址和故意不支持 TLS 的监听器验证：

| DLL／传输 | 直接请求 `https://127.0.0.1:端口/lyrics` 时的线上行为 |
|---|---|
| 原 DLL | 发出明文 `GET /lyrics?sh?...`；接受监听器返回的明文结果并继续明文下载 |
| 重建 DLL + ABI 5 helper | 发出 `16 03 03 ...` TLS ClientHello；拒绝明文监听器，没有发送明文 GET |
| 重建 DLL，无 helper | 本机原生分支连接后无应用数据即失败；没有捕获到明文 GET，也没有捕获到 ClientHello，不能写成 TLS 握手成功 |

测试使用旧工厂／Search／INI 接口，没有 Configure；证明旧宿主调用方式可进入新 TLS 传输。监听器故意不完成握手，因此不是 HTTPS 搜索成功测试。

结论限定在本 DLL 的直接目录／搜索／下载路径；不据此断言原播放器其它模块或 WinINet 自动重定向的全部行为。

## 4. 当前已恢复的原版部分

以下依据既有修复和测试，本轮复核源码；并非本轮重新运行所有历史矩阵。

| 范围 | 当前状态 |
|---|---|
| 导出及工厂 | ordinal 1 / `ttpGetSoundAddIn`、两个 Creator、原 GUID、x86 stdcall 与 CoTaskMem 契约已恢复 |
| 查询规范化 | 编号前缀、括号／标点处理、简体与小写映射、UTF-16LE 十六进制参数已恢复；尚无覆盖所有字符和区域设置的穷举证据 |
| 候选与签名 | 保留结果顺序、重复 ID、每行 artist/title；32 位 ID 环绕及签名输入的五种命名实体规则已修复 |
| 下载 | 旧 Code、UTF-8 文本、特殊换行修复、tt-title／tt-url 头和服务器 errmsg 已恢复 |
| NCAB | 捕获响应中的裸 &、重复 ID、正确行选择、搜索／下载 Cookie、重定向已适配；不能据此认定 NCAB 服务端已提供 HTTPS |
| 命令和生命周期 | 空 artist、初始化失败重试、忙碌替换、过期结果丢弃、同步宿主回调和释放等待已修复 |
| 目录 | 单项更新保留另一槽、extra-only 会话更新、未知 XML 属性／第三服务器保留、失败不破坏原 INI 已实现 |
| HTTP | Search 内复用 session、原 Accept／Referer、201／206、跨跳转期限已实现；206 检查还没有覆盖 helper 分支 |

原版只有两个工厂，不能把“没有第三个工厂”列为未恢复。现代宿主可通过可选 Configure 使用自定义服务。歌词显示、编辑、时间轴、自动匹配和本地关联属于宿主职责。

## 5. 当前 HTTPS 实现与使用条件

```text
原版 TTPlayer.exe
  → 原 Search 接口／同名 INI
  → 重建 ttp_lrcsh：旧搜索和下载协议
  → Transport::Get
      HTTP  → WinINet
      HTTPS → 同目录 ttp_https.dll 的 ABI 5 exchange
                → TCP／可选代理 → Mbed TLS → 服务端
  → 结果／歌词回调 → 原版列表和歌词显示
```

`src/transport.cpp:138` 从歌词 DLL 目录定位 helper，检查 `get_api(5)`、ABI 版本、结构大小及函数指针。HTTPS 支持不依赖原宿主识别 Configure。

- helper 不存在、无法加载、不支持 ABI 5，或明确返回需要原生代理处理时，回退 WinINet。回退后仍请求 HTTPS，不主动改成 HTTP，但 XP 能否连接现代站点将取决于旧系统能力。
- helper 已尝试 TLS 并返回证书／连接错误时，插件报错，不通过原生重试规避失败。
- helper 固定 Mbed TLS 4.2.0／TF-PSA-Crypto 1.2.0，默认 TLS 1.2～1.3；启用证书链及主机名验证，使用内置 Mozilla/curl 2026-08-13 CA 集合。这是本地源码版本，不是全球最新版声明。
- helper ABI 支持显式私有 CA，但歌词插件没有传入、也没有对应 INI 配置。把自签 CA 安装到系统证书库不会自动使该 helper 信任它。此前私有 CA 测试只把 CA 传给探针请求。
- 原生 WinINet 与 helper 的代理、证书来源和 HTTP 实现不同，不能仅以“HTTP 成功”推断“HTTPS 成功”。

旧协议仍然要求：

```text
搜索：?sh?Artist=<UTF-16LE hex>&Title=<UTF-16LE hex>&Flags=0&
结果：带 id／artist／title 属性的 XML
下载：?dl?Id=<有符号整数>&Code=<旧签名>&
正文：UTF-8 歌词；可带 tt-title／tt-url
```

HTTPS 只升级传输；不会自动把旧协议变为现代站点的 JSON API，也不会恢复已停服的历史域名。是否可把某个 NCAB 地址改为 HTTPS，需要验证具体服务的 TLS、证书、端口和协议；本轮没有对公网 NCAB 服务作此声明。本插件也不会为原版的音频流播放、更新检查或其它插件统一增加 HTTPS。

## 6. 本轮确认的 HTTPS 缺口

### 6.1 响应状态、范围与正文处理：应优先修复

`src/transport.cpp:194` 只在 Native 检查 Content-Range。`include/ttp_https.h` 的 ABI 5 没有该字段，Portable 拿到任何 2xx 正文后无法做同样校验。

同时，`../mbedtlsmin/src/https_client.cpp:431` 的 ABI 5 路径取得响应头后，无论 204、304、重定向或错误状态，均进入 ReadBody。后果已通过实际 TLS 1.2 请求确认：

| 本地服务响应 | 当前 ABI 5 结果 | 对歌词流程的影响 |
|---|---|---|
| 200 + 完整正文 | 成功，status=200 | 正常基线 |
| 206 + 完整 Content-Range | 成功，status=206 | 正常基线 |
| 206 + `bytes 0-22/46`，只有 23 字节 | 仍成功，返回 23 字节 | 插件按成功正文解析；格式碰巧有效时可能接受不完整歌词／结果 |
| 302 + Location + 空正文 | 成功返回跳转地址 | 正常基线 |
| 302 + Location + gzip 正文 | `Unexpected HTTPS content encoding`，status/Location 均未交给调用者 | 丢失已收到的有效跳转 |
| 302 + Location + Content-Length 超过 2 MiB | body length 错误，status/Location 丢失 | 跳转说明正文阻断跳转 |
| 404 + Content-Length 超过 2 MiB | body length 错误，status=0 | 丢失原 HTTP 错误语义 |
| 204，无正文，连接暂不关闭 | 继续等正文，1.5 秒探针期限后取消 | 本应立即完成的响应错误等待 |
| 304，无正文，连接暂不关闭 | 同上 | 应立即交给调用者处理；不意味着把 304 当歌词成功 |

206 的“组件接受部分内容”本身可以是合法通用传输行为，缺陷在于插件没有拿到范围信息却把它当完整歌词。其后续影响依据生产 Portable／Get 代码；本轮 TLS 响应测试直接调用 helper，没有伪装成原版 GUI 测试。

204／304 在头结束后即结束消息，不以连接关闭判定正文完成；参见 [RFC 9112 §6.3](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)。1.5 秒是探针取消期限，不是生产默认超时。

### 6.2 HTTPS 请求头未恢复到原版

| 请求头 | 原生歌词分支 | 当前 HTTPS ABI 5 |
|---|---|---|
| User-Agent | 原 MSIE 6.0 字符串 | `TTPlayerRebuild/Lyrics` |
| Accept | 原 image／html／xml／通配列表 | `*/*` |
| Referer | scheme://host/ | 无 |
| Accept-Encoding | WinINet 解码能力决定 | 明确请求 identity，仅接受 identity |
| Connection | KEEP_CONNECTION | close |

本地 TLS 服务实际收到的请求头与源码一致。ABI 4 虽可指定 UA／Accept，却不能代替 ABI 5 的逐跳 Cookie 接口；不能靠强制转换混用。依赖旧 UA／Referer 的服务可能只在 HTTPS 分支失败，这是兼容风险，尚未确认某个公网服务确实受影响。

### 6.3 HTTPS 配置可能被服务器目录改回 HTTP

源码路径：Search 未 Configure → Catalog::Refresh → 请求第一服务的 `?svrlst&` → Parse 接受 HTTP 或 HTTPS → 覆盖服务槽并写回 INI。

因此，即使初始 INI 使用 HTTPS，返回目录也可把搜索 URL 更新成 HTTP。Transport 已禁止“HTTPS 302 跳到 HTTP”，但不覆盖“HTTPS 目录正文里返回 HTTP 地址”。这是静态调用链确认，不表述为公网服务已发生的事件。

原版宿主无法调用当前扩展 Configure 来避开刷新。建议保存用户对某槽的 HTTPS 要求，阻止目录无提示降低该槽的传输等级；同时保留用户明确配置普通 HTTP 服务的能力。

### 6.4 依赖与错误可见性不足

- 歌词独立 ZIP 只有 `AddIn/ttp_lrcsh.dll` 和 SHA256SUMS，只覆盖这个包不能为 XP 配齐现代 HTTPS。
- 重建 Action 已分别下载 HTTPS 和歌词插件最新稳定包；HTTPS 下载脚本检查摘要、PE 和 XP／Win7 导入，但没有校验歌词所需 ABI 5 能力。两个独立的“最新版本”不能替代接口约束。
- helper 详细错误进入 `Failure(32004, error)` 后，`src/search.cpp:92` 通常只给宿主通用资源文字；证书、代理、协议错误难以区分。
- 可通过原版已有 OnError 回调改善文字，不需要修改宿主界面。发行校验可增加与摘要绑定的 ABI 能力清单，仍不在 Action 放入或执行私有测试。

## 7. 其它仍存的原版差异

### 7.1 目录边界新增实测

三组原 DLL／重建 DLL 成对测试均正常退出，确认以下差异：

| 场景 | 原 DLL | 当前重建 DLL | 判断 |
|---|---|---|---|
| 第一 server 有效，第二 URL 为相对地址 | 两项覆盖／保存，第一项可搜索 | 整次更新拒绝，保留旧配置 | 严格验证是改进，但有效第一项也被丢弃；应明确逐槽策略 |
| 第一 server 名称为空，URL 有效 | 接受，搜索时服务器名为空 | 整次更新拒绝 | 有效性策略差异，不建议无条件复制空名称 |
| 初始 INI 含两个 extra | GetExtra 取第一个 | GetExtra 取最后一个 | 小范围兼容缺口；构造器与 Refresh 自身也不一致 |
| 刷新响应含两个 extra | 会话用第一个；保存仅移除第一个，第二个留在磁盘 | 会话用第一个；保存移除全部 | 清除全部可作为有意策略，需说明 |

修复“取首个 extra”的范围很小；不要为复制无效 URL 行为而取消校验。这些额外边界不表示此前单项更新／extra-only 修复失效。

### 7.2 Cookie、字符与存储边界

- Cookie 只保存在 Search 内，按 scheme + host + port 和路径限制。Domain 通过检查也不扩展到跨子域；HTTP 与 HTTPS 间同名主机也不共享。与原 WinINet 存储不同，应按实际服务需要限定调整。
- helper 要求主机名为 ASCII／已转 IDNA 的形式，没有自动转换原始 Unicode 域名；URL 路径可 UTF-8 转义。
- 原 WinINet 为 ANSI API，重建使用 Unicode 和自有 UTF-8 校验；畸形编码、DTD、超深 XML、超大结果不会完全复刻旧解析器。
- 目录保存仍用 GetTempFileNameW，超长路径有 MAX_PATH 限制。XML 保留语义不承诺旧库的逐字节排版。

### 7.3 生命周期、代理与性能

- 最终 Release 已能处理原版同步 UI 消息，但仍先无限等待在途回调排空。宿主回调永久阻塞时，不能声称“最多 1 秒关闭”；1 秒只限制其后网络线程等待。
- 65 秒是跨跳转检查期限，不能强制中断每个系统调用。同步 DNS、PAC 解析等仍可能延迟取消。
- 原生直连／显式代理、helper 的 CONNECT／SOCKS／系统代理不能互相替代验证；尚未完成全部认证方式和长期断网矩阵。
- HTTPS 每次请求 LoadLibrary／FreeLibrary；无其它模块持有 helper 时会重复模块初始化，且没有 TLS 连接复用。先保证正确，再评估按 Search／模块持有 helper 的收益；继续保证响应释放先于 DLL 卸载。

### 7.4 应保留的有意差异

不建议恢复 TerminateThread、不发送旧 ci 机器标识、刷新失败保留可用 INI、取消后丢弃旧结果、拒绝内嵌 NUL／畸形编码、资源上限、证书验证和 HTTPS 禁止降级等现有改进。

## 8. 建议实施顺序

1. **修复 HTTP 状态与正文语义。** 优先 204／304；为歌词明确跳转／错误正文策略，保留已取得状态和 Location，不能通过取消所有大小限制解决。
2. **增加向后兼容的 helper 扩展。** 建议 ABI 6 增加 Content-Range、允许的 UA／Accept／Referer 和所需响应策略；保持 ABI 1～5 的结构尺寸和契约。统一歌词 HTTP／HTTPS 完整性检查。
3. **补齐旧宿主 HTTPS 配置和诊断。** 目录不悄悄覆盖用户 HTTPS 要求；小范围修正首个 extra；明确 helper 能力不足和证书错误。必要时提供按服务配置的私有 CA，不关闭验证。
4. **明确分发依赖。** 原版安装说明同时列出两个 DLL；发行元数据声明最低 helper ABI。Action 继续校验下载与导入，私有测试留在本地。
5. **整链路验证后优化。** XP／Win7／本机 Windows 11 上，用原版 EXE + 原版 ttpcomm + 重建歌词 DLL + helper 验证 HTTPS 目录、搜索、连续下载、Cookie、跳转、取消、关闭、保存显示；覆盖证书拒绝、206 部分正文及无正文状态。Windows 11 代测 Windows 10，不写成原生 Win10 测试。

最终判断：旧 ABI 不构成 HTTPS 障碍，重建已实现传输接入。下一阶段主要工作是统一响应语义、保护 HTTPS 配置、配齐依赖并补上原版宿主的加密整链路证据。
