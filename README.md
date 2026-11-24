# Quire

Quire 是跨平台的本地阅读器，使用 [Asuka](https://asuka.egoist.dev) 编写：同一套源码构建 iOS、macOS、Linux、Android 与 Web 应用，界面由各平台原生控件实现。书籍解析、排版与光栅化由
[ebook-engine](https://github.com/Laic7092/ebook-engine)（Rust）通过 C ABI 提供。

## 功能

- 书架：网格或列表两种摆法，按最近阅读、书名、作者或手动排序；阅读进度、长按删除；内置公版样书。
- 阅读器：分页与连续滚动两种模式。
- 目录与书签：读取 EPUB 目录，书签按章节位置保存。
- 选中与笔记：长按页面上的一段原文，画出选区并可拖动；复制、加高亮，或写一条笔记。笔记和引文一起保存，在目录页按书面列出，点一条回到它所在的位置。
- 阅读统计：总时长、阅读天数、连续天数、最近七天，以及每本书自己的记录（累计时长、第一次/最近一次阅读、每天的时长）。
- 排版：字号、行距、页边距、字距、词距，纸色与夜间主题。
- 导入：文档选择器（可一次选多本），以及 Files / Mail 中的“用 Quire 打开”。
- 格式：`.txt`、`.epub`。
- 同步：书架、进度、书签、笔记和排版设置随同步走；云端正文按内容 hash 存放，打开云端的书时才下载那一本。云端默认是坚果云，也可选其他 WebDAV 服务器。入口在书架右上角的云图标与「我 → 同步与备份」，可开启启动时自动同步、自动备份书籍，或手动「备份全部」；本地缓存可设上限并按最久未读清理。
  云端布局：`catalog.json` 放书架、进度和设置，`meta/<key>.json` 放每本书的书签、笔记与阅读统计，`blobs/<hash>` 放正文；只有打开某本书时才下载它的正文。
- 导航：首页、书架、搜索三个标签，书架为默认页；首页与搜索为预留入口。

## 平台

Quire 使用 Asuka 的跨平台运行时，同一套源码可构建 iOS、macOS、Linux、Android 与 Web。iOS 是当前 CI 持续产出的目标；其他平台使用同一套 Asuka 源码和对应的 ebook-engine 目标库构建。

WebDAV 同步走 `std/http`：iOS / macOS 的 URLSession、Linux 的 libsoup 与 Windows 的 WinHTTP 都能发 `PROPFIND` / `MKCOL`。Android 的 `HttpURLConnection` 只允许标准方法，这一版在 Android 上暂时用不了同步；网页端还受浏览器 CORS 的限制。

同步密码优先存系统钥匙串（`platform/secure`）。没有钥匙串的桌面（例如没装或没运行 Secret Service 的 Linux）会退回到应用目录里一个仅本人可读（`0600`）的 `sync-password` 文件，并在同步页提示；这样每次启动不用重输。

## 架构

```text
Asuka 应用
  ├─ Tabs：首页 / 书架 / 搜索
  ├─ Shelf → Reader（根层覆盖页）
  ├─ Library store（书架、进度、书签、排版设置）
  │      │ WebDAV：PROPFIND / MKCOL / GET / PUT / DELETE
  │      ▼
  │   坚果云（或别的 WebDAV 服务器）
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
- 滚动模式由引擎维护整本书的连续排版会话（章节起点、高度、跨章拼接），阅读器只按全书流坐标请求可见的位图条带；条带的光栅化在后台执行，且只画条带内的内容。
- 位图格式为 `EQP1`：12 字节头（宽、高）加 RGBA8 像素。像素不经过 Asuka 运行时。
- `BookPage` 负责各平台的位图显示与点按事件。

## 目录

| 路径 | 内容 |
| --- | --- |
| `app.asuka` | 根组件：三个标签页与阅读页覆盖层 |
| `icons.asuka` | 图标名：iOS/macOS 用 SF Symbols，其它平台用 Material Symbols |
| `shelf.asuka` | 书架、封面、导入、删除、显示方式与排序 |
| `reader.asuka` | 阅读器、翻页/滚动、目录、书签、选中与笔记、排版设置 |
| `library.asuka` | 书架与阅读状态的持久化 store，以及按需下载、备份和缓存清理 |
| `home.asuka` / `search.asuka` | 首页与搜索入口 |
| `stats.asuka` | 阅读统计页：总体数据、最近七天、每本书的记录 |
| `sync.asuka` | 同步的文件格式与合并规则：catalog、每本书的 sidecar、删除记录 |
| `webdav.asuka` | WebDAV 客户端：认证、列目录、上传/下载、删除 |
| `sync_sheet.asuka` | 「同步」页：状态与动作；云端服务、账号等连接细节收在二级页 |
| `sync_test.asuka` | 列目录解析、URL、合并与 catalog/sidecar 的测试 |
| `engine/engine.asuka` | ebook-engine 的 C ABI 绑定 |
| `bookpage/` | 原生 `BookPage` 组件（iOS、Linux、Android、Web） |
| `lib/` | 数据模型、纯文本解析、分页辅助、选词与笔记、阅读统计与对应测试 |
| `samples/` | 编译时嵌入的公版样书 |
| `fonts/` | 字体的许可证；字体本身由脚本下载，不入库 |

## 环境

- Asuka CLI
- ebook-engine 预编译静态库：`lib/<target>/libebook_abi.a`
- Linux：GTK 4
- macOS / iOS：Xcode 26 或更新版本、iOS SDK
- Android / Web：使用 Asuka 对应平台的工具链

使用预编译库时不需要 Rust 工具链。

## 开发

```sh
# 首次：下载内嵌字体（8 MB 的 WOFF，不入库）
.github/scripts/fetch-font.sh

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

测试覆盖书架、导入、阅读位置、书签、选词与笔记的保存/编辑/删除、目录跳转、分页/滚动切换、主题与排版参数、阅读时长统计、云端书标记与按需下载的失败路径、缓存清理、纯文本章节解析，以及 WebDAV 列目录解析、同步合并与 catalog/sidecar 读回。

## 字体

内嵌字体为 Noto Serif SC Regular，采用 SIL Open Font License 1.1，许可证见 `fonts/OFL.txt`。字体以 WOFF 形式提供，由 ebook-engine 直接读取。

WOFF 本身不入库：它由 `.github/scripts/fetch-font.sh` 从固定版本的 CDN 直接下载，并校验哈希，细节见 `fonts/README.md`。
