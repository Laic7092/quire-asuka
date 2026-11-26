/* ebook_abi.h — ebook-engine 的 C ABI。
 *
 * 手写声明，与 `crates/ebook-abi/src` 下的 `pub extern "C" fn` 一一对应：
 * `tests/header.rs` 会扫 Rust 源码里的 `pub extern "C" fn`，这里漏一个、
 * 或者参数形状对不上，测试就红。CI 的 release 资产（`libebook_abi.a`）就是
 * 按这份头文件链接的。
 *
 * 两个面：
 *
 *   1. postcard 面 —— `eb_open` / `eb_request` / `eb_close`。一条入口收发
 *      `Request`/`Response` 枚举（postcard 编码，schema 见
 *      `crates/ebook-abi/src/protocol.rs`）。适合能读返回缓冲区的宿主（cgo、
 *      Swift、Rust）。
 *   2. 标量面 —— `eb_plain_*`。参数与结果都是标量/字符串/结构体；像素与封面
 *      既可以写文件，也可以走 `_bytes` 直接拿内存。适合只能拿 `Pointer` 不读
 *      内存的宿主（Asuka 的 `extern`，JNI 的薄包装）。
 *
 * 两个面共用的约定：
 *
 *   - **panic 不出边界**。所有入口 `catch_unwind`；panic 变成 `Error` 响应或
 *     负返回值，不会让进程 abort。因此 native 构建必须是 `panic = "unwind"`
 *     （cargo 的 `native` profile 已经这么配）。
 *   - **缓冲区归调用方**。`eb_alloc` / `eb_request` 返回的指针必须
 *     `eb_free(ptr, len)`。
 *   - **返回的 `const char *` 归库**，不要 free。有效期内是 NUL 结尾的 UTF-8：
 *       * `eb_version` / `eb_plain_version`：进程生命周期
 *       * `eb_plain_last_error` / `eb_last_error`：只增不减，旧指针不会失效
 *       * 其余 `eb_plain_*` 字符串：存在句柄自己的池里，直到 `eb_plain_close`
 *   - **选区归调用方**：`eb_plain_select` 返回 `EbSelection *`，用完必须
 *     `eb_plain_selection_free`；快照之间互不覆盖。
 *   - **句柄是不透明指针**：`eb_open` / `eb_plain_open` 返回 `EbEngine *`，
 *     失败返回 NULL。句柄关闭后不得再使用；其余入口对 NULL 返回 0 / `-1` /
 *     空串 / `ok == 0` 的结构体，不 panic。
 *   - **线程**：句柄内部互斥；任意线程调用安全，同一本书上的调用串行化。
 */

#ifndef EBOOK_ABI_H
#define EBOOK_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 版本 */

/* 结构布局版本，对应 `ebook_abi::ABI_VERSION`。任何 `repr(C)` 结构体加字段、
 * 改字段顺序或改既有函数签名都会 +1。宿主启动时：
 *
 *     if (eb_abi_version() != EB_ABI_VERSION) { 拒绝加载 }
 *
 * 只新增函数不算破坏性变更。 */
#define EB_ABI_VERSION 3

/* `eb_struct_size` 的 kind。宿主可用它做二次校验（头文件与库同源）。 */
#define EB_STRUCT_PAGE_INFO 1
#define EB_STRUCT_LOC 2
#define EB_STRUCT_SELECTION_RECT 3
#define EB_STRUCT_LINK 4
#define EB_STRUCT_SCROLL_CHAPTER 5
#define EB_STRUCT_SCROLL_LOC 6
#define EB_STRUCT_METADATA 7
#define EB_STRUCT_IMAGE_INFO 8
#define EB_STRUCT_LAYOUT_OPTIONS 9
#define EB_STRUCT_SCROLL_SLICE_OPTIONS 10
#define EB_STRUCT_SELECTION 11

/* `eb_plain_capabilities` 的位。 */
#define EB_CAP_EPUB 1u
#define EB_CAP_TXT 2u
#define EB_CAP_STREAM 4u
#define EB_CAP_IMAGES 8u
#define EB_CAP_FONTS 16u

/* 库版本串，如 "ebook-abi 0.2.1"。静态内存，永不失效。 */
const char *eb_version(void);
/* 结构布局版本；应与 EB_ABI_VERSION 相等。 */
uint32_t eb_abi_version(void);
/* 第 kind 个 `repr(C)` 结构体的字节数；未知 kind 返回 0。 */
size_t eb_struct_size(uint32_t kind);

/* ---------------------------------------------------------------- 缓冲区 */

/* 分配 len 字节交给宿主写请求；len 为 0 返回 NULL。 */
uint8_t *eb_alloc(size_t len);
/* 释放 eb_alloc / eb_request 返回的缓冲区。len 必须与当初一致。 */
void eb_free(uint8_t *ptr, size_t len);

/* ---------------------------------------------------------------- postcard 面 */

/* 不透明句柄，内部是 `Mutex<Engine>`。 */
typedef struct EbEngine EbEngine;

/* 打开一本书（EPUB 或纯文本，按魔数识别）。失败返回 NULL，原因见
 * eb_last_error。`name` 可以为 NULL。 */
EbEngine *eb_open(const uint8_t *bytes, size_t len, const char *name);
/* 关掉并释放句柄；只能关一次。 */
void eb_close(EbEngine *engine);

/* 上次打开失败的细节（UTF-8，**不带**结尾 0；`*len` 是字节数）。
 * 空串表示没有错误。错误串只增不减，旧指针不会失效；不要 free。 */
const uint8_t *eb_last_error(size_t *len);

/* 收一个 postcard `Request`，回一个 postcard `Response`。
 *
 * 返回的缓冲区用 eb_free 释放；`*out_len == 0` 时返回 NULL（只有请求本身
 * 不是合法 postcard 才会发生——业务错误也会编码成 `Response::Error`）。
 *
 * 线格式（postcard 是 varint 长度前缀 + 字段顺序，不含字段名）：
 *
 *   Request   前 16 个是 Layout / Bitmap / Hit / LinkAt / Selection /
 *             Progress / FragmentLine / ResolveLink / RegisterFont /
 *             RegisterFontFace / BaseFontSize / Metadata / Toc / Pages /
 *             Limits / Ping；之后是连续滚动和流式章节，Spine / Fonts 追加在
 *             枚举末尾
 *   Response  Ok / Pong / Metadata / Toc / Pages / Bitmap / Hit / Link /
 *             Selection / Progress / Line / Target / Font / Number / Error，
 *             滚动响应追加在后面；Spine / Fonts 追加在枚举末尾
 *
 * 字段类型与顺序见 `crates/ebook-abi/src/protocol.rs`；同一份 schema 也被
 * `tests/ffi.rs` 逐字段走一遍。标量面能做的事这里都能做（还多一个
 * `Selection` 的矩形数组），选它只是宿主能不能读内存的区别。 */
uint8_t *eb_request(EbEngine *engine, const uint8_t *req, size_t len, size_t *out_len);

/* ---------------------------------------------------------------- 标量面 */

/* 一页的元数据。按值回传，所以是 `repr(C)`。
 * width/height 是内容盒（视口减留白），不含留白。 */
typedef struct EbPageInfo {
  int32_t ok;        /* 1 有，0 没有 */
  float width;
  float height;
  float progress;    /* 本章内进度 0..1 */
} EbPageInfo;

/* 文本位置。page == -1 表示没有。 */
typedef struct EbLoc {
  int32_t page;
  uint32_t line;
  uint32_t byte;
  float progress;    /* 全书进度 0..1（-1 未知） */
} EbLoc;

/* 选区里的一块矩形。ok == 0 表示没有这一块。 */
typedef struct EbSelectionRect {
  int32_t ok;
  uint32_t page;
  float x;
  float y;
  float w;
  float h;
} EbSelectionRect;

/* 链接解析结果。ok == 0 表示解不出。外部链接也算解出（external == 1）。
 * fragment 指向句柄字符串池（不带 '#'），在 eb_plain_close 前有效。 */
typedef struct EbLink {
  int32_t ok;
  int32_t spine;          /* -1：不在阅读顺序里 */
  int32_t external;       /* 1 外部链接，0 书内 */
  const char *fragment;   /* 没有 fragment 时是空串 */
} EbLink;

/* 选区快照。由 eb_plain_select 分配，调用方必须 eb_plain_selection_free。
 * text / rects 归快照所有，不要单独 free。 */
typedef struct EbSelection {
  EbLoc start;
  EbLoc end;
  uint32_t rect_count;
  const char *text;
  const EbSelectionRect *rects;
} EbSelection;

/* 连续滚动会话里的一章。ok == 0 表示这一章排不出来（height 按 0 计）。 */
typedef struct EbScrollChapter {
  int32_t ok;
  uint32_t spine;    /* 越界下标回 UINT32_MAX */
  float top;         /* 全书流坐标里的起点 */
  float height;      /* 内容高度 */
} EbScrollChapter;

/* 滚动模式下的命中。spine == -1 表示没有。 */
typedef struct EbScrollLoc {
  int32_t spine;
  uint32_t line;     /* 全章行号 */
  uint32_t byte;     /* 行内字节偏移 */
  float y;           /* 该行行盒顶边的全书流坐标 */
} EbScrollLoc;

/* 一次取齐的书目。字符串属于句柄，在 eb_plain_close 前一直有效。 */
typedef struct EbMetadata {
  int32_t ok;              /* 1 有，0 句柄无效 */
  uint32_t creator_count;  /* eb_plain_creator_at 的范围 */
  const char *title;
  const char *creator;     /* 第一作者；等价于 creator_at(0) */
  const char *language;
  const char *publisher;
  const char *identifier;
  const char *date;
  const char *description;
  const char *cover_href;
} EbMetadata;

/* 当前章的一张图片。id 就是 image_full_write / image_full_bytes 的入参。 */
typedef struct EbImageInfo {
  int32_t ok;
  uint32_t id;
  uint32_t width;          /* 固有尺寸；读不出时 0 */
  uint32_t height;
  const char *mime;
  const char *src;         /* 归档内路径 */
  const char *alt;
} EbImageInfo;

/* 排版参数。size 填 sizeof(EbLayoutOptions)，version 填
 * EB_LAYOUT_OPTIONS_VERSION；以后加字段只增大 size。 */
typedef struct EbLayoutOptions {
  uint32_t size;
  uint32_t version;
  uint32_t spine;
  uint32_t scroll;      /* 0 分页，1 整章一页滚动流 */
  float width;
  float height;
  float top;
  float right;
  float bottom;
  float left;
  float font_scale;
  float line_scale;
  float letter_px;
  float word_px;
  uint32_t align;
} EbLayoutOptions;

#define EB_LAYOUT_OPTIONS_VERSION 1

/* 排一章并取一个滚动窗口。 */
typedef struct EbScrollSliceOptions {
  uint32_t size;
  uint32_t version;
  EbLayoutOptions layout;
  float y;
  float slice_height;
  float scale;
  uint32_t ink;
  uint32_t paper;
  bool bold;
} EbScrollSliceOptions;

#define EB_SCROLL_SLICE_OPTIONS_VERSION 1


/* 标量面的版本检查（与上面两个同义，只是给不用 postcard 面的宿主）。 */
const char *eb_plain_version(void);
uint32_t eb_plain_abi_version(void);

/* 最近一次失败的原因（NUL 结尾，"" 没有）。错误串只增不减，旧指针不会失效。 */
const char *eb_plain_last_error(void);

/* 打开一本书，返回句柄；失败 NULL，原因见 eb_plain_last_error。 */
EbEngine *eb_plain_open(const uint8_t *data, size_t len, const char *name);
/* 关掉并释放句柄。0 好，-1 空句柄。关闭后不得再使用这个指针。 */
int32_t eb_plain_close(EbEngine *engine);

/* postcard 面：新建空书（流式章节），返回与 eb_open 相同的句柄；失败 NULL。 */
EbEngine *eb_engine_new_book(const char *title);

/* ---- 章节流（在线书源，G-26） ----
 *
 * 不传整本字节：先 eb_plain_new_book，再逐章 append/replace。
 * 章节版本（rev）在 append/replace 后变化，宿主用它做页位图的缓存键。 */
EbEngine *eb_plain_new_book(const char *title);     /* 失败 NULL */
/* 返回新章数（>= 1）；-1 句柄/参数错；-2 不是流式书/空文本/超单章上限。 */
int32_t eb_plain_append_chapter(EbEngine *engine, const char *title, const char *text);
/* 0 成功；-1 句柄/参数错；-2 越界/空文本/超限。 */
int32_t eb_plain_replace_chapter(EbEngine *engine, uint32_t spine, const char *title, const char *text);
/* 0 成功；-1 句柄错；-2 不是流式书。 */
int32_t eb_plain_clear_chapters(EbEngine *engine);
/* 内容版本；0 表示没有这一章或不是流式书。 */
uint64_t eb_plain_chapter_rev(EbEngine *engine, uint32_t spine);

/* ---- 高度索引持久化 ----
 *
 * 键是书指纹 + 视口 + 样式 + 字体版本；键或章数不匹配时 read 返回 0。 */
/* 写入字节数；-1 没有滚动会话/句柄错；-3 写失败。 */
int32_t eb_plain_scroll_index_write(EbEngine *engine, const char *path);
/* 1 成功；0 不匹配/不可读；-1 句柄/参数错。 */
int32_t eb_plain_scroll_index_read(EbEngine *engine, const char *path);

/* ---- 能力 ---- */

/* 当前后端的能力位（EB_CAP_*）。 */
uint32_t eb_plain_capabilities(EbEngine *engine);

/* ---- 书目 ---- */

/* 一次取齐 metadata；比逐个 getter 少很多往返。 */
EbMetadata eb_plain_metadata(EbEngine *engine);

/* 单字段 getter 保留，适合只缺一个字段的宿主。 */
const char *eb_plain_title(EbEngine *engine);        /* "" 没有 */
const char *eb_plain_language(EbEngine *engine);     /* dc:language，退回 lang */
const char *eb_plain_creator(EbEngine *engine);      /* 第一作者 */
uint32_t eb_plain_creator_count(EbEngine *engine);   /* 多作者 */
const char *eb_plain_creator_at(EbEngine *engine, uint32_t index);
const char *eb_plain_publisher(EbEngine *engine);
const char *eb_plain_identifier(EbEngine *engine);   /* 通常是 ISBN/UUID */
const char *eb_plain_date(EbEngine *engine);         /* 原始字符串，不解析 */
const char *eb_plain_description(EbEngine *engine);
const char *eb_plain_cover_href(EbEngine *engine);   /* 归档内路径，"" 没有 */

/* ---- 封面 ----
 *
 * 封面字节只走文件：宿主给路径，库写 PNG/JPEG 原始字节。
 * 扩展名/解码方式由 eb_plain_cover_mime 决定。 */
const char *eb_plain_cover_mime(EbEngine *engine);              /* "image/png" / "" */
size_t eb_plain_cover_len(EbEngine *engine);                    /* 0 没有封面 */
int32_t eb_plain_cover_write(EbEngine *engine, const char *path); /* 0 好，-1 没有，-3 写失败 */

/* ---- 阅读顺序（spine）---- */

uint32_t eb_plain_spine_count(EbEngine *engine);
const char *eb_plain_spine_href(EbEngine *engine, uint32_t index);        /* 归档内路径 */
const char *eb_plain_spine_id(EbEngine *engine, uint32_t index);          /* manifest id */
const char *eb_plain_spine_media_type(EbEngine *engine, uint32_t index);
int32_t eb_plain_spine_linear(EbEngine *engine, uint32_t index);          /* 1 在阅读顺序里 */
/* 最近一次排版的章节下标（-1 没有）。 */
int32_t eb_plain_last_spine(EbEngine *engine);

/* ---- 目录（TOC）---- */

uint32_t eb_plain_toc_count(EbEngine *engine);
const char *eb_plain_toc_title(EbEngine *engine, uint32_t index);
const char *eb_plain_toc_href(EbEngine *engine, uint32_t index);      /* 不含 fragment */
const char *eb_plain_toc_fragment(EbEngine *engine, uint32_t index);  /* 不带 '#'，"" 没有 */
int32_t eb_plain_toc_spine(EbEngine *engine, uint32_t index);         /* -1 不在阅读顺序里 */
uint32_t eb_plain_toc_depth(EbEngine *engine, uint32_t index);        /* 0 起算 */

/* ---- 字体 ----
 *
 * 排版前至少注册一个字体；没有字体时页照样出，但位图上没有字。
 * 返回字体 id（>= 0）；-1 句柄/参数错，-2 字体解析失败。 */
int32_t eb_plain_register_font(EbEngine *engine, const uint8_t *bytes, size_t len);
/* 注册字体集合（TTC/OTC）里的第 index 个面，先注册整份文件。 */
int32_t eb_plain_register_font_face(EbEngine *engine, uint32_t index);
uint32_t eb_plain_font_count(EbEngine *engine);

/* ---- 排版 ---- */

/* options 一次带全参数：ABI 不保存 viewport/style。
 * width/height 是视口（含留白），top/right/bottom/left 是留白，
 * font_scale/line_scale 是倍率，letter_px/word_px 是额外字距/词距（点），
 * align：0 原书、1 左、2 中、3 右、4 两端。
 * 0 好；-1 句柄/参数错；-2 排版失败（原因见 eb_plain_last_error）。 */
int32_t eb_plain_layout(EbEngine *engine, const EbLayoutOptions *options);

/* 排出来的页数；滚动模式下是 1。 */
/* 排出来的页数；滚动模式下是 1。 */
uint32_t eb_plain_page_count(EbEngine *engine);
/* 滚动模式的内容高度（本章流坐标总长）；分页模式/没排版时 0。 */
float eb_plain_content_height(EbEngine *engine);
/* 一页的逻辑尺寸与本章进度。ok == 0 表示没有这一页。 */
EbPageInfo eb_plain_page_info(EbEngine *engine, uint32_t page);

/* ---- 像素 ----
 *
 * 位图可以写成 EQP1 文件（`_raw`），也可以直接返回 EQP1 字节（`_bytes`）：
 *
 *   "EQP1" | u32le width | u32le height | width*height*4 bytes RGBA8
 *
 * 行优先，直通 alpha（非预乘）。`scale` 是设备像素比，`ink`/`paper` 是
 * 0xRRGGBBAA 主题色（0 = 不修正 / 白纸），`bold` 全局加粗。`ink` 只替换
 * 默认字色（正文主导色），书里显式写的链接色/强调色保留。
 * 位图覆盖整个视口（含留白），不是内容盒。
 * 写文件是原子的：先写 {path}.{pid}.{seq}.part 再 rename；宿主用「文件存在」
 * 判断可用时不会读到半张位图。
 * 返回：0 好，-1 句柄/路径错，-2 光栅化失败，-3 写文件失败。 */
int32_t eb_plain_page_raw(EbEngine *engine, uint32_t page, float scale, uint32_t ink,
                          uint32_t paper, bool bold, const char *path);

/* 滚动流的一个窗口：y/height 是点（y 在 [0, content_height) 里）。
 * 同一个 y 再来一次得到逐字节相同的文件。 */
int32_t eb_plain_page_slice_raw(EbEngine *engine, uint32_t page, float y, float height,
                                float scale, uint32_t ink, uint32_t paper,
                                bool bold, const char *path);

/* 当前页的 EQP1 字节；用 eb_free(ptr, len) 释放。 */
uint8_t *eb_plain_page_bytes(EbEngine *engine, uint32_t page, float scale,
                             uint32_t ink, uint32_t paper, bool bold, size_t *out_len);
/* 当前页一段的 EQP1 字节；用 eb_free(ptr, len) 释放。 */
uint8_t *eb_plain_page_slice_bytes(EbEngine *engine, uint32_t page, float y,
                                   float height, float scale, uint32_t ink,
                                   uint32_t paper, bool bold, size_t *out_len);

/* 排一章并取它的一个滚动窗口，一次调用完成：后台线程取瓦片时，排版与光栅
 * 之间不会插入别的调用。 */
int32_t eb_plain_scroll_slice_raw(EbEngine *engine,
                                  const EbScrollSliceOptions *options,
                                  const char *path);

/* ---- 连续滚动（跨章）----
 *
 * 一次性开始会话，之后全部用**全书流坐标**：章节起点由引擎自己算
 * （懒量高度），宿主不再维护“章节 top 表”、按章调度排版。上下留白会被
 * 置 0（滚动容器自己留），左右保留。 */
int32_t eb_plain_scroll_begin(EbEngine *engine, const EbLayoutOptions *options);
/* 书的章节数（滚动会话的章下标范围就是它）；0 句柄/空书。 */
uint32_t eb_plain_scroll_chapter_count(EbEngine *engine);
/* 一章：起点/高度/成没成。高度按视口高向上取整（至少一屏）。ok == 0 表示
 * 下标越界或这一章排不出来。 */
EbScrollChapter eb_plain_scroll_chapter(EbEngine *engine, uint32_t index);
/* 全书流坐标总长（滚动容器的 contentSize）；首次调用量完全书，-1 错。 */
float eb_plain_scroll_total_height(EbEngine *engine);
/* 全书流坐标落在哪一章（-1 超出内容或没有会话）。 */
int32_t eb_plain_scroll_spine_at(EbEngine *engine, float y);
/* 某一章的起点；取不到 -1。 */
float eb_plain_scroll_chapter_top(EbEngine *engine, uint32_t spine);
/* 全书流坐标的一段，写成 EQP1；可以跨章，章缝不会错行。 */
int32_t eb_plain_scroll_band_raw(EbEngine *engine, float y, float height, float scale,
                                 uint32_t ink, uint32_t paper, bool bold,
                                 const char *path);
/* 同上，但把 EQP1 字节交给宿主：用 eb_free(ptr, *out_len) 释放。
 * 能读内存的宿主用它，瓦片不必先落盘、自己管文件名与磁盘缓存。 */
uint8_t *eb_plain_scroll_band_bytes(EbEngine *engine, float y, float height, float scale,
                                    uint32_t ink, uint32_t paper, bool bold,
                                    size_t *out_len);
/* 全书流坐标 → 原文位置（跨章）。 */
EbScrollLoc eb_plain_scroll_hit(EbEngine *engine, float x, float y);
/* 行号 → 全书流坐标（行盒顶边）；-1 没有。 */
float eb_plain_scroll_line_y(EbEngine *engine, uint32_t spine, uint32_t line);
/* "#fragment" → 全书流坐标；这一章没有该锚点或出错回 -1。 */
float eb_plain_scroll_anchor_y(EbEngine *engine, uint32_t spine, const char *fragment);
/* 全书流坐标 → 滚动进度 0..1；出错回 -1（首次调用量完全书）。 */
float eb_plain_scroll_progress(EbEngine *engine, float y);

/* ---- 位置与交互 ---- */

/* 页内点（点坐标）→ 原文位置；page == -1 表示没有命中。 */
EbLoc eb_plain_hit(EbEngine *engine, uint32_t page, float x, float y);
/* 位置 → 全书进度 0..1（-1 未知）。 */
float eb_plain_progress(EbEngine *engine, uint32_t page, uint32_t line, uint32_t byte);
/* 稳定位置：spine:node:offset。重排/换字号后仍指向同一段原文。 */
const char *eb_plain_locator_at(EbEngine *engine, uint32_t page, float x, float y);
const char *eb_plain_locator_range(EbEngine *engine, uint32_t a_page, uint32_t a_line,
                                   uint32_t a_byte, uint32_t b_page,
                                   uint32_t b_line, uint32_t b_byte);
EbLoc eb_plain_locate(EbEngine *engine, const char *locator);
/* 页内点 → 链接地址（"" 没有）。 */
const char *eb_plain_link_at(EbEngine *engine, uint32_t page, float x, float y);
/* 当前章 "#fragment" 的行号；-1 本章没有这个锚点。 */
int32_t eb_plain_fragment_line(EbEngine *engine, const char *fragment);

/* 选区。端点按 (page, line, byte) 给，顺序无所谓（库会规范化）。
 * 成功返回快照指针，失败 NULL（原因见 eb_plain_last_error）。
 * 宿主用完必须 eb_plain_selection_free；快照之间互不覆盖。 */
EbSelection *eb_plain_select(EbEngine *engine, uint32_t a_page, uint32_t a_line,
                             uint32_t a_byte, uint32_t b_page, uint32_t b_line,
                             uint32_t b_byte);
/* 释放选区快照；NULL 安全，同一快照只能释放一次。 */
void eb_plain_selection_free(EbSelection *selection);

/* 在最近一次排版的那一章里解析书内链接。ok == 0 表示解不出。
 * fragment 就在返回值里，不带 '#'。 */
EbLink eb_plain_resolve_link(EbEngine *engine, const char *href);

/* 资源上限：单页像素与书级图片缓存（解码后的 RGBA 字节数）。
 * 默认 4096×4096 / 24 MiB。0 好，-1 句柄无效。 */
int32_t eb_plain_set_limits(EbEngine *engine, uint64_t max_page_pixels, uint64_t image_cache_bytes);

/* 释放可再生的缓存（跨章 IR、断行缓存、图片 RGBA、字体位图），保留当前页。
 * 0 好，-1 句柄无效。 */
int32_t eb_plain_trim_memory(EbEngine *engine);
/* 当前缓存占用的近似字节数；0 表示句柄无效或没有缓存。 */
uint64_t eb_plain_memory_bytes(EbEngine *engine);

/* ---- 图片 ---- */

/* 当前章图片数；下标就是 image_id。 */
uint32_t eb_plain_image_count(EbEngine *engine);
/* 第 index 张图片的元数据；ok == 0 表示越界/没排版。 */
EbImageInfo eb_plain_image_info(EbEngine *engine, uint32_t index);

/* 当前章一张图片的原分辨率 RGBA，写成 EQP1 文件（点击查看原图用）。
 * 0 好，-1 图片不存在，-3 写失败。 */
int32_t eb_plain_image_full_write(EbEngine *engine, uint32_t image_id, const char *path);
/* 同上，但返回 EQP1 字节；用 eb_free(ptr, len) 释放。 */
uint8_t *eb_plain_image_full_bytes(EbEngine *engine, uint32_t image_id, size_t *out_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EBOOK_ABI_H */
