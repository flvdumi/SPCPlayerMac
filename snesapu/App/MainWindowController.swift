import Cocoa

final class MainWindowController: NSWindowController, NSWindowDelegate, NSTableViewDelegate, NSTableViewDataSource, SpcEngineDelegate {
    private let engine = SpcEngine()
    private var playlistURLs: [URL] = []
    private var timer: Timer?
    private var keyMonitor: Any?
    private var appNapActivity: NSObjectProtocol?

    private var visualizer: VisualizerView!
    private var playlistTable: NSTableView!
    private var playButton: NSButton!
    private var voiceButtons: [ChannelButton] = []

    // Submenu references
    private var rateSubmenu: NSMenu?
    private var bitSubmenu: NSMenu?
    private var channelSubmenu: NSMenu?
    private var pitchBaseSubmenu: NSMenu?
    private var keyShiftSubmenu: NSMenu?
    private var stereoSubmenu: NSMenu?
    private var feedbackSubmenu: NSMenu?
    private var speedSubmenu: NSMenu?
    private var volumeSubmenu: NSMenu?
    private var muteSubmenu: NSMenu?
    private var noiseSubmenu: NSMenu?
    private var flagsSubmenu: NSMenu?
    private var interSubmenu: NSMenu?
    private var playTimeSubmenu: NSMenu?
    private var playOrderSubmenu: NSMenu?
    private var seekTimeSubmenu: NSMenu?
    private var viewModeSubmenu: NSMenu?
    private var topmostItem: NSMenuItem?

    convenience init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 521 * 2, height: 152 * 2),
            styleMask: [.titled, .closable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        window.isReleasedWhenClosed = false
        self.init(window: window)

        window.delegate = self
        window.title = "SNES SPC700 Player"
        window.backgroundColor = NSColor(hex: "#1C1C1C")

        engine.delegate = self
        setupUI()
        setupMenu()
        setupTimer()
        setupKeyboardMonitor()

        window.registerForDraggedTypes([.fileURL])
    }

    deinit {
        if let monitor = keyMonitor { NSEvent.removeMonitor(monitor) }
        if let activity = appNapActivity { ProcessInfo.processInfo.endActivity(activity) }
        timer?.invalidate()
    }
    func playTrackFromURL(_ url: URL) {
            if !playlistURLs.contains(url) {
                playlistURLs.append(url)
                playlistTable.reloadData()
            }
            if let idx = playlistURLs.firstIndex(of: url) {
                playTrack(index: idx)
            }
        }
    private func setupUI() {
        guard let contentView = window?.contentView else { return }
        contentView.wantsLayer = true
        contentView.layer?.backgroundColor = NSColor(hex: "#1C1C1C").cgColor
        let s: CGFloat = 2.0

        // Visualizer
        visualizer = VisualizerView(frame: NSRect(x: 5 * s, y: contentView.bounds.height - (98 * s), width: 287 * s, height: 96 * s))
        visualizer.engine = engine
        contentView.addSubview(visualizer)

        // Transport Buttons
        _ = createButton(title: "OPEN", x: 5, y: 103, w: 55, h: 21, action: #selector(openFileDialog))
        _ = createButton(title: "SAVE", x: 62, y: 103, w: 55, h: 21, action: #selector(saveWavDialog))
        playButton = createButton(title: "PLAY", x: 126, y: 103, w: 54, h: 21, action: #selector(togglePlayPause))
        _ = createButton(title: "RESTART", x: 182, y: 103, w: 54, h: 21, action: #selector(restartPlayback))
        _ = createButton(title: "STOP", x: 238, y: 103, w: 54, h: 21, action: #selector(stopPlayback))

        // Channel Buttons 1..8
        for i in 0..<8 {
            let btn = ChannelButton(frame: NSRect(x: CGFloat(5 + i * 14) * s,
                                                  y: contentView.bounds.height - CGFloat(127 + 21) * s,
                                                  width: 14 * s, height: 21 * s))
            btn.title = "\(i + 1)"
            btn.bezelStyle = .regularSquare
            btn.setButtonType(.pushOnPushOff)
            btn.state = .on
            btn.channelIndex = i
            btn.onToggle = { [weak self] idx in self?.toggleChannel(idx) }
            btn.onSolo = { [weak self] idx in self?.soloChannel(idx) }
            contentView.addSubview(btn)
            voiceButtons.append(btn)
        }

        // Steppers & Seek Buttons
        _ = createButton(title: "VL-", x: 126, y: 127, w: 26, h: 21) { [weak self] in self?.nudgeVolume(up: false) }
        _ = createButton(title: "VL+", x: 154, y: 127, w: 26, h: 21) { [weak self] in self?.nudgeVolume(up: true) }
        _ = createButton(title: "SP-", x: 182, y: 127, w: 26, h: 21) { [weak self] in self?.nudgeSpeed(up: false) }
        _ = createButton(title: "SP+", x: 210, y: 127, w: 26, h: 21) { [weak self] in self?.nudgeSpeed(up: true) }
        _ = createButton(title: "REW", x: 238, y: 127, w: 26, h: 21) { [weak self] in self?.seekStep(forward: false) }
        _ = createButton(title: "FF",  x: 266, y: 127, w: 26, h: 21) { [weak self] in self?.seekStep(forward: true) }

        // Playlist View
        let scroll = NSScrollView(frame: NSRect(x: 301 * s, y: contentView.bounds.height - (124 * s), width: 215 * s, height: 122 * s))
        playlistTable = NSTableView(frame: scroll.bounds)
        let col = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(rawValue: "SPCFile"))
        col.title = "SPC Playlist"
        playlistTable.addTableColumn(col)
        playlistTable.delegate = self
        playlistTable.dataSource = self
        playlistTable.doubleAction = #selector(onPlaylistDoubleClick)
        playlistTable.backgroundColor = NSColor(hex: "#111111")
        scroll.documentView = playlistTable
        contentView.addSubview(scroll)

        _ = createButton(title: "APPEND", x: 301, y: 127, w: 54, h: 21, action: #selector(openFileDialog))
        _ = createButton(title: "REMOVE", x: 357, y: 127, w: 54, h: 21, action: #selector(removeSelected))
        _ = createButton(title: "CLEAR",  x: 413, y: 127, w: 54, h: 21, action: #selector(clearPlaylist))
        _ = createButton(title: "▲", x: 472, y: 127, w: 21, h: 21) { [weak self] in self?.movePlaylistItem(direction: -1) }
        _ = createButton(title: "▼", x: 495, y: 127, w: 21, h: 21) { [weak self] in self?.movePlaylistItem(direction: 1) }
    }

    private func createButton(title: String, x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat, action: Selector? = nil) -> NSButton {
        let s: CGFloat = 2.0
        guard let contentView = window?.contentView else { return NSButton() }
        let btn = NSButton(frame: NSRect(x: x * s, y: contentView.bounds.height - (y + h) * s, width: w * s, height: h * s))
        btn.title = title
        btn.bezelStyle = .regularSquare
        if let action = action {
            btn.target = self
            btn.action = action
        }
        contentView.addSubview(btn)
        return btn
    }

    private func createButton(title: String, x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat, handler: @escaping () -> Void) -> NSButton {
        let btn = createButton(title: title, x: x, y: y, w: w, h: h)
        btn.target = self
        btn.action = #selector(buttonActionWrapper(_:))
        objc_setAssociatedObject(btn, "btnHandlerClosure", handler, .OBJC_ASSOCIATION_COPY_NONATOMIC)
        return btn
    }

    @objc private func buttonActionWrapper(_ sender: NSButton) {
        if let block = objc_getAssociatedObject(sender, "btnHandlerClosure") as? () -> Void {
            block()
        }
    }

    func spcEngineDidFinishTrack(_ engine: SpcEngine) {
        playNextTrack()
    }

    // MARK: - Transport Actions
    @objc private func togglePlayPause() {
        if !engine.isPlaying {
            if let cur = engine.currentSpcURL { _ = engine.loadSPC(url: cur) }
            else if !playlistURLs.isEmpty { playTrack(index: 0) }
            playButton.title = "PAUSE"
        } else if engine.isPaused {
            engine.resume()
            playButton.title = "PAUSE"
        } else {
            engine.pause()
            playButton.title = "PLAY"
        }
    }

    @objc private func restartPlayback() {
        if let url = engine.currentSpcURL {
            _ = engine.loadSPC(url: url)
            playButton.title = "PAUSE"
        }
    }

    @objc private func stopPlayback() {
        engine.stop()
        playButton.title = "PLAY"
    }

    private func toggleChannel(_ idx: Int) {
        engine.channelMuteMask ^= (1 << idx)
        engine.applyAllParameters()
        updateChannelButtons()
        updateMenuCheckmarks()
    }

    private func soloChannel(_ idx: Int) {
        if engine.channelMuteMask == (0xFF & ~(1 << idx)) {
            engine.channelMuteMask = 0x00
        } else {
            engine.channelMuteMask = 0xFF & ~(1 << idx)
        }
        engine.applyAllParameters()
        updateChannelButtons()
        updateMenuCheckmarks()
    }

    private func updateChannelButtons() {
        for (i, btn) in voiceButtons.enumerated() {
            let active = (engine.channelMuteMask & (1 << i)) == 0
            btn.state = active ? .on : .off
        }
    }

    private func nudgeVolume(up: Bool) {
        engine.stepVolume(up: up)
        flashTitle("Volume \(Int(engine.currentAmp * 100))%")
        updateMenuCheckmarks()
    }

    private func nudgeSpeed(up: Bool) {
        engine.stepSpeed(up: up)
        flashTitle("Speed \(Int(engine.currentSpeed * 100))%")
        updateMenuCheckmarks()
    }

    private func seekStep(forward: Bool) {
        var delta = engine.seekTimeStep
        if engine.isSeekAsync { delta *= Double(engine.currentSpeed) }
        seekRelative(forward ? delta : -delta)
        flashTitle("Seek \(forward ? "+" : "-")\(Int(delta))s")
    }

    private func seekRelative(_ delta: Double) {
        let state = engine.getVisualState()
        let cur = Double(state.t64_ticks) / 64000.0
        engine.seek(to: cur + delta)
    }

    private func flashTitle(_ message: String) {
        window?.title = "[[ \(message) ]] - SNES SPC700 Player"
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) { [weak self] in
            guard let self = self else { return }
            if let spc = self.engine.currentSpcURL?.lastPathComponent {
                self.window?.title = "\(spc) - SNES SPC700 Player"
            } else {
                self.window?.title = "SNES SPC700 Player"
            }
        }
    }

    func playTrack(index: Int) {
        guard index >= 0 && index < playlistURLs.count else { return }
        playlistTable.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        playlistTable.scrollRowToVisible(index)
        _ = engine.loadSPC(url: playlistURLs[index])
        playButton.title = "PAUSE"
        window?.title = "\(playlistURLs[index].lastPathComponent) - SNES SPC700 Player"
    }

    func playNextTrack() {
        guard !playlistURLs.isEmpty else { return }
        let current = playlistTable.selectedRow
        switch engine.playOrder {
        case .stop: stopPlayback()
        case .repeatOne: restartPlayback()
        case .next:
            let next = (current + 1 < playlistURLs.count) ? current + 1 : 0
            playTrack(index: next)
        case .previous:
            let prev = (current - 1 >= 0) ? current - 1 : playlistURLs.count - 1
            playTrack(index: prev)
        case .random, .shuffle:
            let rand = Int.random(in: 0..<playlistURLs.count)
            playTrack(index: rand)
        }
    }

    func playPreviousTrack() {
        guard !playlistURLs.isEmpty else { return }
        let current = playlistTable.selectedRow
        let prev = (current - 1 >= 0) ? current - 1 : playlistURLs.count - 1
        playTrack(index: prev)
    }

    @objc private func openFileDialog() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = []
        if panel.runModal() == .OK {
            for url in panel.urls where url.pathExtension.lowercased() == "spc" {
                playlistURLs.append(url)
            }
            playlistTable.reloadData()
            if !engine.isPlaying, let first = playlistURLs.first {
                playTrack(index: 0)
            }
        }
    }

    @objc private func saveWavDialog() {
        guard engine.currentSpcURL != nil else { return }
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "output.wav"
        if panel.runModal() == .OK, let url = panel.url {
            engine.exportWav(to: url)
        }
    }

    @objc private func onPlaylistDoubleClick() { playTrack(index: playlistTable.selectedRow) }

    @objc private func removeSelected() {
        let row = playlistTable.selectedRow
        if row >= 0 && row < playlistURLs.count {
            playlistURLs.remove(at: row)
            playlistTable.reloadData()
        }
    }

    @objc private func clearPlaylist() {
        engine.stop()
        playButton.title = "PLAY"
        playlistURLs.removeAll()
        playlistTable.reloadData()
    }

    private func movePlaylistItem(direction: Int) {
        let row = playlistTable.selectedRow
        guard row >= 0 else { return }
        let target = row + direction
        guard target >= 0 && target < playlistURLs.count else { return }
        let item = playlistURLs.remove(at: row)
        playlistURLs.insert(item, at: target)
        playlistTable.reloadData()
        playlistTable.selectRowIndexes(IndexSet(integer: target), byExtendingSelection: false)
    }

    private func setupTimer() {
        appNapActivity = ProcessInfo.processInfo.beginActivity(
            options: [.userInitiated, .latencyCritical],
            reason: "SNES SPC700 Real-Time Audio & Telemetry"
        )

        let t = Timer(timeInterval: 0.016, repeats: true) { [weak self] _ in
            guard let self = self else { return }

            if self.engine.isPlaying && !self.engine.isPaused {
                let state = self.engine.getVisualState()
                let elapsedSec = Double(state.t64_ticks) / 64000.0

                if self.engine.isTimeRepeatActive && elapsedSec >= self.engine.repeatLimitTime {
                    self.engine.seek(to: self.engine.repeatStartTime)
                } else if self.engine.playTimeMode != .endless {
                    let totalAllowed = (self.engine.playTimeMode == .defaultTime ? self.engine.defaultDuration : self.engine.songDuration) + self.engine.fadeDuration + self.engine.waitDuration
                    if elapsedSec >= totalAllowed {
                        self.playNextTrack()
                    }
                }
            }
            self.visualizer.needsDisplay = true
        }

        RunLoop.main.add(t, forMode: .common)
        self.timer = t
    }

    func numberOfRows(in tableView: NSTableView) -> Int { playlistURLs.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        var cell = tableView.makeView(withIdentifier: NSUserInterfaceItemIdentifier(rawValue: "SPC"), owner: self) as? NSTextField
        if cell == nil {
            cell = NSTextField(labelWithString: "")
            cell?.identifier = NSUserInterfaceItemIdentifier(rawValue: "SPC")
            cell?.textColor = NSColor(hex: "#00FF66")
            cell?.font = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
        }
        cell?.stringValue = playlistURLs[row].lastPathComponent
        return cell
    }

    private func setupKeyboardMonitor() {
            keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
                guard let self = self else { return event }
                let chars = event.charactersIgnoringModifiers ?? ""
                let isShift = event.modifierFlags.contains(.shift)
                let isCtrl = event.modifierFlags.contains(.control)

                if chars == "[" {
                    self.engine.pitchKeyShift = max(-6, self.engine.pitchKeyShift - 1)
                    self.engine.applyAllParameters()
                    self.flashTitle("Key Shift: \(self.engine.pitchKeyShift)")
                    self.updateMenuCheckmarks()
                    return nil
                } else if chars == "]" {
                    self.engine.pitchKeyShift = min(6, self.engine.pitchKeyShift + 1)
                    self.engine.applyAllParameters()
                    self.flashTitle("Key Shift: \(self.engine.pitchKeyShift)")
                    self.updateMenuCheckmarks()
                    return nil
                }

                switch event.keyCode {
                case 49: self.togglePlayPause(); return nil
                case 18...21, 23, 22, 26, 28:
                    let idx = (event.keyCode <= 23) ? Int(event.keyCode - 18) : (event.keyCode == 22 ? 5 : (event.keyCode == 26 ? 6 : 7))
                    if isShift { self.soloChannel(idx) } else { self.toggleChannel(idx) }
                    return nil
                case 126: // Up Arrow = Volume Up
                    self.nudgeVolume(up: true); return nil
                case 125: // Down Arrow = Volume Down
                    self.nudgeVolume(up: false); return nil
                case 123: // Left Arrow = Seek Backward (or Speed with Ctrl)
                    if isCtrl { self.nudgeSpeed(up: false) } else { self.seekStep(forward: false) }
                    return nil
                case 124: // Right Arrow = Seek Forward (or Speed with Ctrl)
                    if isCtrl { self.nudgeSpeed(up: true) } else { self.seekStep(forward: true) }
                    return nil
                case 9:
                    let nextMode = (self.visualizer.viewMode.rawValue + 1) % VisualizerView.ViewMode.allCases.count
                    self.visualizer.viewMode = VisualizerView.ViewMode(rawValue: nextMode)!
                    self.updateMenuCheckmarks()
                    return nil
                case 11:
                    self.visualizer.isDetailDecMode.toggle()
                    return nil
                default:
                    if chars.lowercased() == "r" { self.restartPlayback(); return nil }
                    else if chars.lowercased() == "s" { self.stopPlayback(); return nil }
                }
                return event
            }
        }

    private func setupMenu() {
        let mainMenu = NSMenu()

        // 1. File Menu
        let fileMenuItem = NSMenuItem(title: "File", action: nil, keyEquivalent: "")
        let fileMenu = NSMenu(title: "File")
        fileMenu.addItem(withTitle: "Open SPC File...", action: #selector(openFileDialog), keyEquivalent: "o")
        fileMenu.addItem(withTitle: "Export WAV...", action: #selector(saveWavDialog), keyEquivalent: "s")
        fileMenu.addItem(.separator())
        fileMenu.addItem(withTitle: "Play", action: #selector(togglePlayPause), keyEquivalent: "x")
        fileMenu.addItem(withTitle: "Pause", action: #selector(togglePlayPause), keyEquivalent: "c")
        fileMenu.addItem(withTitle: "Restart", action: #selector(restartPlayback), keyEquivalent: "r")
        fileMenu.addItem(withTitle: "Stop", action: #selector(stopPlayback), keyEquivalent: "t")
        fileMenuItem.submenu = fileMenu
        mainMenu.addItem(fileMenuItem)

        // 2. Settings Menu
        let settingsMenuItem = NSMenuItem(title: "Settings", action: nil, keyEquivalent: "")
        let settingsMenu = NSMenu(title: "Settings")

        // Channels
        let chSub = NSMenu(title: "Channels")
        for (lbl, mono) in [("1 Channel (Monaural)", true), ("2 Channels (Stereo)", false)] {
            let item = NSMenuItem(title: lbl, action: #selector(channelMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = mono ? 1 : 2
            chSub.addItem(item)
        }
        self.channelSubmenu = chSub
        let chItem = NSMenuItem(title: "Channels", action: nil, keyEquivalent: "")
        chItem.submenu = chSub
        settingsMenu.addItem(chItem)

        // Bit Depth
        let bitSub = NSMenu(title: "Bit")
        for (lbl, bit) in [("8-Bit", 8), ("16-Bit [Normal]", 16), ("24-Bit", 24), ("32-Bit (int)", 32), ("32-Bit (float) [HQ]", -4)] {
            let item = NSMenuItem(title: lbl, action: #selector(bitMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = bit
            bitSub.addItem(item)
        }
        self.bitSubmenu = bitSub
        let bitItem = NSMenuItem(title: "Bit", action: nil, keyEquivalent: "")
        bitItem.submenu = bitSub
        settingsMenu.addItem(bitItem)

        // Sampling Rate
        let rateSub = NSMenu(title: "Sampling Rate")
        let rateList: [(String, Int)] = [
            ("8,000 Hz", 8000), ("10,000 Hz", 10000), ("11,025 Hz", 11025), ("12,000 Hz", 12000),
            ("16,000 Hz", 16000), ("20,000 Hz", 20000), ("22,050 Hz", 22050), ("24,000 Hz", 24000),
            ("32,000 Hz [Normal]", 32000), ("40,000 Hz", 40000), ("44,100 Hz [CD]", 44100),
            ("48,000 Hz [DVD]", 48000), ("64,000 Hz", 64000), ("80,000 Hz", 80000),
            ("88,200 Hz", 88200), ("96,000 Hz", 96000)
        ]
        for (lbl, hz) in rateList {
            let item = NSMenuItem(title: lbl, action: #selector(rateMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = hz
            rateSub.addItem(item)
        }
        self.rateSubmenu = rateSub
        let rateItem = NSMenuItem(title: "Sampling Rate", action: nil, keyEquivalent: "")
        rateItem.submenu = rateSub
        settingsMenu.addItem(rateItem)

        // Interpolation Mode
        let interSub = NSMenu(title: "Interpolation")
        for (lbl, mode) in [("None", SpcEngine.Interpolation.none),
                            ("Linear", .linear),
                            ("Cubic", .cubic),
                            ("Gaussian [SNES]", .gauss)] {
            let item = NSMenuItem(title: lbl, action: #selector(interpolationMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = Int(mode.rawValue)
            interSub.addItem(item)
        }
        self.interSubmenu = interSub
        let interItem = NSMenuItem(title: "Interpolation", action: nil, keyEquivalent: "")
        interItem.submenu = interSub
        settingsMenu.addItem(interItem)

        settingsMenu.addItem(.separator())

        // Pitch Base Clock
        let pitchSub = NSMenu(title: "Pitch Base Clock")
        for (lbl, hz) in [("Normal (32,000 Hz)", 32000), ("Old Sound Blaster (32,458 Hz)", 32458), ("Old ZSNES (32,768 Hz)", 32768)] {
            let item = NSMenuItem(title: lbl, action: #selector(pitchBaseMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = hz
            pitchSub.addItem(item)
        }
        self.pitchBaseSubmenu = pitchSub
        let pitchItem = NSMenuItem(title: "Pitch Base Clock", action: nil, keyEquivalent: "")
        pitchItem.submenu = pitchSub
        settingsMenu.addItem(pitchItem)

        // Key Shift
        let keySub = NSMenu(title: "Key Shift")
        for st in (-6...6).reversed() {
            let sign = st > 0 ? "+\(st)" : "\(st)"
            let text = st == 0 ? "0 (Original Pitch)" : "\(sign) Semitones"
            let item = NSMenuItem(title: text, action: #selector(keyShiftMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = st
            keySub.addItem(item)
        }
        self.keyShiftSubmenu = keySub
        let keyItem = NSMenuItem(title: "Key Shift (Semitones)", action: nil, keyEquivalent: "")
        keyItem.submenu = keySub
        settingsMenu.addItem(keyItem)

        // Stereo Separation
        let sepSub = NSMenu(title: "Stereo Separation")
        for pct in [0, 10, 20, 25, 30, 33, 40, 50, 60, 67, 70, 75, 80, 90, 100] {
            let val = Int(Double(pct) / 100.0 * 65536.0)
            let item = NSMenuItem(title: "\(pct)%", action: #selector(stereoSepMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = val
            sepSub.addItem(item)
        }
        self.stereoSubmenu = sepSub
        let sepItem = NSMenuItem(title: "Stereo Separation", action: nil, keyEquivalent: "")
        sepItem.submenu = sepSub
        settingsMenu.addItem(sepItem)

        // Feedback Mixer
        let fbSub = NSMenu(title: "Feedback Mixer")
        for pct in [0, 10, 20, 25, 30, 33, 40, 50, 60, 67, 70, 75, 80, 90, 100] {
            let val = Int(Double(pct) / 100.0 * 65536.0)
            let item = NSMenuItem(title: "\(pct)%", action: #selector(feedbackMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = val
            fbSub.addItem(item)
        }
        self.feedbackSubmenu = fbSub
        let fbItem = NSMenuItem(title: "Feedback Mixer", action: nil, keyEquivalent: "")
        fbItem.submenu = fbSub
        settingsMenu.addItem(fbItem)

        // Playback Speed
        let spdSub = NSMenu(title: "Playback Speed")
        for spd in SpcEngine.speedPresets {
            let pct = Int(spd * 100)
            let item = NSMenuItem(title: "\(pct)%", action: #selector(speedMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = pct
            spdSub.addItem(item)
        }
        self.speedSubmenu = spdSub
        let spdItem = NSMenuItem(title: "Playback Speed", action: nil, keyEquivalent: "")
        spdItem.submenu = spdSub
        settingsMenu.addItem(spdItem)

        // Volume
        let volSub = NSMenu(title: "Volume")
        for amp in SpcEngine.ampPresets {
            let pct = Int(amp * 100)
            let item = NSMenuItem(title: "\(pct)%", action: #selector(volumeMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = pct
            volSub.addItem(item)
        }
        self.volumeSubmenu = volSub
        let volItem = NSMenuItem(title: "Volume", action: nil, keyEquivalent: "")
        volItem.submenu = volSub
        settingsMenu.addItem(volItem)

        settingsMenu.addItem(.separator())

        // Channel Mutes
        let muteSub = NSMenu(title: "Channel Mute")
        let unmuteAll = NSMenuItem(title: "Enable All (Unmute)", action: #selector(unmuteAllChannels), keyEquivalent: "")
        unmuteAll.target = self
        muteSub.addItem(unmuteAll)
        let muteAll = NSMenuItem(title: "Disable All (Mute All)", action: #selector(muteAllChannels), keyEquivalent: "")
        muteAll.target = self
        muteSub.addItem(muteAll)
        let invMute = NSMenuItem(title: "Invert All", action: #selector(invertMuteChannels), keyEquivalent: "")
        invMute.target = self
        muteSub.addItem(invMute)
        muteSub.addItem(.separator())
        for i in 0..<8 {
            let item = NSMenuItem(title: "Channel \(i + 1)", action: #selector(toggleChannelMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = i
            muteSub.addItem(item)
        }
        self.muteSubmenu = muteSub
        let muteItem = NSMenuItem(title: "Channel Mute", action: nil, keyEquivalent: "")
        muteItem.submenu = muteSub
        settingsMenu.addItem(muteItem)

        // Channel Noise
        let noiseSub = NSMenu(title: "Channel Noise")
        let noiseAll = NSMenuItem(title: "Enable All Noise", action: #selector(enableAllNoise), keyEquivalent: "")
        noiseAll.target = self
        noiseSub.addItem(noiseAll)
        let clearNoise = NSMenuItem(title: "Disable All Noise", action: #selector(disableAllNoise), keyEquivalent: "")
        clearNoise.target = self
        noiseSub.addItem(clearNoise)
        noiseSub.addItem(.separator())
        for i in 0..<8 {
            let item = NSMenuItem(title: "Channel \(i + 1) Noise", action: #selector(toggleNoiseMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = i
            noiseSub.addItem(item)
        }
        self.noiseSubmenu = noiseSub
        let noiseItem = NSMenuItem(title: "Channel Noise", action: nil, keyEquivalent: "")
        noiseItem.submenu = noiseSub
        settingsMenu.addItem(noiseItem)

        // Expansion Flags
        let flagsSub = NSMenu(title: "Expansion Flags")
        let optList: [(String, UInt32)] = [
            ("SNES Low-Pass Filter", 0x01),
            ("Old ADPCM Decoder", 0x02),
            ("Reverse Stereo", 0x08),
            ("Disable Echo", 0x10),
            ("Disable Pitch Modulation", 0x20),
            ("Disable Pitch Bend", 0x40),
            ("Disable FIR Filter", 0x80),
            ("Bass Boost", 0x100),
            ("Disable Envelope", 0x200),
            ("Disable Noise Generator", 0x400),
            ("SNES Echo/FIR Method", 0x800)
        ]
        for (name, flag) in optList {
            let mItem = NSMenuItem(title: name, action: #selector(toggleFlagAction(_:)), keyEquivalent: "")
            mItem.target = self
            mItem.tag = Int(flag)
            flagsSub.addItem(mItem)
        }
        self.flagsSubmenu = flagsSub
        let flagsItem = NSMenuItem(title: "Expansion Flags", action: nil, keyEquivalent: "")
        flagsItem.submenu = flagsSub
        settingsMenu.addItem(flagsItem)

        // Play Time Mode
        let ptSub = NSMenu(title: "Play Time")
        for (lbl, mode) in [("Disable / Endless", SpcEngine.PlayTimeMode.endless),
                            ("Enable ID666 Time", .id666Priority),
                            ("Always Default Time", .defaultTime)] {
            let item = NSMenuItem(title: lbl, action: #selector(playTimeModeSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = mode.rawValue
            ptSub.addItem(item)
        }
        ptSub.addItem(.separator())
        let startMark = NSMenuItem(title: "Set Start Position Mark", action: #selector(setStartMarkMenu), keyEquivalent: ",")
        startMark.target = self
        ptSub.addItem(startMark)
        let endMark = NSMenuItem(title: "Set Limit Position Mark", action: #selector(setEndMarkMenu), keyEquivalent: ".")
        endMark.target = self
        ptSub.addItem(endMark)
        let resetMarks = NSMenuItem(title: "Reset Position Marks", action: #selector(resetMarksMenu), keyEquivalent: "/")
        resetMarks.target = self
        ptSub.addItem(resetMarks)
        self.playTimeSubmenu = ptSub
        let ptItem = NSMenuItem(title: "Play Time", action: nil, keyEquivalent: "")
        ptItem.submenu = ptSub
        settingsMenu.addItem(ptItem)

        // Play Order
        let orderSub = NSMenu(title: "Play Order")
        for (name, order) in [("Stop after track", SpcEngine.PlayOrder.stop),
                              ("Next track", .next),
                              ("Previous track", .previous),
                              ("Random", .random),
                              ("Shuffle", .shuffle),
                              ("Repeat track", .repeatOne)] {
            let item = NSMenuItem(title: name, action: #selector(playOrderSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = order.rawValue
            orderSub.addItem(item)
        }
        self.playOrderSubmenu = orderSub
        let orderItem = NSMenuItem(title: "Play Order", action: nil, keyEquivalent: "")
        orderItem.submenu = orderSub
        settingsMenu.addItem(orderItem)

        // Seek Time
        let seekSub = NSMenu(title: "Seek Time")
        for sec in [1, 2, 3, 4, 5, 10] {
            let item = NSMenuItem(title: "\(sec) s", action: #selector(seekTimeMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = sec
            seekSub.addItem(item)
        }
        seekSub.addItem(.separator())
        let fastSeek = NSMenuItem(title: "Fast Seek", action: #selector(toggleFastSeek), keyEquivalent: "")
        fastSeek.target = self
        seekSub.addItem(fastSeek)
        let asyncSeek = NSMenuItem(title: "Multiply by Speed", action: #selector(toggleAsyncSeek), keyEquivalent: "")
        asyncSeek.target = self
        seekSub.addItem(asyncSeek)
        self.seekTimeSubmenu = seekSub
        let seekItem = NSMenuItem(title: "Seek Time", action: nil, keyEquivalent: "")
        seekItem.submenu = seekSub
        settingsMenu.addItem(seekItem)

        // Information Viewer
        let viewSub = NSMenu(title: "Information Viewer")
        for (idx, name) in [
            (0, "Graphic Indicator (VU Meters)"),
            (1, "DSP Mixer & BPM"),
            (2, "Channel 1 (Vol, Pitch, Envelope)"),
            (3, "Channel 2 (ADSR / Gain)"),
            (4, "Channel 3 (Channel Flags)"),
            (5, "Channel 4 (Source & Directory)"),
            (6, "SPC Tags 1 (Metadata)"),
            (7, "SPC Tags 2 (Registers)"),
            (8, "Script700 Debug")
        ] {
            let item = NSMenuItem(title: name, action: #selector(viewModeMenuSelected(_:)), keyEquivalent: "")
            item.target = self
            item.tag = idx
            viewSub.addItem(item)
        }
        self.viewModeSubmenu = viewSub
        let viewItem = NSMenuItem(title: "Information Viewer Mode", action: nil, keyEquivalent: "")
        viewItem.submenu = viewSub
        settingsMenu.addItem(viewItem)

        settingsMenu.addItem(.separator())

        // Always On Top
        let topItem = NSMenuItem(title: "Always on Top", action: #selector(toggleAlwaysOnTop), keyEquivalent: "")
        topItem.target = self
        self.topmostItem = topItem
        settingsMenu.addItem(topItem)

        settingsMenuItem.submenu = settingsMenu
        mainMenu.addItem(settingsMenuItem)

        // 3. Playlist Menu
        let plistMenuItem = NSMenuItem(title: "Playlist", action: nil, keyEquivalent: "")
        let plistMenu = NSMenu(title: "Playlist")
        plistMenu.addItem(withTitle: "Play Selected", action: #selector(onPlaylistDoubleClick), keyEquivalent: "\r")
        plistMenu.addItem(withTitle: "Next Track", action: #selector(nextTrackAction), keyEquivalent: "")
        plistMenu.addItem(withTitle: "Previous Track", action: #selector(prevTrackAction), keyEquivalent: "")
        plistMenu.addItem(.separator())
        plistMenu.addItem(withTitle: "Clear Playlist", action: #selector(clearPlaylist), keyEquivalent: "")
        plistMenuItem.submenu = plistMenu
        mainMenu.addItem(plistMenuItem)

        NSApp.mainMenu = mainMenu
        updateMenuCheckmarks()
    }

    // MARK: - Menu Actions
        @objc private func channelMenuSelected(_ sender: NSMenuItem) {
            engine.isMono = (sender.tag == 1)
            engine.applyAllParameters() // Seamlessly toggles mono inside C++ DSP
            updateMenuCheckmarks()
        }

  
    @objc private func bitMenuSelected(_ sender: NSMenuItem) {
        engine.targetBitDepth = sender.tag
        updateMenuCheckmarks()
    }

    @objc private func rateMenuSelected(_ sender: NSMenuItem) {
        engine.targetSampleRate = Double(sender.tag)
        flashTitle("Export Rate: \(sender.tag) Hz")
        updateMenuCheckmarks()
    }

    @objc private func interpolationMenuSelected(_ sender: NSMenuItem) {
        if let mode = SpcEngine.Interpolation(rawValue: UInt8(sender.tag)) {
            engine.interpolationMode = mode
            engine.applyAllParameters()
            updateMenuCheckmarks()
        }
    }

    @objc private func pitchBaseMenuSelected(_ sender: NSMenuItem) {
        engine.pitchBaseHz = UInt32(sender.tag)
        engine.applyAllParameters()
        flashTitle("Clock \(sender.tag) Hz")
        updateMenuCheckmarks()
    }

    @objc private func keyShiftMenuSelected(_ sender: NSMenuItem) {
        engine.pitchKeyShift = sender.tag
        engine.applyAllParameters()
        flashTitle("Key Shift: \(sender.tag)")
        updateMenuCheckmarks()
    }

    @objc private func stereoSepMenuSelected(_ sender: NSMenuItem) {
        engine.stereoSepValue = UInt32(sender.tag)
        engine.applyAllParameters()
        flashTitle("Stereo: \(Int(Double(sender.tag) / 65536.0 * 100))%")
        updateMenuCheckmarks()
    }

    @objc private func feedbackMenuSelected(_ sender: NSMenuItem) {
        engine.feedbackValue = UInt32(sender.tag)
        engine.applyAllParameters()
        flashTitle("Feedback: \(Int(Double(sender.tag) / 65536.0 * 100))%")
        updateMenuCheckmarks()
    }

    @objc private func speedMenuSelected(_ sender: NSMenuItem) {
        engine.currentSpeed = Float(sender.tag) / 100.0
        engine.applyAllParameters()
        flashTitle("Speed: \(sender.tag)%")
        updateMenuCheckmarks()
    }

    @objc private func volumeMenuSelected(_ sender: NSMenuItem) {
        engine.currentAmp = Float(sender.tag) / 100.0
        engine.applyAllParameters()
        flashTitle("Volume: \(sender.tag)%")
        updateMenuCheckmarks()
    }

    @objc private func toggleChannelMenuSelected(_ sender: NSMenuItem) { toggleChannel(sender.tag) }
    @objc private func unmuteAllChannels() { engine.channelMuteMask = 0x00; engine.applyAllParameters(); updateChannelButtons(); updateMenuCheckmarks() }
    @objc private func muteAllChannels() { engine.channelMuteMask = 0xFF; engine.applyAllParameters(); updateChannelButtons(); updateMenuCheckmarks() }
    @objc private func invertMuteChannels() { engine.channelMuteMask ^= 0xFF; engine.applyAllParameters(); updateChannelButtons(); updateMenuCheckmarks() }

    @objc private func toggleNoiseMenuSelected(_ sender: NSMenuItem) {
        engine.channelNoiseMask ^= (1 << sender.tag)
        engine.applyAllParameters()
        updateMenuCheckmarks()
    }
    @objc private func enableAllNoise() { engine.channelNoiseMask = 0xFF; engine.applyAllParameters(); updateMenuCheckmarks() }
    @objc private func disableAllNoise() { engine.channelNoiseMask = 0x00; engine.applyAllParameters(); updateMenuCheckmarks() }

    @objc private func playTimeModeSelected(_ sender: NSMenuItem) {
        if let mode = SpcEngine.PlayTimeMode(rawValue: sender.tag) {
            engine.playTimeMode = mode
            updateMenuCheckmarks()
        }
    }
    @objc private func setStartMarkMenu() {
        let state = engine.getVisualState()
        engine.setStartRepeatMark(to: Double(state.t64_ticks) / 64000.0)
    }
    @objc private func setEndMarkMenu() {
        let state = engine.getVisualState()
        engine.setLimitRepeatMark(to: Double(state.t64_ticks) / 64000.0)
    }
    @objc private func resetMarksMenu() { engine.resetRepeatMarks() }

    @objc private func playOrderSelected(_ sender: NSMenuItem) {
        if let order = SpcEngine.PlayOrder(rawValue: sender.tag) {
            engine.playOrder = order
            updateMenuCheckmarks()
        }
    }

    @objc private func seekTimeMenuSelected(_ sender: NSMenuItem) {
        engine.seekTimeStep = Double(sender.tag)
        updateMenuCheckmarks()
    }
    @objc private func toggleFastSeek() { engine.isSeekFast.toggle(); updateMenuCheckmarks() }
    @objc private func toggleAsyncSeek() { engine.isSeekAsync.toggle(); updateMenuCheckmarks() }

    @objc private func viewModeMenuSelected(_ sender: NSMenuItem) {
        if let mode = VisualizerView.ViewMode(rawValue: sender.tag) {
            visualizer.viewMode = mode
            updateMenuCheckmarks()
        }
    }

    @objc private func toggleFlagAction(_ sender: NSMenuItem) {
        let flag = UInt32(sender.tag)
        engine.dspOptions ^= flag
        engine.applyAllParameters()
        updateMenuCheckmarks()
    }

    @objc private func toggleAlwaysOnTop() {
        guard let win = window else { return }
        if win.level == .floating {
            win.level = .normal
            topmostItem?.state = .off
        } else {
            win.level = .floating
            topmostItem?.state = .on
        }
    }

    private func updateMenuCheckmarks() {
        if let items = channelSubmenu?.items {
            for item in items { item.state = ((item.tag == 1 && engine.isMono) || (item.tag == 2 && !engine.isMono)) ? .on : .off }
        }
        if let items = bitSubmenu?.items {
            for item in items { item.state = (item.tag == engine.targetBitDepth) ? .on : .off }
        }
        if let items = rateSubmenu?.items {
            for item in items { item.state = (item.tag == Int(engine.targetSampleRate)) ? .on : .off }
        }
        if let items = interSubmenu?.items {
            for item in items { item.state = (item.tag == Int(engine.interpolationMode.rawValue)) ? .on : .off }
        }
        if let items = pitchBaseSubmenu?.items {
            for item in items { item.state = (UInt32(item.tag) == engine.pitchBaseHz) ? .on : .off }
        }
        if let items = keyShiftSubmenu?.items {
            for item in items { item.state = (item.tag == engine.pitchKeyShift) ? .on : .off }
        }
        if let items = stereoSubmenu?.items {
            for item in items { item.state = (abs(Int(engine.stereoSepValue) - item.tag) < 500) ? .on : .off }
        }
        if let items = feedbackSubmenu?.items {
            for item in items { item.state = (abs(Int(engine.feedbackValue) - item.tag) < 500) ? .on : .off }
        }
        if let items = speedSubmenu?.items {
            for item in items { item.state = (item.tag == Int(round(engine.currentSpeed * 100))) ? .on : .off }
        }
        if let items = volumeSubmenu?.items {
            for item in items { item.state = (item.tag == Int(round(engine.currentAmp * 100))) ? .on : .off }
        }
        if let items = muteSubmenu?.items {
            for item in items where item.tag >= 0 && item.tag < 8 {
                item.state = (engine.channelMuteMask & (1 << item.tag)) != 0 ? .on : .off
            }
        }
        if let items = noiseSubmenu?.items {
            for item in items where item.tag >= 0 && item.tag < 8 {
                item.state = (engine.channelNoiseMask & (1 << item.tag)) != 0 ? .on : .off
            }
        }
        if let items = flagsSubmenu?.items {
            for item in items {
                let flag = UInt32(item.tag)
                item.state = (engine.dspOptions & flag) != 0 ? .on : .off
            }
        }
        if let items = playTimeSubmenu?.items {
            for item in items where item.tag >= 0 && item.tag <= 2 {
                item.state = (item.tag == engine.playTimeMode.rawValue) ? .on : .off
            }
        }
        if let items = playOrderSubmenu?.items {
            for item in items { item.state = (item.tag == engine.playOrder.rawValue) ? .on : .off }
        }
        if let items = seekTimeSubmenu?.items {
            for item in items {
                if item.tag > 0 { item.state = (item.tag == Int(engine.seekTimeStep)) ? .on : .off }
                else if item.title == "Fast Seek" { item.state = engine.isSeekFast ? .on : .off }
                else if item.title == "Multiply by Speed" { item.state = engine.isSeekAsync ? .on : .off }
            }
        }
        if let items = viewModeSubmenu?.items {
            for item in items { item.state = (item.tag == visualizer.viewMode.rawValue) ? .on : .off }
        }
    }

    @objc private func nextTrackAction() { playNextTrack() }
    @objc private func prevTrackAction() { playPreviousTrack() }
}

extension NSColor {
    convenience init(hex: String) {
        var cString = hex.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        if cString.hasPrefix("#") { cString.remove(at: cString.startIndex) }
        var rgbValue: UInt64 = 0
        Scanner(string: cString).scanHexInt64(&rgbValue)
        self.init(
            red: CGFloat((rgbValue & 0xFF0000) >> 16) / 255.0,
            green: CGFloat((rgbValue & 0x00FF00) >> 8) / 255.0,
            blue: CGFloat(rgbValue & 0x0000FF) / 255.0,
            alpha: 1.0
        )
    }
}
