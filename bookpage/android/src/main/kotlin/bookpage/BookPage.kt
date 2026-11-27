// BookPage on Android: a View whose picture is the engine's EQP1 raw bitmap
// file (12-byte header + RGBA8 pixels), read straight into a Bitmap and drawn
// to fill the view.
//
// The file is written by ebook-engine's ABI (`eb_plain_page_raw`) at the
// screen's scale, so one bitmap pixel is one device pixel. Reading it — the
// header, the pixels, the Bitmap over them — happens off the main thread: a
// page is a few megabytes, the reader crosses one every few hundred points at
// speed, and that work on the main thread is a dropped frame each time.
package bookpage

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.Executors

/// The page, the highlights over it, and the touches it takes.
private class PageView(context: Context, private val events: BookPageEvents) : View(context) {
    private val pagePaint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.DITHER_FLAG)
    private val markPaint = Paint()

    private var page: Bitmap? = null
    private var highlights: List<Highlight> = emptyList()
    private var selecting = false
    private var scale = 1.0

    /// What the reader is looking at must not blink out while the next page is
    /// read, and the same path asked for twice is read once.
    private var shown = ""
    private var requested = ""
    private var wanted = 0

    // One decode at a time, in the order they were asked for: the reader asks
    // nearest-first, and a fling must not put every core on bands nobody sees.
    private val loader = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "quire.page").apply { isDaemon = true }
    }
    private val main = Handler(Looper.getMainLooper())

    // The touches: a press that holds still starts a selection, motion while
    // down extends it, and the click that ends one is not a page turn.
    private var down = false
    private var dragging = false
    private var pressX = 0f
    private var pressY = 0f
    private var blockedUntil = 0L
    private var endedAt = 0L
    private var pendingHold: Runnable? = null

    fun setPath(value: String) {
        if (value.isEmpty() || value == shown || value == requested) return
        requested = value
        wanted += 1
        val token = wanted
        loader.execute {
            val decoded = loadPage(value)
            main.post {
                if (wanted != token) return@post
                if (decoded == null) {
                    // Not on disk (yet): let the next ask read it again.
                    requested = ""
                    return@post
                }
                shown = value
                page = decoded
                invalidate()
            }
        }
    }

    fun setHighlights(value: List<Highlight>) {
        highlights = value
        invalidate()
    }

    fun setSelecting(value: Boolean) {
        selecting = value
    }

    fun setScale(value: Double) {
        // The highlights are in points; the page's pixels are `scale` to a
        // point, and the view draws the two at the page's own size.
        scale = value
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val bitmap = page ?: return
        canvas.drawBitmap(bitmap, null, RectF(0f, 0f, width.toFloat(), height.toFloat()), pagePaint)
        if (highlights.isEmpty() || bitmap.width <= 0 || bitmap.height <= 0) return
        // In the page's pixels, as the file has them, then to the view's.
        val s = if (scale > 0) scale.toFloat() else 1f
        val kx = width.toFloat() / bitmap.width
        val ky = height.toFloat() / bitmap.height
        for (h in highlights) {
            val alpha = (h.color and 0xff).toInt()
            if (alpha == 0 || h.w <= 0.0 || h.h <= 0.0) continue
            markPaint.color = Color.argb(
                alpha,
                ((h.color shr 24) and 0xff).toInt(),
                ((h.color shr 16) and 0xff).toInt(),
                ((h.color shr 8) and 0xff).toInt(),
            )
            canvas.drawRect(
                ((h.x * s) * kx).toFloat(),
                ((h.y * s) * ky).toFloat(),
                (((h.x + h.w) * s) * kx).toFloat(),
                (((h.y + h.h) * s) * ky).toFloat(),
                markPaint,
            )
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val x = event.x
        val y = event.y
        val now = SystemClock.uptimeMillis()
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                down = true
                dragging = false
                pressX = x
                pressY = y
                if (selecting) {
                    // A drag from anywhere on the page moves the selection's
                    // nearer end: no second long press, no lifting first.
                    dragging = true
                    blockedUntil = now + 2000
                    events.hold(points(x), points(y))
                } else {
                    // A finger that drifts still begins a selection: the hold
                    // has no movement limit, it is only cancelled by motion.
                    val hold = Runnable {
                        if (down && !dragging) {
                            dragging = true
                            blockedUntil = SystemClock.uptimeMillis() + 2000
                            events.hold(points(pressX), points(pressY))
                        }
                    }
                    pendingHold = hold
                    postDelayed(hold, 350)
                }
            }

            MotionEvent.ACTION_MOVE -> {
                if (dragging) {
                    blockedUntil = now + 2000
                    events.drag(points(x), points(y))
                } else if (down) {
                    val dx = x - pressX
                    val dy = y - pressY
                    // A few points of jitter are still a tap, unless a
                    // selection is up: then the drag adjusts it.
                    if (dx * dx + dy * dy < 25 && !selecting) return true
                    cancelHold()
                    dragging = true
                    blockedUntil = now + 2000
                    events.hold(points(pressX), points(pressY))
                    events.drag(points(x), points(y))
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                cancelHold()
                down = false
                if (dragging) {
                    dragging = false
                    endedAt = now
                    events.release()
                    performClick()
                    return true
                }
                // The click that comes with a selection's lift is that
                // selection's own, not a page turn.
                if (now < blockedUntil || (endedAt != 0L && now - endedAt < 800)) return true
                events.press(points(x), points(y))
                performClick()
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    // The page's gestures are the page's: the view is what the reader touches,
    // and it is not a button besides.
    private fun cancelHold() {
        pendingHold?.let { removeCallbacks(it) }
        pendingHold = null
    }

    /// Device pixels to points, as the engine rendered the page.
    private fun points(px: Float): Double = px / (if (scale > 0) scale else 1.0)
}

/// The file as a Bitmap: `EQP1`, u32 width, u32 height, then RGBA8 — straight
/// alpha, 8 bits a channel, sRGB, which is Android's own ARGB_8888 order.
private fun loadPage(path: String): Bitmap? {
    val file = File(path)
    if (!file.isFile) return null
    val bytes = try {
        file.readBytes()
    } catch (_: java.io.IOException) {
        return null
    }
    if (bytes.size < 12) return null
    if (bytes[0] != 'E'.code.toByte() || bytes[1] != 'Q'.code.toByte() ||
        bytes[2] != 'P'.code.toByte() || bytes[3] != '1'.code.toByte()
    ) {
        return null
    }
    fun le32(offset: Int): Int =
        (bytes[offset].toInt() and 0xff) or
            ((bytes[offset + 1].toInt() and 0xff) shl 8) or
            ((bytes[offset + 2].toInt() and 0xff) shl 16) or
            ((bytes[offset + 3].toInt() and 0xff) shl 24)
    val width = le32(4)
    val height = le32(8)
    if (width <= 0 || height <= 0) return null
    val pixels = width.toLong() * height.toLong() * 4
    if (bytes.size.toLong() < 12 + pixels) return null
    val bitmap = try {
        Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
    } catch (_: IllegalArgumentException) {
        return null
    } catch (_: OutOfMemoryError) {
        return null
    }
    bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(bytes, 12, pixels.toInt()))
    return bitmap
}

/// What the view is: the page, the highlights over it, the touches it takes.
private class AndroidBookPage(events: BookPageEvents) : BookPage {
    private val page = PageView(events.context, events)

    override val view: View get() = page

    override fun setPath(value: String) {
        page.setPath(value)
    }

    override fun setHighlights(value: List<Highlight>) {
        page.setHighlights(value)
    }

    override fun setSelecting(value: Boolean) {
        page.setSelecting(value)
    }

    override fun setScale(value: Double) {
        page.setScale(value)
    }
}

/// Where a font's file is on Android, by family name. The system's faces are
/// files under /system/fonts, so the family name maps to one, probed by name.
private object SystemFonts : FontFiles {
    private val known = mapOf(
        "Songti SC" to listOf("/system/fonts/NotoSerifCJK-Regular.ttc"),
        "Songti TC" to listOf("/system/fonts/NotoSerifCJK-Regular.ttc"),
        "Noto Serif CJK SC" to listOf("/system/fonts/NotoSerifCJK-Regular.ttc"),
        "PingFang SC" to listOf(
            "/system/fonts/NotoSansCJK-Regular.ttc",
            "/system/fonts/DroidSansFallback.ttf",
        ),
        "Hiragino Sans GB" to listOf("/system/fonts/DroidSansFallback.ttf"),
        "Noto Sans CJK SC" to listOf("/system/fonts/NotoSansCJK-Regular.ttc"),
        "DejaVu Serif" to listOf("/system/fonts/DejaVuSerif.ttf"),
    )

    override fun path(family: String): String {
        for (file in known[family].orEmpty()) {
            if (File(file).canRead()) return file
        }
        return ""
    }
}

fun makeFontFiles(): FontFiles = SystemFonts

fun makeBookPage(events: BookPageEvents): BookPage = AndroidBookPage(events)
