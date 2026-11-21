# Quire on Asuka

把 [Quire](../quire) 的阅读器用 Asuka 重写的一份实验分支：视图用各平台原生控件，
目标是 **iPhone / iPad 上的 .ipa**。

排版与分页不重写，交给 [ebook-engine](../ebook-engine)（Rust）：它解析 EPUB 与纯文本、
按章分页、把一页光栅化成位图。Asuka 这边只调它的 C ABI（`eb_plain_*`），再把位图交给
一个原生视图显示 —— 排版是同一份代码，桌面、iOS 与 Web 出的是同一套断行。

## 现在有什么

| 文件 | 内容 |
| --- | --- |
| `engine/engine.asuka` | 引擎的 Asuka 绑定：`extern fn eb_plain_*`，书是一个整数句柄（store/state 里都能放） |
| `bookpage/` | 原生组件 `BookPage(path:)`：iOS 上 `CGImage`（文件 mmap，不拷贝）+ `UIImageView`，Linux/GTK 上 `GdkMemoryTexture` + `GtkPicture`；点按发 `press(x, y)` |
| `lib/text.asuka` | 纯文本开书（书架用的元数据：章标题、段数、字数）：第…章 / 卷… / Chapter N / 序 / 楔子 / 番外、空行分段 vs 一行一段、硬换行合并、长段落在句子处切分 —— 移植 Quire 的 `internal/book/txt.go` |
| `lib/text_test.asuka` | 11 个解析测试 |
| `lib/models.asuka` | `Chapter` / `Book` / `Reading` / 纸墨 `Palette` / 正文字体（iOS/macOS 用 Songti SC） |
| `library.asuka` | 书单、阅读位置、字号、纸色；`std/store` 持久化（iOS = UserDefaults），`importFile` 收 .txt 和 .epub（存进 app 目录） |
| `shelf.asuka` | 书架：网格封面、按窗口宽度决定列数、导入按钮、`onOpenFiles`（Files/邮件里「用 Quire 打开」） |
| `reader.asuka` | 阅读页：引擎排出的**逐页**位图、点按左右三分之一翻页、目录 sheet（引擎的 TOC）、字号/纸色 sheet（改了就重排）、位置写回 store |
| `samples/` | 两本公有领域样书（朱自清散文选、鲁迅野草），编译时由 `std/embed` 嵌进去 |

9 个测试全绿（`asuka test`）。原生预览用 asuka 自己的截图，不经过浏览器：

```sh
# 桌面窗口，png 出来就退出；ASUKA_SCRIPT 可以 tap:/type: 先走一遍
ASUKA_SNAPSHOT=shot.png ASUKA_SCRIPT="tap:散文選;tap:目录" \
  xvfb-run -a dbus-run-session asuka dev --platform linux
```

## 一页是怎么画的

```
ebook-engine                          Asuka                   平台
─────────────                         ─────                   ────
eb_plain_open(书的字节)      ──>  句柄（int，进 state）
eb_plain_layout(章, 视口, 字号) ──>  页数
eb_plain_page_raw(页, DPR, 墨/纸, 路径)
                              ──>  EQP1 文件 ──>  BookPage(path) ──> CGImage / GdkMemoryTexture
eb_plain_hit(页, x, y)        <──  press(x, y)（原生手势）
```

- 位图格式是 `EQP1` + u32 宽 + u32 高 + RGBA8（直通 alpha），由引擎按屏幕 DPR 直接写文件；
  像素不经过 Asuka 运行时，也没有 PNG 编解码。
- `BookPage` 的原生代码读文件（iOS 是 `mmap` + `CGDataProvider`，不拷），点按再回给 Asuka。
- 一页的排版 + 光栅在 20–40ms 量级，翻页在 `task` 里同步做；改字号、改纸色、转屏都重排。

已知的 asuka 0.2.12 限制（值得回报上游）：

- **`Canvas` 不能画位图**，所以位图只能走 `Image(路径)` 或原生组件；这里选了原生组件。
- **透明 `View` 上的 `on press` 在 GTK 上收不到点击**（Stack 覆盖层），点按因此交给原生视图的手势。
- 底部工具栏按钮的 `disabled` 在自己 handler 里翻转会让 GTK 堆损坏，所以按钮不设 `disabled`。

## 跑

```sh
asuka dev                  # 本机窗口（Linux/GTK 4）
asuka dev --platform web   # 浏览器，需要 Zig
asuka test                 # 不接设备，按手机尺寸排版
asuka fmt -w               # 格式化
```

## GitHub CI：未签名 .ipa

`.github/workflows/ios.yml` + `.github/scripts/build-unsigned-ipa.sh` 在
`macos-latest` 上出一个**未签名 device .ipa**，不需要 Apple 开发者账号：

1. 造一个一次性自签证书（Code Signing EKU），导进本次运行专用的 keychain 并信任；
2. 写一个 stub `.mobileprovision`（app-store 类型、`application-identifier =
   假team.<id>`、`DeveloperCertificates` 就是上面那张证书），asuka 会读它、按它签名；
3. `asuka build --platform ios --profile <stub>` 正常出 `.ipa`；
4. 解包，删掉 `_CodeSignature`、`embedded.mobileprovision`，`codesign --remove-signature`
   掉可执行文件和各个 framework，再打包成 `dist/Quire-unsigned.ipa`。

产物用 Sideloadly / AltStore（免费 Apple ID 重签，7 天一签）装，越狱设备或
TrollStore（≤ iOS 17.0）可以直接装。跑法：push 到 `main`，或 Actions 页面手动 Run。

注意：**runner 上要有 Xcode 26**（asuka 的编译要求）。workflow 会先挑
`/Applications/Xcode_26*.app`，没有就直接报错并提示换镜像标签（比如 `runs-on: macos-26`）。
如果这个 Asuka 工程是放进 quire 仓库的子目录，把 workflow 的 `working-directory`/artifacts
路径按子目录改一下即可（脚本自己认 `asuka.toml` 的位置）。

## iOS / .ipa

**这台机器（WSL2 / Linux）打不了 .ipa**：iOS 构建要 Apple 芯片的 Mac + Xcode 26 +
iOS SDK，`asuka build --platform ios` 在这里的报错就是

```
asuka: no iOS SDK (install Xcode): exec: "xcrun": executable file not found in $PATH
```

工程这一侧已经按 iOS 校好了，换到 Mac 上只剩签名和构建：

- `asuka.toml`：`icon.png`（1024×1024、不带透明通道，由 Quire 的 `resources/icon.py`
  出图后压到墨色底上）、`accent`、`orientations`、启动屏 `[splash]`、
  `[[fileTypes]]`（`.txt`，iOS 上是 Files/邮件里的「用 Quire 打开」）、
  `[ios] iphoneOnly = false`、`nonExemptEncryption = "no"`
- 隐私清单由 Asuka 按二进制实际用到的 API 生成，工程里不用手写
- 视角：书架 `largeTitle`、导航栈推入/边缘返回、sheet、底部工具栏都是 UIKit 原生

在 Mac 上：

```sh
# 1) 一次性：developer.apple.com 建 App ID（dev.laix.quire），Xcode › Settings ›
#    Accounts 登 Apple ID 并下载证书；装好 Xcode 26 与 iOS 模拟器
# 2) 填 team（asuka.toml 的 [ios] team = "ABCDE12345"），或让脚本自己找匹配的 profile
# 3) 模拟器上先看：无签名 .app
asuka dev --platform ios            # 模拟器；--device iphone 走真机（需要开发者模式）
asuka build --platform ios --simulator

# 4) 出 .ipa（App Store 签名，用 ios.profile 或 keychain 里最匹配的 profile）
asuka build --platform ios          # → dist/Quire.ipa

# 5) 直接上 TestFlight（可选，需要 App Store Connect API key）
asuka publish --platform ios --groups "Friends, QA" --notes "第一个能开书的构建"
```

`--distribution ad-hoc|development` 可以签名给指定设备，不经过 App Store。

## 和 Quire 的差距（下一步）

- **EPUB 没搬**：`std/` 里没有 zip+crypto 的成品，Quire 的 `layx/epub` 是 Go。
  要么在 Asuka 里写一个 zip 读取 + OPF/nav 解析，要么继续只支持 .txt。
- **没有 layx 那样精确的分页**：现在是一章一滚动屏；跨平台断行各排各的。
  要做翻页模式，需要按平台文本度量算页面（Asuka 没暴露 glyph 级别的度量）。
- **阅读进度只到章**：`Reading.percent` 按章算；章内滚动位置还没接。
- **EPUB 的封面、目录层级、书源、搜索、书签、笔记**：都在 Quire 里，还没搬。

## 设计上与 Quire 的一致处

纸墨配色（`Palette`）沿用 Quire 的三套；封面颜色取自 Quire 图标的墨/印/布；
章节与段落的切分规则逐条对齐 `internal/book/txt.go`，并有测试对照。
