# 验证记录

2026-10-09。测试及原始日志仅在 `rebuild/tests/lrcsh_rebuild`；Actions 不运行这些测试。

## 已完成

- 独立 x86 Release 编译；静态导入通过 XP 5.1.2600、Win7 6.1.7600 清单检查。仅导出 `ttpGetSoundAddIn` ordinal 1。
- 本机 Windows 11：原 DLL / 重建 DLL 22 个接口和协议场景。覆盖工厂、查询文字、负数/重复 ID、真实 NCAB 抓包、裸 &、错误、本地重定向、Cookie、无 HTTP 头、失败重试、响应大小限制、无效 UTF-8、取消及回调内最后释放。有效场景逐项比较请求和回调。
- NCAB 实服 `http://localhost:99`：萧煌奇《无人熟识的彼个人》《心里有针》均成功搜索并下载指定网易结果。第一首与原 DLL 内容完全一致；第二首本次原 DLL 获取的正文多一行“歌词解析错误代码：”，不能将两次动态响应声明为摘要一致。同一捕获响应的原/新 DLL 对照已通过。
- TLS ABI 5：TLS 1.2/1.3，各验证带两个 Set-Cookie 的 302、手动 Cookie 后续请求、取消、重复释放；两种协议均拒绝不受信任证书。测试 CA 仅作为请求参数传入，未安装到系统证书库。ABI 1..5 查询仍可用。
- 重建宿主：`lyric_search_tests` 通过实际新 DLL、选项与搜索 UI 208/209、自动匹配、关联保存、错误、取消、旧曲目回调、关闭不阻塞等；服务器编辑器测试通过。
- Win7 虚拟机：实际执行 x86 测试宿主，新 DLL 加载、接口、搜索、独立 Cookie 会话及三次下载通过。
- 主程序 Release 编译和 XP/Win7 导入审计通过。两个 Action 通过 actionlint 静态检查。
- 8 项发行下载/解包校验通过（本地合成发行元数据，不代表远端 Action 已运行），拒绝错误摘要、截断 ZIP、错误来源、路径穿越及内部摘要不符。主程序整包实际生成，六个文件与全部 SHA-256 校验通过。

最终插件版本 `2026.10.09`，123,904 字节。SHA-256：

`3bf87f427327af988aa1101491e3cde4e1e69251c9f90f408b52f63500ec3af6`

发行 ZIP：`build/Release/ttp_lrcsh-2026.10.09.zip`。播放器运行目录 `rebuild/build/Release/AddIn` 已放入新歌词 DLL 和支持 ABI 5 的 HTTPS DLL。

## 尚未证明

- XP：虚拟机正在运行，但 Guest Control 执行超时；没有获得运行结果。导入兼容通过不等于 XP 实机行为已通过。
- 原版完整 GUI：已调用原版 DLL 作协议基准，并用原版 ABI 的测试宿主加载新 DLL；尚未完成原版播放器完整界面中的交互回归。不能写成原版 GUI 全部验证通过。
- Windows 10：沿用用户安排，由本机 Windows 11 代验；没有独立 Win10 环境。
- 发布：查询 TTPlayerLrcsh `releases/latest` 返回 404。已准备构建与打包流程，未发布、未上传测试、未代替用户运行远端 Action。需要先发布 TTPlayerLrcsh；现代 HTTPS 会话同时建议发布本次 ABI 5 的 TTPlayerHttps。

原服务端可用性、所有代理/PAC 策略、所有语言环境以及恶意回调不是已完成的全组合验证范围。
