// swift-tools-version:5.9
// The bookpage package's Swift, for iOS and macOS: the Asuka CLI builds it
// with the apps that import the package. Its dependencies (Swift
// packages) go here, as any package's.
import PackageDescription

let package = Package(
    name: "BookpageNative",
    platforms: [.iOS("16.0"), .macOS("11.0")],
    products: [.library(name: "BookpageNative", targets: ["BookpageNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "BookpageNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
