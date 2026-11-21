// BookPage on iOS and macOS: a UIImageView (or NSImageView) whose image is
// the engine's EQP1 raw bitmap, mapped from the file, not copied.
//
// The file is written by ebook-engine's ABI (`eb_plain_page_raw`) at the
// screen's scale, so one bitmap pixel is one device pixel.
import Asuka
import Foundation

#if canImport(UIKit)
import UIKit
typealias PageImage = UIImage
#else
import AppKit
typealias PageImage = NSImage
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

@MainActor final class SwiftBookPage: BookPage {
    private let picture: AsukaPlatformView
    private let events: BookPageEvents
    private var image: PageImage?

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
#if canImport(UIKit)
        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        picture.addGestureRecognizer(tap)
#else
        let click = NSClickGestureRecognizer(target: self, action: #selector(handleClick(_:)))
        picture.addGestureRecognizer(click)
#endif
    }

#if canImport(UIKit)
    /// A tap, in the page's points: the reader turns the page by its third.
    @objc private func handleTap(_ recognizer: UITapGestureRecognizer) {
        guard let target = recognizer.view else { return }
        let point = recognizer.location(in: target)
        events.press(x: Double(point.x), y: Double(point.y))
    }
#else
    @objc private func handleClick(_ recognizer: NSClickGestureRecognizer) {
        guard let target = recognizer.view else { return }
        let point = recognizer.location(in: target)
        events.press(x: Double(point.x), y: Double(target.bounds.height - point.y))
    }
#endif

    func setPath(_ value: String) {
        guard let image = SwiftBookPage.load(value) else { return }
        self.image = image
#if canImport(UIKit)
        (picture as? UIImageView)?.image = image
#else
        (picture as? NSImageView)?.image = image
#endif
    }

    /// The file as an image: `EQP1`, u32 width, u32 height, then RGBA8.
    /// Straight alpha, 8 bits a channel, sRGB — what the engine writes.
    private static func load(_ path: String) -> PageImage? {
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
}

@MainActor func makeBookPage(_ events: BookPageEvents) -> any BookPage {
    SwiftBookPage(events)
}
