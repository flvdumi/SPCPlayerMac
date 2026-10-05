import Cocoa

final class ChannelButton: NSButton {
    var channelIndex: Int = 0
    var onToggle: ((Int) -> Void)?
    var onSolo: ((Int) -> Void)?

    override func rightMouseDown(with event: NSEvent) {
        onSolo?(channelIndex)
    }

    override func sendAction(_ action: Selector?, to target: Any?) -> Bool {
        onToggle?(channelIndex)
        return true
    }
}
