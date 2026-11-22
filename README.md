# Quire

Quire 是跨平台的本地阅读器，使用 [Asuka](https://asuka.egoist.dev) 编写：同一套源码构建 iOS、macOS、Linux、Android 与 Web 应用，界面由各平台原生控件实现。书籍解析、排版与光栅化由
[ebook-engine](https://github.com/Laic7092/ebook-engine)（Rust）通过 C ABI 提供。

## 功能

- 书架：网格封面、阅读进度、长按删除；内置公版样书。
- 阅读器：分页与连续滚动两种模式。
- 目录与书签：读取 EPUB 目录，书签按章节位置保存。
- 排版：字号、行距、页边距、字距、词距，纸色与夜间主题。
- 导入：文档选择器，以及 Files / Mail 中的“用 Quire 打开”。
- 格式：`.txt`、`.epub`。
- 导航：首页、书架、搜索三个标签，书架为默认页；首页与搜索为预留入口。

## 平台

Quire 使用 Asuka 的跨平台运行时，同一套源码可构建 iOS、macOS、Linux、Android 与 Web。iOS 是当前 CI 持续产出的目标；其他平台使用同一套 Asuka 源码和对应的 ebook-engine 目标库构建。

## 架构

```text
Asuka 应用
  ├─ Tabs：首页 / 书架 / 搜索
  ├─ Shelf → Reader（根层覆盖页）
  └─ Library store（书架、进度、书签、排版设置）
          │
          │ C ABI：eb_plain_*
          ▼
ebook-engine
  ├─ 解析 TXT / EPUB
  ├─ 按章排版
  └─ 光栅化为 EQP1 位图
          │
          ▼
BookPage（原生位图视图）
```

- 阅读页是应用的根层覆盖视图，不是系统 sheet；纸张覆盖状态栏区域。
- 分页模式按章排版，逐页光栅化。
- 滚动模式按章生成连续排版，以小块位图渲染可见窗口；滚动窗口的排版与光栅化在后台执行。
- 位图格式为 `EQP1`：12 字节头（宽、高）加 RGBA8 像素。像素不经过 Asuka 运行时。
- `BookPage` 负责各平台的位图显示与点按事件。

## 目录

| 路径 | 内容 |
| --- | --- |
| `app.asuka` | 根组件：三个标签页与阅读页覆盖层 |
| `shelf.asuka` | 书架、封面、导入、删除 |
| `reader.asuka` | 阅读器、翻页/滚动、目录、书签、排版设置 |
| `library.asuka` | 书架与阅读状态的持久化 store |
| `home.asuka` / `search.asuka` | 首页与搜索入口 |
| `engine/engine.asuka` | ebook-engine 的 C ABI 绑定 |
| `bookpage/` | 原生 `BookPage` 组件（iOS、Linux、Android、Web） |
| `lib/` | 数据模型、纯文本解析、分页辅助与对应测试 |
| `samples/` | 编译时嵌入的公版样书 |
| `fonts/` | 内嵌的 Noto Serif SC 字体及许可证 |

## 环境

- Asuka CLI
- ebook-engine 预编译静态库：`lib/<target>/libebook_abi.a`
- Linux：GTK 4
- macOS / iOS：Xcode 26 或更新版本、iOS SDK
- Android / Web：使用 Asuka 对应平台的工具链

使用预编译库时不需要 Rust 工具链。

## 开发

```sh
asuka check
asuka test
asuka test lib
asuka fmt -w
asuka dev
```

`asuka test` 运行应用测试，`asuka test lib` 运行 `lib/` 包的解析与分页测试。

## 引擎

预编译库来自 ebook-engine 的 GitHub Releases：

```sh
# 默认使用最新非预发布版本
.github/scripts/fetch-engine.sh linux-x64

# 固定版本
ENGINE_VERSION=v0.2.4 .github/scripts/fetch-engine.sh linux-x64
```

下载会校验 Release 中的 `SHA256SUMS`。需要从源码构建时：

```sh
.github/scripts/build-engine.sh linux-x64 ../ebook-engine
```

CI 通过 `ENGINE_VERSION` 选择引擎版本，默认值为 `latest`。

## iOS 构建

```sh
asuka dev --platform ios
asuka build --platform ios --simulator
asuka build --platform ios
```

签名配置位于 `asuka.toml` 的 `[ios]` 节：

```toml
[ios]
team = "ABCDE12345"
profile = "Quire Distribution"
```

未配置时，Asuka 会在 keychain 中查找匹配的 profile。

## CI

`.github/workflows/ios.yml` 包含两个并行作业：

| 作业 | Runner | 内容 |
| --- | --- | --- |
| Tests | `ubuntu-latest` | 获取 `linux-x64` 引擎，运行 `asuka check`、`asuka test`、`asuka test lib` |
| Unsigned IPA | `macos-latest` | 获取 `ios-arm64` 引擎，使用 Xcode 26+ 构建未签名设备 IPA |

未签名产物位于 `dist/Quire-unsigned.ipa`，作为 `ios-unsigned-ipa` artifact 上传。构建流程使用临时自签证书与 stub profile 完成 Asuka 的签名步骤，随后移除签名。

安装方式：

- Sideloadly 或 AltStore（免费 Apple ID 重签）
- TrollStore（iOS 17.0 及更早版本）
- 越狱设备

## 测试

```sh
asuka test
asuka test lib
```

测试覆盖书架、导入、阅读位置、书签、目录跳转、分页/滚动切换、主题与排版参数、纯文本章节解析。

## 字体

内嵌字体为 Noto Serif SC Regular，采用 SIL Open Font License 1.1，许可证见 `fonts/OFL.txt`。字体以 WOFF2 形式提供，由 ebook-engine 直接读取。
