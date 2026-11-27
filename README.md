# Quire

Quire 是跨平台的本地阅读器，使用 [Asuka](https://asuka.egoist.dev) 编写：同一套源码构建 iOS、macOS、Linux、Android 与 Web 应用，界面由各平台原生控件实现。书籍解析、排版与光栅化由
[ebook-engine](https://github.com/Laic7092/ebook-engine)（Rust）通过 C ABI 提供；在线书源的规则与 JavaScript 由
[legado-engine](https://github.com/Laic7092/legado-engine)（Rust + QuickJS）提供。

## 功能

- 书架：网格或列表两种摆法，按最近阅读、书名、作者或手动排序；阅读进度、长按删除；内置公版样书。
- 阅读器：分页与连续滚动两种模式。
- 目录与书签：读取 EPUB 目录；书签存的是引擎的稳定位置（段与段内字节，`spine:node:offset`），换字号、换行距、换屏幕之后还指着同一段原文，而不是同一个页码。目录页还会列出本章的图片，点一张按原分辨率查看。
- 选中与笔记：长按页面上的一段原文，画出选区并可拖动；复制、加高亮，或写一条笔记。笔记和引文一起保存，两端也存成稳定位置，重排后高亮仍落在原处；在目录页按书面列出，点一条回到它所在的位置。
- 书内搜索：阅读页右上角的放大镜搜整本书的正文，引擎按章建纯文本索引（同章再搜只查缓存）。命中带章名和一段前后文（命中的词在前后文里带底色），点一条跳到那一行、在页上标出来；阅读页底下留一条结果条，可以「上一条 / 下一条」，点中间的数字回到结果列表，不用重新输入；回车直接搜，搜完收起键盘。大小写不敏感，英文/数字按词边界（`the` 不命中 `theatre`），中文按子串。在线书和大 TXT 只搜这一窗已经取到正文的章节。
- 阅读统计：总时长、阅读天数、连续天数、最近七天，以及每本书自己的记录（累计时长、第一次/最近一次阅读、每天的时长）。
- 关于 Quire：应用版本、链接进来的 ebook-engine 版本与 C ABI 版本；排版页会显示引擎当前的缓存占用，阅读时换窗会主动回收用不到的章节缓存。
- 排版：字号、行距、页边距、字距、词距，纸色与夜间主题。
- 导入：文档选择器（可一次选多本），以及 Files / Mail 中的“用 Quire 打开”。EPUB 导入时读它的 Dublin Core 书目（书名、多作者、语言、出版社、标识符）。
- 格式：`.txt`、`.epub`。大 TXT（超过 8 MB 的 UTF-8）按章从文件里读，不整本进内存。
- 同步：书架、进度、书签、笔记和排版设置随同步走；云端正文按内容 hash 存放，打开云端的书时才下载那一本。云端默认是坚果云，也可选其他 WebDAV 服务器。入口在书架右上角的云图标与「我 → 同步与备份」，可开启启动时自动同步、自动备份书籍，或手动「备份全部」；本地缓存可设上限并按最久未读清理。
  云端布局：`catalog.json` 放书架、进度和设置，`meta/<key>.json` 放每本书的书签、笔记与阅读统计，`blobs/<hash>` 放正文；只有打开某本书时才下载它的正文。
- 搜索与书源：导入阅读3.0（Legado）书源 JSON（粘贴、链接或文件），在书源里搜书；详情、目录和正文都留在应用里。加入书架的是「在线书」：书架只记书源和书页，目录存一份，正文读到哪取到哪（文件型书源除外，见下）——引擎里是一本流式书，目录的每一章先占好位置，读到哪一章才取哪一章的正文替换进去（一窗 40 章，读完一窗换下一窗，引擎和字体不重开）；书架上一枚小地球标出它。书源规则和 JavaScript 由 legado-engine 执行：CSS、XPath、JSONPath、正则、`##` 替换、`{{…}}` 模板和 `@js:` / `<js>` 脚本，连同 cookie 与书源变量，都是 Legado 那一套。没有这个库的平台（iOS 模拟器、Windows、Android、网页）暂时没有书源搜索；等引擎支持这些目标，接入同一份 C ABI 就能用。带 `exploreUrl` 的书源会出现在「发现」里，按书源自己的分类浏览，不必先搜一个关键词。书源多的时候顶部一排书源横着挑，每个书源的分类是标签，一行排好几个，不再一个分类占一整行；分类地址里带 `{{page}}` / `{page}`（或 `<js>` 脚本自己在拼 `page`）的，列表末尾有「加载更多」，翻到没有新书为止。书源管理里能按名字、地址或分组搜索，批量开关 / 分组 / 删除；一条书源只列一次，名字下面用标签标出它属于哪些分组，顶部的分组按钮只筛不重复列。单条删除会先确认，停用的书源在列表里变淡。导入前先说是几个书源、叫什么（或哪里读不出来），也可以直接读剪贴板；导入后列出这次新进来的名字。

文件型书源（`bookSourceType: 3`，如知轩藏书）没有章节：详情页列出 `ruleBookInfo.downloadUrls` 里的每个文件，点「下载并导入」由引擎用书源自己的 header / cookie / 代理把文件取下来（`legado_download`，先写 `.part` 再改名，字节不动）。文件原样落盘，引擎打开 TXT 时按自己嗅出的 `charset` 读（GBK / Big5 都认）。判断一个地址是不是压缩包只看路径的扩展名，不看域名：`download.zxcs.zip` 是知轩的下载站，文件本身是 `.txt`。

需要登录的书源：书源管理里每条书源右边的钥匙按钮打开「变量」。应用先从书源的 `variable` 字段和 jsLib / 规则里的 `source.get('…')`、`source.put('…')` 找出它要哪些键，按名字逐项给输入框（密码、令牌类遮起来），也可以切回原始 JSON。保存时整段进 `source.getVariable()`，每个键也进 `source.get()`；面板里「变量」和「Cookie」两页切换填。书源自己的 `jsLib`（`java.post` 登录）在第一次搜索或发现时用它登录，Cookie 与规则请求共用同一个 per-source jar。引擎不读导入 JSON 里的 `variable` 字段，所以应用在导入时、以及每次启动时把书源自带的变量补写一次（已有值的不动）。

轻小说文库的登录页要图形验证码（`checkcode.php`），书源自己的 `jsLib` 只 POST 账号密码，所以面板多一页「验证码」：先取一张图，填上图上的字，登录时把 `checkcode` 和账号密码一起 POST（图片和登录用同一个源会话）。别的书源没有这一页。

书源要验证码、扫码，或者它的登录脚本登不上时，可以在浏览器登录后复制 Cookie 请求头，粘到面板的「Cookie」页：粘一份就是换一整套会话（先清掉这个书源的旧 Cookie，再写新的；引擎的 `legado_set_cookie` 只加不换，浏览器粘来的是 host-only、登录脚本下发的是域级，同名两条会一起发出去），搜索和发现都带上它，也可以一键清掉换账号。只填账号密码时，「搜一次验证」会先清掉旧 Cookie 再搜，免得书源的 `loggedIn()` 看到旧 Cookie 就不去登录。Asuka 的 `WebView` 扫码登录还没接。

## 平台

Quire 使用 Asuka 的跨平台运行时，同一套源码可构建 iOS、macOS、Linux、Android 与 Web。iOS 是当前 CI 持续产出的目标；其他平台使用同一套 Asuka 源码和对应的 ebook-engine 目标库构建。

WebDAV 同步走 `std/http`：iOS / macOS 的 URLSession、Linux 的 libsoup 与 Windows 的 WinHTTP 都能发 `PROPFIND` / `MKCOL`。Android 的 `HttpURLConnection` 只允许标准方法，这一版在 Android 上暂时用不了同步；网页端还受浏览器 CORS 的限制。

同步密码优先存系统钥匙串（`platform/secure`）。没有钥匙串的桌面（例如没装或没运行 Secret Service 的 Linux）会退回到应用目录里一个仅本人可读（`0600`）的 `sync-password` 文件，并在同步页提示；这样每次启动不用重输。

## Windows 构建（暂停）

`.github/workflows/windows.yml` 先停在手动触发（`workflow_dispatch`）。在 Linux 上用 Zig 0.14.1 能跑出 `dist/windows-amd64/Quire/Quire.exe` 与 `.zip`，但产物还不能用，原因不在这个仓库：

1. **引擎库得是 mingw 的**。`ebook-engine` 的 release 只出 `x86_64-pc-windows-msvc` 的 `ebook_abi.lib`，而 asuka 的 Windows 构建用 zig 的 mingw（`x86_64-windows-gnu`）链接：两套 CRT 混着链，`lld-link` 会报 `duplicate symbol .weak.__udivti3.default` 和一堆 MSVC 符号（`__chkstk`、`_fltused`、`WaitOnAddress`、`type_info::vftable`）。本地是拿引擎源码 `cargo build --target x86_64-pc-windows-gnu` 补的，`asuka.toml` 现在指向 mingw 版；release 补上这个目标（或流水线就地编）才能回 CI。
2. **asuka 0.2.12 的 Windows host 要裸名 `swprintf` / `swscanf`**，而 zig 的 mingw 是纯 UCRT，导入库里只有 `__stdio_common_vswprintf/vswscanf`（`msvcrt` 导入库干脆不存在）。`c/windows-legacy-stdio.c` 是把这两个名字转发过去的垫片，等 asuka 把 host 编成 mingw 那套就能删。
3. **exe 是 console 子系统**（`Subsystem = 3`，会闪黑框）：zig 的 `zig cc` 对 COFF 忽略 `-mwindows`，也不转发任何链接参数（`-Wl,/force:multiple`、`--subsystem` 一律 `unsupported linker arg`，0.14.1 与 0.17.1 都一样），asuka 没有能写链接参数的口子。

`[c.windows] libraries` 里的 `unwind`、`api-ms-win-crt-stdio-l1-1-0`、`api-ms-win-core-synch-l1-2-0` 是给 Rust 的 std 和上面那个垫片用的，换成 MSVC 的引擎库就没意义了。

## 架构

```text
Asuka 应用
  ├─ Tabs：首页 / 书架 / 发现；搜索在发现页的导航栏右上角
  ├─ Shelf → Reader（根层覆盖页）
  ├─ 搜索 / 书源（sources/）
  │      │ C ABI：quire_legado_*
  │      ▼
  │   legado-engine
  │      ├─ Legado 规则 + QuickJS
  │      └─ 每源 HTTP / cookie / 变量
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
  ├─ 全文检索（命中 + 上下文）
  └─ 光栅化为 EQP1 位图
          │
          ▼
BookPage（原生位图视图）
```

- 阅读页是应用的根层覆盖视图，不是系统 sheet；纸张覆盖状态栏区域。

引擎按 ebook-engine **0.5.1** 的 C ABI 构建（`[c.prebuilt]` 的库里要有
`eb_plain_metadata` / `eb_plain_selection_*` / `eb_plain_locator_*` /
`eb_plain_new_book` / `eb_plain_scroll_index_*`，以及 postcard 面的
`eb_request`）。0.4.0 把句柄改成了不透明
指针（Asuka 里是 `extern type Engine`，最后一个引用走时自动关闭），排版参数
改成带版本的结构体，选区变成调用方拥有的快照；`c/engine.c` 是把这些形状
交给 Asuka 的一层薄垫片。0.5.0 加了全文搜索（`Request::Search` →
`Response::Search`，只在 postcard 面上），`c/engine.c` 于是也写那一小段
postcard 请求，并把答案拆成 Asuka 能一个字段一个字段读的句柄；0.5.1 修了行
锚点的字节偏移，折行段落里的命中用 `locate` 落回真正的那一行。0.4.0 起它
给了三件这个应用正需要的东西：

- **稳定位置**：`Anchor`（章 + 段 + 段内字节）从 IR 里的东西变成 C ABI 的
  `eb_plain_locator_at` / `_locator_range` / `_locate`，书签、笔记和阅读进度
  都按它存，重排不再让位置失效。
- **流式章节**：`eb_plain_new_book` / `_append_chapter` / `_replace_chapter` /
  `_chapter_rev`，在线书不再拼 `window.txt`（不再靠「第 N 章」这种标题行
  切章），换窗只是换章节表，`chapter_rev` 就是页位图的缓存键。
- **本地大 TXT 也走这条路**：`eb_plain_new_book` 不只给在线书用。超过 8 MB 的
  UTF-8 文本（`localbook/`）开书时只扫一遍文件，记下每章从第几字节起、多长
  （只是几万个整数），正文读到哪一章才从文件里读那一段、替换进引擎里它自己
  的位置。一本 300 MB 的书因此不再要「宿主一份 + 引擎一份 + 解码后再一份」
  三份内存，读的时候只留窗口里的几十章。切章规则和引擎的 TXT 逐条对齐（标题
  行、以及「128 KB 就在行边界上断开」），所以章号不变，老书签不会挪位；
  GBK/Big5 的文件仍旧整本交给引擎——按章读只做 UTF-8。
- **全书高度索引**：`scroll_begin` 不再排整本书，每章先用字节数估高、读到
  才实测（Fenwick 前缀和 O(log n)），`eb_plain_scroll_index_write/_read` 把
  实测值落盘，同一屏同一字号下再打开直接是上次的精度。代价是总高先是个
  估计值，阅读页因此把「位置」记成章 + 章内比例（再加一个锚点，见
  `goToChapter` / `settleAnchor`），而不是全书的百分比。

顺带拿到的：排版前段缓存（同宽不同高不再重新断行）、`paint_window` 只画
窗口里的字形、`page(index)` O(1)、EPUB 惰性字体与资源。
- 分页模式按章排版，逐页光栅化。
- 滚动模式由引擎维护整本书的连续排版会话（章节起点、高度、跨章拼接），阅读器只按全书流坐标请求可见的位图条带，且只画条带内的内容。Asuka 的 `spawn` 不能带走一个 extern 句柄，所以条带和页一样在主线程光栅化；这是一个已知的取舍。
- 位图格式为 `EQP1`：12 字节头（宽、高）加 RGBA8 像素。像素不经过 Asuka 运行时。
- `BookPage` 负责各平台的位图显示与点按事件。

## 目录

| 路径 | 内容 |
| --- | --- |
| `app.asuka` | 根组件：三个标签页（首页 / 书架 / 发现）与阅读页覆盖层 |
| `icons.asuka` | 图标名：iOS/macOS 用 SF Symbols，其它平台用 Material Symbols |
| `shelf.asuka` | 书架、封面、导入、删除、显示方式与排序 |
| `reader.asuka` | 阅读器、翻页/滚动、目录、书签、选中与笔记、排版设置、书内搜索 |
| `library.asuka` | 书架与阅读状态的持久化 store，以及按需下载、备份和缓存清理 |
| `home.asuka` / `search.asuka` | 首页；发现与搜索：书源管理、导入、结果、详情与章节阅读 |
| `remote/` | 在线书的本机部分：目录、按章正文缓存、给引擎的三章窗口 |
| `sources/` | 书源：Legado 书源格式、导入与保存，以及 legado-engine 的 C 绑定与搜索/详情/目录/正文调用 |
| `stats.asuka` | 阅读统计页：总体数据、最近七天、每本书的记录 |
| `search_test.asuka` | 全文搜索：命中、章名与上下文、截断、流式章节换正文后的索引、命中位置的往返 |
| `sync.asuka` | 同步的文件格式与合并规则：catalog、每本书的 sidecar、删除记录 |
| `webdav.asuka` | WebDAV 客户端：认证、列目录、上传/下载、删除 |
| `sync_sheet.asuka` | 「同步」页：状态与动作；云端服务、账号等连接细节收在二级页 |
| `sync_test.asuka` | 列目录解析、URL、合并与 catalog/sidecar 的测试 |
| `engine/engine.asuka` | ebook-engine 的 C ABI 绑定 |
| `c/engine.c` | ABI 的垫片：`EbLayoutOptions` 结构体、选区快照和 `extern type` 的释放函数；`c/ebook_abi.h` 是同一版头文件 |
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

`asuka test` 运行应用测试（含 `native_test.asuka` 里原生书源引擎的规则与 JavaScript 测试，和 `search_test.asuka` 里全文搜索的命中、上下文、截断与命中位置），`asuka test lib` 运行 `lib/` 包的解析与分页测试。

## 引擎

两个引擎都来自各自的 GitHub Releases。ebook-engine 负责本地书的解析、排版、光栅化；
legado-engine 负责在线书源的规则、JavaScript、cookie 与变量。

```sh
# 默认使用最新非预发布版本
.github/scripts/fetch-engine.sh linux-x64
.github/scripts/fetch-legado.sh linux-x64

# 固定版本
ENGINE_VERSION=v0.5.1 .github/scripts/fetch-engine.sh linux-x64
LEGADO_VERSION=v0.3.0 .github/scripts/fetch-legado.sh linux-x64
```

ebook-engine 的下载会校验 Release 中的 `SHA256SUMS`。需要从源码构建时：

```sh
.github/scripts/build-engine.sh linux-x64 ../ebook-engine
.github/scripts/build-legado.sh linux-x64 ../legado-engine
```

CI 通过 `ENGINE_VERSION` / `LEGADO_VERSION` 选择版本，现在钉在 `v0.5.1` / `v0.3.0`：
这本书的应用按 ebook-engine 的 C ABI 写（稳定位置、流式章节、高度索引、全文搜索），
legado-engine 则提供 `legado_open` / `legado_search` / `legado_info` / `legado_toc` / `legado_content`
这几个调用，0.3.0 又加了 `legado_download`（书源的文件，原样落盘）和 `legado_eval`
（书源变量与书源自己的登录脚本）。换版本得先确认这些还在。

`asuka.toml` 的 `[c.prebuilt]` 里，`linux-x64`、`macos-arm64`、`macos-x64` 和
`ios-arm64` 同时列出 `libebook_abi.a` 与 `liblegado_engine.a`；`ios-simulator-*` 和
`windows-x64` 只有前者。`c/legado.c` 在没有后者的目标上把调用编译成“不可用”，
`sources/engine.asuka` 于是直接说这个平台暂时不能搜索书源。

两个静态库都带着 Rust 的 std，所以要来自同一套 Rust 工具链：要么都用 Release 的，
要么都用 `build-engine.sh` / `build-legado.sh` 在本机编译。把 Release 的 ebook-engine
和本机编译的 legado-engine 混在一起，链接器会报 `rust_eh_personality` 重复定义。

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
| Tests | `ubuntu-latest` | 获取 `linux-x64` 的两个引擎，运行 `asuka check`、`asuka test`、`asuka test lib` |
| Unsigned IPA | `macos-latest` | 获取 `ios-arm64` 的两个引擎，使用 Xcode 26+ 构建未签名设备 IPA |

未签名产物位于 `dist/Quire-unsigned.ipa`，作为 `ios-unsigned-ipa` artifact 上传。构建流程使用临时自签证书与 stub profile 完成 Asuka 的签名步骤，随后移除签名。

同一套流程也写在仓库根目录的 `codemagic.yaml` 里，Codemagic（https://codemagic.io）可直接用它构建，与 GitHub Actions 并存：

| Workflow | 机器 | 内容 |
| --- | --- | --- |
| `tests` | `mac_mini_m2`（Xcode 26.6） | 获取 `macos-arm64` 的两个引擎，运行 `asuka check`、`asuka test`、`asuka test lib` |
| `ios-ipa` | 同上 | 获取 `ios-arm64` 的两个引擎，构建未签名设备 IPA，`dist/*-unsigned.ipa` 作为 build 的 artifact 下载 |

接入步骤：在 Codemagic 里添加本仓库，它会读到 `codemagic.yaml`；自动构建还需在应用的 Webhooks 设置里建一个 webhook。只有 `.md` 改动的提交由 `when.changeset` 跳过。固定 Xcode 版本改 `definitions` 里的 `xcode`，固定引擎版本把 `ENGINE_VERSION` 写成 tag（如 `v0.5.1`）。

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
