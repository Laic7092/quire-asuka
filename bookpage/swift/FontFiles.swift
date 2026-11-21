// Where a font's file is, by family name: CoreText knows, and on iOS most
// font paths aren't the ones a reader would guess.
import Asuka
import CoreText
import Foundation

final class SwiftFontFiles: FontFiles {
    func path(family: String) throws -> String {
        let attributes = [kCTFontFamilyNameAttribute: family] as CFDictionary
        let descriptor = CTFontDescriptorCreateWithAttributes(attributes)
        let font = CTFontCreateWithFontDescriptor(descriptor, 12, nil)
        guard let url = CTFontCopyAttribute(font, kCTFontURLAttribute) as? URL else {
            return ""
        }
        // Some system fonts live in the dyld shared cache: the URL is real,
        // the file isn't.
        guard FileManager.default.fileExists(atPath: url.path) else { return "" }
        return url.path
    }
}

func makeFontFiles() throws -> any FontFiles { SwiftFontFiles() }
