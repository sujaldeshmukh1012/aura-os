import AppKit

final class NexusAppDelegate: NSObject, NSApplicationDelegate {
    private var statusItem: NSStatusItem!

    func applicationDidFinishLaunching(_ notification: Notification) {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem.button?.title = "◆ NEXUS"
        statusItem.button?.toolTip = "NEXUS Personal Intelligence Dock"

        let menu = NSMenu()
        let status = NSMenuItem(title: "Host core runs as a local service", action: nil, keyEquivalent: "")
        status.isEnabled = false
        menu.addItem(status)
        menu.addItem(.separator())
        menu.addItem(
            withTitle: "Open Device Simulator",
            action: #selector(openSimulator),
            keyEquivalent: "o"
        ).target = self
        menu.addItem(
            withTitle: "Open Project Folder",
            action: #selector(openProject),
            keyEquivalent: "p"
        ).target = self
        menu.addItem(.separator())
        menu.addItem(withTitle: "Quit NEXUS", action: #selector(quit), keyEquivalent: "q").target = self
        statusItem.menu = menu
    }

    @objc private func openSimulator() {
        if let url = URL(string: "http://127.0.0.1:8080") {
            NSWorkspace.shared.open(url)
        }
    }

    @objc private func openProject() {
        let path = ProcessInfo.processInfo.environment["NEXUS_PROJECT_DIR"] ?? FileManager.default.currentDirectoryPath
        NSWorkspace.shared.open(URL(fileURLWithPath: path, isDirectory: true))
    }

    @objc private func quit() {
        Task { @MainActor in
            NSApplication.shared.terminate(nil)
        }
    }
}

let application = NSApplication.shared
let delegate = NexusAppDelegate()
application.delegate = delegate
application.setActivationPolicy(.accessory)
application.run()
