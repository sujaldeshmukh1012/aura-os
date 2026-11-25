// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "NexusMenuBar",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "NexusMenuBar", targets: ["NexusMenuBar"])],
    targets: [.executableTarget(name: "NexusMenuBar")]
)
