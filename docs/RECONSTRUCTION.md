# ttp_lrcsh 重建与宿主迁移

2026-10-09。

本文保留初期重建的接口分析。后续 HTTPS ABI 6 修复见 [p2 记录](HTTPS_ORIGINAL_PLAYER_FIXES_20261009.md)，XML 优先与 INI 迁移的当前规则见 [p3 记录](SERVER_CATALOG_XML_20261009.md)。

`2026.10.09p4` 已在 XML 模式按实际条目枚举最多 128 个工厂；下文的两项描述是原版及旧 INI 路径的行为。实现与原宿主实测见 [多服务器说明](MULTI_SERVER_ORIGINAL_HOST_20261009.md)。

## 原版证据

原文件 `AddIn/ttp_lrcsh.dll` 为 38,400 字节，SHA-256：

`e2b6e0aacb9ef39dca1bb80865e1fe83a4c993a071c5887a4fc32251c8f8021d`

私有证据：`rebuild/tests/lyrics/ncab/pseudo/ttp_lrcsh.dll.pseudo.c`（240 个函数）、`rebuild/tests/ttpcomm_analysis_20261007/lrcsh-disassembly.txt`。伪代码未覆盖的短包装函数使用反汇编补齐。原播放器在 `004C88D9` 解析导出，按 GUID 枚举工厂，未发现文件大小或摘要校验。宿主枚举到失败为止，“两个服务器”来自插件自身的限制。

## 二进制接口

| 层 | 原版地址/契约 | 重建 |
|---|---|---|
| 入口 | `60354C50`，stdcall，ordinal 1 `ttpGetSoundAddIn` | 相同导出、调用约定和输出对象 |
| AddIn | `EEC6C534-FEBA-421E-AA5D-10AC66F98784`，slot 3 枚举 | index 0/1；越界 E_INVALIDARG |
| Creator | `9D5AE963-7DF6-4323-B4CF-FBDA159BFF15` | slot 3 创建、slot 4 名称；CoTaskMem 分配 |
| Search | `BF17B3E8-7E33-45E7-9342-CDD34BA55AF9` | slots 3/4/5/6 初始化、搜索、下载、附加链接 |
| 初始化 | `603520CD` | 先查询 Host，再保留 Callback；网络结构维持 24 字节 |
| 回调 | `CA540428-2528-4BBC-87B0-6605F21810C9` | slots 3..6 结果、下载、错误、服务器名称 |
| 附加链接 | `60352387` / `603523F9` | 标题或 URL 为空返回 E_FAIL；输出缓冲区由宿主 CoTaskMemFree |
| 可选控制 | 新 GUID `B25E8A91-0A73-46BB-9C25-A8E73C519D64` | 初始化前 Configure，终止式 Cancel；不扩展旧 vtable |

回调数据只在回调期间有效；引用计数及 QueryInterface 的 IUnknown 身份保持一致。对象内部布局无需复刻；宿主只使用这些接口。

## 查询、下载和 NCAB

1. 保留原版数字前缀、十组括号内容、标点清理；`LCMapStringW` 按线程区域做简体和小写映射。ASCII 单引号先替换为 U+2019 和空格。
2. 字符串转 UTF-16LE 字节，再生成大写十六进制 `?sh?Artist=...&Title=...&Flags=0&`。仅主机名的地址补 `/`。
3. 结果按服务器顺序保留，允许重复及负数 ID。ID 按原版 32 位 `atoi` 环绕转换。旧 XML 只解码五种命名实体，数字实体保持字面形式；重复属性取首次值，兼容 NCAB 未转义的 `R&B`。下载签名使用这一行经旧解析规则处理的 UTF-8 artist+title，不能用清洗后的查询或按 ID 重新定位。
4. Code 严格使用带符号字节与 32 位环绕计算；下载索引是行号，URL 为 `?dl?Id=...&Code=...&`。
5. 搜索和下载共用每个 Search 独立的 Cookie 容器。保留重定向响应的 Set-Cookie，处理路径、过期、Secure；不向另一个 origin 发送该会话 Cookie。
6. 下载错误保留候选列表，支持换行重试。正文仅按原版做 `LF CR -> CR LF`、`CR LF CR -> CR LF`，不改写所有换行。
7. `tt-title`、`tt-url` 按 UTF-16LE hex 解码，字面 `\n` 转 CRLF。正文以 `<result ` 开始时走错误响应分支。

## 服务器配置与差异

默认资源保留原版两个地址与中英文字符串。DLL 同名 INI 为 UTF-8 `ttp_lrcsvr` XML。传统接口每个模块生命周期首次搜索执行一次 `svrlst`，只更新返回的服务器槽位，单项刷新保留另一槽；仅 `extra` 的响应也能更新会话链接。保存保留第三个服务器、未知属性及扩展节点，只移除根 `query_url` 和 `extra`。使用同目录临时文件、刷新与原子替换，保存失败输出调试诊断；`extra` 不写回缓存。

有意修正：

- 刷新失败或返回空内容保留可用 INI；不复现原版删除配置的行为。
- 明确配置的现代服务器跳过远端列表刷新，避免覆盖用户选择；GetExtra 返回 E_FAIL 并清空输出，不暴露其它服务器的旧链接。
- 不调用原版通过 ttpcomm ordinal 302 获取机器标识并拼接 `ci` 的分支；默认不发送设备标识。若某旧服务器强制依赖该字段，属于未兼容的服务端策略。
- 网络响应限制 2 MiB、最多 10,000 行；拒绝畸形 UTF-8、DTD、过深 XML、HTTPS 降级跳转。不会逐一复制原版宽松解析的未定义行为。
- 取消关闭当前请求并通知工作线程退出，不调用 TerminateThread。工作线程持有模块引用，清理 C++ 对象后才 FreeLibraryAndExitThread；回调内释放最后对象不会等待自己。Cancel 不等待回调，取消前已获准进入的回调可以完成；正常最终 Release 先排空在途回调，期间仅处理同步发送消息，然后最多等待线程清理 1 秒。若宿主回调自身永久阻塞，排空仍可能等待，不承诺无条件有界退出。
- 忙碌时允许替换命令；操作代数阻止旧结果／错误覆盖新请求。空 artist 视为空字符串，空 title 指针仍返回 E_POINTER。终止式 Cancel 后对象不能重用。
- Initialize 等待网络工作线程准备就绪，最多 5 秒；接口查询、创建线程或会话初始化失败时回滚，允许再次初始化。外部 QueryInterface/Release 不在状态锁内执行。
- 每次操作共用 65 秒检查期限，跳转不重置计时；阻塞网络调用仍受各自超时约束，不能承诺严格在第 65 秒返回。保留每个 Search 的 WinINet 会话及原版 Referer/Accept；接受有效 2xx，空响应保留实际状态的错误映射，HTTP 206 存在 Content-Range 时验证完整正文。
- XP 的 InternetCrackUrlW 对缺失字段可能保留 DWORD(-1) 长度；使用返回指针和长度共同判断字段存在，避免错误拒绝普通 URL。

## HTTPS 会话扩展

`mbedtlsmin` 的 `ttp_https` 增加 ABI 5 单次请求接口：输入 Cookie，输出 HTTP 状态、Location 和全部 Set-Cookie，不自动跳转。ABI 1..4 保持不变。重定向及 Cookie 策略属于歌词 DLL；TLS、证书和代理仍由 HTTPS 组件负责。缺少 ABI 5 时回退 WinINet，不在证书校验失败后降级。

XP 使用现代 HTTPS 建议同步安装新版 `ttp_https.dll`。系统 WinINet 回退的 TLS 能力随系统而异，不能据此承诺 XP 支持所有现代服务。

## 重建版迁移与 Action

生产 `ttplayer_core` 移除 `lyric_http.cpp`、`legacy_lyric_http.cpp` 和 `https_provider.cpp`。旧实现移到私有测试目录作为对照，不参与播放器或 Action 构建。`OnlineSearch` 只负责宿主状态、插件调用和取消；具体请求、解析、Code、Cookie、下载均在此 DLL。

已知 URL 的服务先通过可选接口配置，然后沿用旧搜索回调。缺失新组件时明确报错，不回退宿主网络实现。其它既有私有协议 AddIn 仍可走原来的插件工厂接口。

播放器 Action 从 `https://github.com/TTPlayerRebuild/TTPlayerLrcsh/releases/latest` 获取稳定日期版本，核对 ZIP 的 GitHub SHA-256、内部 DLL 摘要、x86/XP/Win7 导入及唯一导出；打包必含 `AddIn/ttp_lrcsh.dll`。版本、来源和摘要记录在 build-info 中。缺失发行版或校验失败时构建失败，不静默打包旧 DLL。

本地构建使用 `TTPLAYER_LRCSH_DLL` 指定独立构建结果，默认 `../ttp_lrcsh/build/Release/ttp_lrcsh.dll`。
