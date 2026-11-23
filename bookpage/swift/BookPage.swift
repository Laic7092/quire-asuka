// BookPage on iOS and macOS: a UIImageView (or NSImageView) whose image is
// the engine's EQP1 raw bitmap, mapped from the file, not copied.
//
// The file is written by ebook-engine's ABI (`eb_plain_page_raw`) at the
// screen's scale, so one bitmap pixel is one device pixel. Reading it — the
// mapping, the header, the platform's image over it — happens off the main
// thread: a band is a few megabytes, the reader crosses one every 512 points
// at speed, and that work on the main thread is a dropped frame each time.
import Asuka
import Foundation

#if canImport(UIKit)
import UIKit
typealias PageImage = UIImage
#else
import AppKit
typealias PageImage = NSImage
#endif

/// A long press that remembers where the finger went down. Its movement
/// limit is off: a reader's finger is never perfectly still, and a press
/// that fails after a few points never gets to grow a selection.
#if canImport(UIKit)
private final class PageHoldRecognizer: UILongPressGestureRecognizer {
    var down: CGPoint = .zero

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        if let touch = touches.first, let target = view { down = touch.location(in: target) }
        super.touchesBegan(touches, with: event)
    }
}
#endif

/// The highlights over the page: the notes' and the live selection's. It
/// sits in the page's own view, so it scrolls and resizes with it, and it
/// takes no touch: the page's gestures must reach the page beneath.
#if canImport(UIKit)
private final class PageHighlightView: UIView {
    var highlights: [Highlight] = [] { didSet { setNeedsDisplay() } }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = false
        backgroundColor = nil
        contentMode = .redraw
        isUserInteractionEnabled = false
        isAccessibilityElement = false
    }

    required init?(coder: NSCoder) { fatalError() }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        for h in highlights {
            guard let color = AsukaColors.color(h.color) else { continue }
            ctx.setFillColor(color.cgColor)
            ctx.fill(CGRect(x: h.x, y: h.y, width: h.w, height: h.h))
        }
    }
}
#else
private final class PageHighlightView: NSView {
    var highlights: [Highlight] = [] { didSet { needsDisplay = true } }

    override var isFlipped: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = nil
    }

    required init?(coder: NSCoder) { fatalError() }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func draw(_ rect: NSRect) {
        for h in highlights {
            guard let color = AsukaColors.color(h.color) else { continue }
            color.setFill()
            NSBezierPath(rect: NSRect(x: h.x, y: h.y, width: h.w, height: h.h)).fill()
        }
    }
}
#endif

/// The mapping, released with the CGImage.
private final class PageMapping {
    let base: UnsafeMutableRawPointer
    let size: Int

    init(base: UnsafeMutableRawPointer, size: Int) {
        self.base = base
        self.size = size
    }

    deinit { munmap(base, size) }
}

@MainActor final class SwiftBookPage: NSObject, BookPage {
    private let picture: AsukaPlatformView
    private let highlightsView = PageHighlightView(frame: .zero)
    private let events: BookPageEvents
    private var image: PageImage?
    /// A touch is adjusting the selection: the page takes a drag from
    /// wherever it lands, rather than waiting for another long press.
    private var selecting = false
    #if canImport(UIKit)
    private var hold: PageHoldRecognizer?
    private var adjust: UIPanGestureRecognizer?
    #endif
    /// The file the image above came from, the one asked for last, and the one
    /// in flight. A band takes a moment to decode, and the window can have moved
    /// on by then: the late one must not be put up in the place of the newer.
    private var shown = ""
    private var requested = ""
    private var wanted = 0
    /// A tap before this time is the long press's own: while it is held, and
    /// for a moment after it lifts. The two recognizers see the same lift, in
    /// no promised order, so the guard is set when the press begins, not when
    /// it ends.
    private var tapBlockedUntil: CFTimeInterval = 0

    var view: AsukaPlatformView { picture }

    init(_ events: BookPageEvents) {
        self.events = events
#if canImport(UIKit)
        let imageView = UIImageView()
        imageView.contentMode = .scaleToFill
        imageView.clipsToBounds = true
        imageView.isOpaque = true
        imageView.backgroundColor = .white
        imageView.isUserInteractionEnabled = true
        picture = imageView
#else
        let imageView = NSImageView()
        imageView.imageScaling = .scaleAxesIndependently
        imageView.wantsLayer = true
        picture = imageView
#endif
        // Every stored property is set: the gesture recognizers below take
        // self as their target, which only a built object may be.
        super.init()
        highlightsView.frame = picture.bounds
#if canImport(UIKit)
        highlightsView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
#else
        highlightsView.autoresizingMask = [.width, .height]
#endif
        picture.addSubview(highlightsView)
#if canImport(UIKit)
        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        picture.addGestureRecognizer(tap)
        // A long press starts a selection; moving while held extends it. Its
        // movement limit is off, so a finger that drifts still begins one.
        let hold = PageHoldRecognizer(target: self, action: #selector(handleLongPress(_:)))
        hold.minimumPressDuration = 0.35
        hold.allowableMovement = .greatestFiniteMagnitude
        picture.addGestureRecognizer(hold)
        self.hold = hold
        // Once a selection exists, a drag from anywhere on the page moves its
        // nearer end: no second long press, no lifting the finger first. It is
        // always tracking, and lets go of touches that begin without one.
        let adjust = UIPanGestureRecognizer(target: self, action: #selector(handleAdjust(_:)))
        adjust.maximumNumberOfTouches = 1
        adjust.delegate = self
        picture.addGestureRecognizer(adjust)
        self.adjust = adjust
#else
        let click = NSClickGestureRecognizer(target: self, action: #selector(handleClick(_:)))
        picture.addGestureRecognizer(click)
        // A desktop's mouse selects by dragging, as it does in any text.
        let pan = NSPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        picture.addGestureRecognizer(pan)
#endif
    }

#if canImport(UIKit)
    /// A tap, in the page's points: the reader turns the page by its third.
    @objc private func handleTap(_ recognizer: UITapGestureRecognizer) {
        guard let target = recognizer.view else { return }
        if ProcessInfo.processInfo.systemUptime < tapBlockedUntil { return }
        let point = recognizer.location(in: target)
        events.press(x: Double(point.x), y: Double(point.y))
    }

    @objc private func handleLongPress(_ recognizer: UILongPressGestureRecognizer) {
        guard let target = recognizer.view else { return }
        let point = recognizer.location(in: target)
        switch recognizer.state {
        case .began:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 2
            // Where the finger came down, not where the long press was
            // recognized: with drift allowed, they can be far apart.
            let down = (recognizer as? PageHoldRecognizer)?.down ?? point
            events.hold(x: Double(down.x), y: Double(down.y))
        case .changed:
            // Push the guard while the finger is still down; if the press goes
            // away without an end (the app going to the background), it lapses.
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 2
            events.drag(x: Double(point.x), y: Double(point.y))
        case .ended, .cancelled, .failed:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 0.8
            events.release()
        default:
            break
        }
    }

    /// A touch while a selection is up: the drag's first point is the one the
    /// finger came down at, so the end nearest it is the one that moves.
    @objc private func handleAdjust(_ recognizer: UIPanGestureRecognizer) {
        guard let target = recognizer.view, selecting else { return }
        let point = recognizer.location(in: target)
        switch recognizer.state {
        case .began:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 2
            let offset = recognizer.translation(in: target)
            events.hold(x: Double(point.x - offset.x), y: Double(point.y - offset.y))
        case .changed:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 2
            events.drag(x: Double(point.x), y: Double(point.y))
        case .ended, .cancelled:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 0.8
            events.release()
        default:
            break
        }
    }
#else
    @objc private func handleClick(_ recognizer: NSClickGestureRecognizer) {
        guard let target = recognizer.view else { return }
        if ProcessInfo.processInfo.systemUptime < tapBlockedUntil { return }
        let point = recognizer.location(in: target)
        events.press(x: Double(point.x), y: Double(target.bounds.height - point.y))
    }

    @objc private func handlePan(_ recognizer: NSPanGestureRecognizer) {
        guard let target = recognizer.view else { return }
        let point = recognizer.location(in: target)
        // The drag is recognized after a few points of movement: take the
        // finger's first place from the translation, so the word selected is
        // the one the press was on, not the one it had moved to.
        let offset = recognizer.translation(in: target)
        let start = NSPoint(x: point.x - offset.x, y: point.y - offset.y)
        switch recognizer.state {
        case .began:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 2
            events.hold(x: Double(start.x), y: Double(target.bounds.height - start.y))
        case .changed:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 2
            events.drag(x: Double(point.x), y: Double(target.bounds.height - point.y))
        case .ended, .cancelled:
            tapBlockedUntil = ProcessInfo.processInfo.systemUptime + 0.8
            events.release()
        default:
            break
        }
    }
#endif

    func setHighlights(_ value: [Highlight]) {
        highlightsView.highlights = value
    }

    func setSelecting(_ value: Bool) {
        selecting = value
    }

    func setScale(_ value: Double) {
        // The highlights are in points, as the view is: no conversion.
        _ = value
    }

    func setPath(_ value: String) {
        // Nothing to show yet, or the page already on screen, or on its way: the
        // picture the reader is looking at must not blink out while the next one
        // is read, and the same path asked for twice is read once.
        guard !value.isEmpty, value != shown, value != requested else { return }
        requested = value
        wanted += 1
        let token = wanted
        PageCache.shared.image(value) { [weak self] image in
            guard let self, self.wanted == token else { return }
            guard let image else {
                // Not on disk (yet): let the next ask read it again.
                self.requested = ""
                return
            }
            self.shown = value
            self.image = image
#if canImport(UIKit)
            (self.picture as? UIImageView)?.image = image
#else
            (self.picture as? NSImageView)?.image = image
#endif
        }
    }
}

@MainActor func makeBookPage(_ events: BookPageEvents) -> any BookPage {
    SwiftBookPage(events)
}

#if canImport(UIKit)
extension SwiftBookPage: UIGestureRecognizerDelegate {
    /// The adjusting drag may begin only over a selection; without one it
    /// fails, and the long press and the tap go on as before. A recognizer
    /// switched on under a finger would lose the touch that made the
    /// selection, and the drag with it.
    func gestureRecognizerShouldBegin(_ recognizer: UIGestureRecognizer) -> Bool {
        if recognizer === adjust { return selecting }
        return true
    }
}
#endif

/// The file as an image: `EQP1`, u32 width, u32 height, then RGBA8. Straight
/// alpha, 8 bits a channel, sRGB — what the engine writes.
///
/// Outside the page (and off the main actor) on purpose: this is the work the
/// cache does on its own queue.
private func loadPage(_ path: String) -> PageImage? {
    guard !path.isEmpty else { return nil }
    let descriptor = open(path, O_RDONLY)
    guard descriptor >= 0 else { return nil }
    defer { close(descriptor) }

    var info = stat()
    guard fstat(descriptor, &info) == 0, info.st_size > 12 else { return nil }
    let total = Int(info.st_size)
    guard let base = mmap(nil, total, PROT_READ, MAP_PRIVATE, descriptor, 0),
          base != MAP_FAILED
    else { return nil }

    let header = base.assumingMemoryBound(to: UInt8.self)
    guard header[0] == UInt8(ascii: "E"), header[1] == UInt8(ascii: "Q"),
          header[2] == UInt8(ascii: "P"), header[3] == UInt8(ascii: "1")
    else {
        munmap(base, total)
        return nil
    }
    func le32(_ offset: Int) -> Int {
        Int(header[offset]) | Int(header[offset + 1]) << 8
            | Int(header[offset + 2]) << 16 | Int(header[offset + 3]) << 24
    }
    let width = le32(4), height = le32(8)
    let pixels = width * height * 4
    guard width > 0, height > 0, total >= 12 + pixels else {
        munmap(base, total)
        return nil
    }

    let mapping = PageMapping(base: base, size: total)
    let holder = Unmanaged.passRetained(mapping)
    let provider = CGDataProvider(
        dataInfo: holder.toOpaque(),
        data: base + 12,
        size: pixels,
        releaseData: { info, _, _ in
            guard let info else { return }
            Unmanaged<PageMapping>.fromOpaque(info).release()
        }
    )
    guard let provider,
          let space = CGColorSpace(name: CGColorSpace.sRGB),
          let cgImage = CGImage(
              width: width,
              height: height,
              bitsPerComponent: 8,
              bitsPerPixel: 32,
              bytesPerRow: width * 4,
              space: space,
              bitmapInfo: CGBitmapInfo(
                  rawValue: CGImageAlphaInfo.last.rawValue
                      | CGBitmapInfo.byteOrder32Big.rawValue),
              provider: provider,
              decode: nil,
              shouldInterpolate: true,
              intent: .defaultIntent
          )
    else {
        holder.release()
        return nil
    }
#if canImport(UIKit)
    return UIImage(cgImage: cgImage)
#else
    return NSImage(cgImage: cgImage, size: NSSize(width: width, height: height))
#endif
}

/// Decoded pages: the mapping and the platform's image over it, made off the
/// main thread and kept once.
///
/// A page is a few megabytes at a phone's scale, and the window the reader can
/// see is 4–6 bands: keeping the last few decodes makes scrolling back over
/// them a lookup instead of a decode, and keeps little enough mapped at once.
private final class PageCache {
    static let shared = PageCache()

    /// One decode at a time, in the order they were asked for: the reader asks
    /// nearest-first, and a fling must not put every core on bands nobody sees.
    private let queue = DispatchQueue(label: "quire.pagecache", qos: .userInitiated)
    /// Main thread only, like the views they are put into.
    private var pages: [String: PageImage] = [:]
    private var order: [String] = []
    private let limit = 8

    func image(_ path: String, then done: @escaping (PageImage?) -> Void) {
        if let hit = pages[path] {
            order.removeAll { $0 == path }
            order.append(path)
            done(hit)
            return
        }
        queue.async {
            let image = loadPage(path)
            DispatchQueue.main.async {
                if let image { self.remember(path, image) }
                done(image)
            }
        }
    }

    private func remember(_ path: String, _ image: PageImage) {
        pages[path] = image
        order.append(path)
        while order.count > limit {
            pages.removeValue(forKey: order.removeFirst())
        }
    }
}
