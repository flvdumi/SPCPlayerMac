import Cocoa

class AppDelegate: NSObject, NSApplicationDelegate {
    var mainWindowController: MainWindowController?

    func applicationDidFinishLaunching(_ aNotification: Notification) {
        let controller = MainWindowController()
        self.mainWindowController = controller
        
        controller.window?.center()
        controller.window?.makeKeyAndOrderFront(nil)
        controller.showWindow(nil)
        
        NSApp.activate(ignoringOtherApps: true)
    }

    // Called when a user double-clicks an .spc file in Finder or drags it onto the Dock icon
        func application(_ sender: NSApplication, openFile filename: String) -> Bool {
            let url = URL(fileURLWithPath: filename)
            if url.pathExtension.lowercased() == "spc" {
                mainWindowController?.playTrackFromURL(url)
                return true
            }
            return false
        }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
}
