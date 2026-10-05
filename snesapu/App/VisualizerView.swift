import Cocoa

extension SnesApuVisualState {
    func dspReg(_ index: Int) -> UInt8 {
        withUnsafeBytes(of: dsp_regs) { $0[index & 0x7F] }
    }
    func envLevel(_ index: Int) -> UInt8 {
        withUnsafeBytes(of: env_levels) { $0[index & 0x07] }
    }
    func voiceOut(_ index: Int) -> Int16 {
        withUnsafeBytes(of: voice_outs) { $0.load(fromByteOffset: (index & 0x07) * MemoryLayout<Int16>.stride, as: Int16.self) }
    }
}

final class VisualizerView: NSView {
    enum ViewMode: Int, CaseIterable {
        case indicator = 0
        case mixer     = 1
        case channel1  = 2
        case channel2  = 3
        case channel3  = 4
        case channel4  = 5
        case tags1     = 6
        case tags2     = 7
        case script700 = 8
    }

    var viewMode: ViewMode = .indicator { didSet { needsDisplay = true } }
    var isDetailDecMode = false { didSet { needsDisplay = true } }
    weak var engine: SpcEngine?

    private var isScrubbing = false
    private var scrubRatio: Double = 0.0

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext, let engine = engine else { return }

        let scaleX = bounds.width / 287.0
        let scaleY = bounds.height / 96.0
        ctx.saveGState()
        ctx.scaleBy(x: scaleX, y: scaleY)

        ctx.setFillColor(NSColor(hex: "#1C1C1C").cgColor)
        ctx.fill(CGRect(x: 0, y: 0, width: 287, height: 96))

        let state = engine.getVisualState()

        switch viewMode {
        case .indicator: drawIndicatorView(ctx: ctx, state: state, engine: engine)
        case .mixer:     drawMixerView(ctx: ctx, state: state, engine: engine)
        case .channel1:  drawChannel1View(ctx: ctx, state: state)
        case .channel2:  drawChannel2View(ctx: ctx, state: state)
        case .channel3:  drawChannel3View(ctx: ctx, state: state)
        case .channel4:  drawChannel4View(ctx: ctx, state: state)
        case .tags1:     drawTags1View(ctx: ctx, state: state, engine: engine)
        case .tags2:     drawTags2View(ctx: ctx, state: state, engine: engine)
        case .script700: drawScript700View(ctx: ctx, state: state)
        }

        ctx.restoreGState()
    }

    private func drawIndicatorView(ctx: CGContext, state: SnesApuVisualState, engine: SpcEngine) {
        let textAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: NSColor(hex: "#ECECEC")
        ]

        String("Title : \(engine.title.prefix(22))").draw(at: CGPoint(x: 4, y: 2), withAttributes: textAttrs)
        String("Game  : \(engine.game.prefix(22))").draw(at: CGPoint(x: 4, y: 14), withAttributes: textAttrs)

        var elapsed = Double(state.t64_ticks) / 64000.0
        if isScrubbing { elapsed = scrubRatio * engine.songDuration }

        let timeStr = String(format: "Time  : %02d:%02d.%03d",
                             Int(elapsed) / 60, Int(elapsed) % 60, Int(elapsed.truncatingRemainder(dividingBy: 1.0) * 1000.0))
        timeStr.draw(at: CGPoint(x: 4, y: 26), withAttributes: textAttrs)

        let progress = isScrubbing ? scrubRatio : min(1.0, max(0.0, elapsed / engine.songDuration))
        let barW = 140.0
        ctx.setFillColor(NSColor(hex: "#333333").cgColor)
        ctx.fill(CGRect(x: 140, y: 27, width: barW, height: 5))

        ctx.setFillColor(NSColor(hex: "#00FF66").cgColor)
        ctx.fill(CGRect(x: 140, y: 27, width: barW * progress, height: 5))

        if engine.isTimeRepeatActive {
            let startX = 140.0 + (engine.repeatStartTime / engine.songDuration) * barW
            let endX = 140.0 + (engine.repeatLimitTime / engine.songDuration) * barW
            ctx.setFillColor(NSColor.cyan.cgColor)
            ctx.fill(CGRect(x: startX, y: 24, width: 2, height: 11))
            ctx.setFillColor(NSColor.orange.cgColor)
            ctx.fill(CGRect(x: endX, y: 24, width: 2, height: 11))
        }

        let knobX = min(140 + barW - 3, max(140, 140 + barW * progress - 2))
        ctx.setFillColor(NSColor.white.cgColor)
        ctx.fill(CGRect(x: knobX, y: 25, width: 4, height: 9))

        ctx.setStrokeColor(NSColor(hex: "#555555").cgColor)
        ctx.strokeLineSegments(between: [CGPoint(x: 0, y: 45), CGPoint(x: 287, y: 45)])
        ctx.strokeLineSegments(between: [CGPoint(x: 46, y: 45), CGPoint(x: 46, y: 96)])

        let mvolL = Double(abs(Int8(bitPattern: state.dspReg(0x0C)))) / 127.0
        let mvolR = Double(abs(Int8(bitPattern: state.dspReg(0x1C)))) / 127.0
        let evolL = Double(abs(Int8(bitPattern: state.dspReg(0x2C)))) / 127.0
        let evolR = Double(abs(Int8(bitPattern: state.dspReg(0x3C)))) / 127.0
        drawBar(ctx: ctx, x: 4, level: mvolL, width: 3, color: NSColor(hex: "#00AA00"))
        drawBar(ctx: ctx, x: 8, level: mvolR, width: 3, color: NSColor(hex: "#00AA00"))
        drawBar(ctx: ctx, x: 14, level: evolL, width: 3, color: NSColor(hex: "#FF8800"))
        drawBar(ctx: ctx, x: 18, level: evolR, width: 3, color: NSColor(hex: "#FF8800"))

        let pmodDisabled = (engine.dspOptions & 0x20) != 0

        for i in 0..<8 {
            let vx = 48 + (i * 30)
            ctx.setStrokeColor(NSColor(hex: "#333333").cgColor)
            ctx.strokeLineSegments(between: [CGPoint(x: vx + 28, y: 45), CGPoint(x: vx + 28, y: 96)])

            String(i + 1).draw(at: CGPoint(x: vx + 2, y: 46), withAttributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 8, weight: .regular),
                .foregroundColor: NSColor(hex: "#888888")
            ])

            let eon = (state.dspReg(0x4D) & (1 << i)) != 0
            let pmon = ((state.dspReg(0x2D) & (1 << i)) != 0) && !pmodDisabled
            let non = (state.dspReg(0x3D) & (1 << i)) != 0

            func drawFlag(text: String, x: Int, on: Bool, hex: String) {
                let flagAttrs: [NSAttributedString.Key: Any] = [
                    .font: NSFont.monospacedSystemFont(ofSize: 8, weight: .regular),
                    .foregroundColor: on ? NSColor(hex: hex) : NSColor(hex: "#333333")
                ]
                text.draw(at: CGPoint(x: vx + x, y: 46), withAttributes: flagAttrs)
            }
            drawFlag(text: "E", x: 10, on: eon, hex: "#00FFFF")
            drawFlag(text: "P", x: 16, on: pmon, hex: "#FF00FF")
            drawFlag(text: "N", x: 22, on: non, hex: "#FFFF00")

            let envNorm = Double(state.envLevel(i)) / 128.0
            let outNorm = Double(abs(state.voiceOut(i))) / 32768.0
            let vl = Double(abs(Int8(bitPattern: state.dspReg(i << 4 | 0x00)))) / 127.0
            let vr = Double(abs(Int8(bitPattern: state.dspReg(i << 4 | 0x01)))) / 127.0

            drawBar(ctx: ctx, x: vx + 1, level: vl, width: 3, color: NSColor(hex: "#00CC44"))
            drawBar(ctx: ctx, x: vx + 5, level: vr, width: 3, color: NSColor(hex: "#00CC44"))
            drawBar(ctx: ctx, x: vx + 10, level: envNorm, width: 4, color: NSColor(hex: "#CC2222"))
            drawBar(ctx: ctx, x: vx + 16, level: outNorm, width: 6, color: NSColor(hex: "#2266FF"))
            drawBar(ctx: ctx, x: vx + 23, level: outNorm, width: 4, color: NSColor(hex: "#8844FF"))
        }
    }

    private func drawMixerView(ctx: CGContext, state: SnesApuVisualState, engine: SpcEngine) {
        let headAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
            .foregroundColor: NSColor(hex: "#FFFF66")
        ]
        let textAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: NSColor(hex: "#00FFCC")
        ]

        "DSP MIXER & FILTER REGISTERS".draw(at: CGPoint(x: 4, y: 2), withAttributes: headAttrs)
        let mvolL = isDetailDecMode ? "\(abs(Int8(bitPattern: state.dspReg(0x0C))))" : String(format: "%02X", state.dspReg(0x0C))
        let mvolR = isDetailDecMode ? "\(abs(Int8(bitPattern: state.dspReg(0x1C))))" : String(format: "%02X", state.dspReg(0x1C))
        let evolL = isDetailDecMode ? "\(abs(Int8(bitPattern: state.dspReg(0x2C))))" : String(format: "%02X", state.dspReg(0x2C))
        let evolR = isDetailDecMode ? "\(abs(Int8(bitPattern: state.dspReg(0x3C))))" : String(format: "%02X", state.dspReg(0x3C))

        "MVOL L: \(mvolL)  R: \(mvolR)     EVOL L: \(evolL)  R: \(evolR)".draw(at: CGPoint(x: 4, y: 18), withAttributes: textAttrs)
        "EDL Delay : \(state.dspReg(0x7D) & 0x0F) (\(Int(state.dspReg(0x7D) & 0x0F) * 16) ms)  EFB: \(Int8(bitPattern: state.dspReg(0x0D)))%".draw(at: CGPoint(x: 4, y: 32), withAttributes: textAttrs)
        "DIR (Source Dir) : 0x\(String(format: "%02X00", state.dspReg(0x5D)))   ESA: 0x\(String(format: "%02X00", state.dspReg(0x6D)))".draw(at: CGPoint(x: 4, y: 46), withAttributes: textAttrs)

        var firStr = "FIR: "
        for i in 0..<8 {
            firStr += String(format: "%02X ", state.dspReg((i << 4) | 0x0F))
        }
        firStr.draw(at: CGPoint(x: 4, y: 60), withAttributes: textAttrs)
        "FLAGS: 0x\(String(format: "%02X", state.dspReg(0x6C)))  (Noise Freq: \(state.dspReg(0x6C) & 0x1F))".draw(at: CGPoint(x: 4, y: 74), withAttributes: textAttrs)
    }

    private func drawChannel1View(ctx: CGContext, state: SnesApuVisualState) {
        let headAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
            .foregroundColor: NSColor(hex: "#FFFF66")
        ]
        "CH | VOL_L VOL_R PITCH  ENVX OUTX".draw(at: CGPoint(x: 4, y: 2), withAttributes: headAttrs)
        ctx.setStrokeColor(NSColor(hex: "#444444").cgColor)
        ctx.strokeLineSegments(between: [CGPoint(x: 0, y: 15), CGPoint(x: 287, y: 15)])

        for i in 0..<8 {
            let vl = isDetailDecMode ? abs(Int8(bitPattern: state.dspReg(i << 4 | 0x00))) : Int8(bitPattern: state.dspReg(i << 4 | 0x00))
            let vr = isDetailDecMode ? abs(Int8(bitPattern: state.dspReg(i << 4 | 0x01))) : Int8(bitPattern: state.dspReg(i << 4 | 0x01))
            let pitch = UInt16(state.dspReg(i << 4 | 0x02)) | (UInt16(state.dspReg(i << 4 | 0x03)) << 8)
            let envx = state.envLevel(i)
            let outx = abs(state.voiceOut(i)) >> 8

            let rowStr = String(format: "%d  |  %02X    %02X   %04X   %02X   %02X", i + 1, vl, vr, pitch, envx, outx)
            let color = envx > 0 ? NSColor(hex: "#00FF66") : NSColor(hex: "#666666")
            rowStr.draw(at: CGPoint(x: 4, y: CGFloat(16 + (i * 9))), withAttributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
                .foregroundColor: color
            ])
        }
    }

    private func drawChannel2View(ctx: CGContext, state: SnesApuVisualState) {
        let headAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
            .foregroundColor: NSColor(hex: "#FFFF66")
        ]
        "CH | ADSR1 ADSR2 GAIN   TYPE   ATTACK DECAY SUST".draw(at: CGPoint(x: 4, y: 2), withAttributes: headAttrs)

        for i in 0..<8 {
            let a1 = state.dspReg(i << 4 | 0x05)
            let a2 = state.dspReg(i << 4 | 0x06)
            let gn = state.dspReg(i << 4 | 0x07)
            let isADSR = (a1 & 0x80) != 0
            let typeStr = isADSR ? "ADSR" : "GAIN"
            let ar = a1 & 0x0F
            let dr = (a1 >> 4) & 0x07
            let sl = (a2 >> 5) & 0x07

            let rowStr = String(format: "%d  |  %02X    %02X    %02X    %@   %02d     %02d    %02d", i + 1, a1, a2, gn, typeStr, ar, dr, sl)
            rowStr.draw(at: CGPoint(x: 4, y: CGFloat(16 + (i * 9))), withAttributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 8, weight: .regular),
                .foregroundColor: isADSR ? NSColor(hex: "#00FFCC") : NSColor(hex: "#FF8800")
            ])
        }
    }

    private func drawChannel3View(ctx: CGContext, state: SnesApuVisualState) {
        let headAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
            .foregroundColor: NSColor(hex: "#FFFF66")
        ]
        "CH | ECHO PMOD NOISE KEYON KEYOFF ENDX".draw(at: CGPoint(x: 4, y: 2), withAttributes: headAttrs)

        let eon = state.dspReg(0x4D)
        let pmon = state.dspReg(0x2D)
        let non = state.dspReg(0x3D)
        let kon = state.dspReg(0x4C)
        let koff = state.dspReg(0x5C)
        let endx = state.dspReg(0x7C)

        for i in 0..<8 {
            let bit = UInt8(1 << i)
            let eStr = (eon & bit) != 0 ? "ON " : "-- "
            let pStr = (pmon & bit) != 0 ? "ON  " : "--  "
            let nStr = (non & bit) != 0 ? "ON   " : "--   "
            let kStr = (kon & bit) != 0 ? "ON   " : "--   "
            let fStr = (koff & bit) != 0 ? "OFF   " : "--    "
            let xStr = (endx & bit) != 0 ? "END" : "--"

            let rowStr = String(format: "%d  | %@  %@%@%@%@%@", i + 1, eStr, pStr, nStr, kStr, fStr, xStr)
            rowStr.draw(at: CGPoint(x: 4, y: CGFloat(16 + (i * 9))), withAttributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 8, weight: .regular),
                .foregroundColor: NSColor(hex: "#ECECEC")
            ])
        }
    }

    private func drawChannel4View(ctx: CGContext, state: SnesApuVisualState) {
        let headAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
            .foregroundColor: NSColor(hex: "#FFFF66")
        ]
        "CH | SCRN (Source)  DSP DIR OFFSET".draw(at: CGPoint(x: 4, y: 2), withAttributes: headAttrs)
        let dir = UInt16(state.dspReg(0x5D)) << 8

        for i in 0..<8 {
            let src = state.dspReg(i << 4 | 0x04)
            let tblOffset = dir + UInt16(src) * 4
            let rowStr = String(format: "%d  |  Source #%03d   Table Addr: 0x%04X", i + 1, src, tblOffset)
            rowStr.draw(at: CGPoint(x: 4, y: CGFloat(16 + (i * 9))), withAttributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
                .foregroundColor: NSColor(hex: "#00FF66")
            ])
        }
    }

    private func drawTags1View(ctx: CGContext, state: SnesApuVisualState, engine: SpcEngine) {
        let textAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: NSColor(hex: "#00FFCC")
        ]
        String("Title    : \(engine.title.prefix(25))").draw(at: CGPoint(x: 4, y: 4), withAttributes: textAttrs)
        String("Game     : \(engine.game.prefix(25))").draw(at: CGPoint(x: 4, y: 16), withAttributes: textAttrs)
        String("Artist   : \(engine.artist.prefix(25))").draw(at: CGPoint(x: 4, y: 28), withAttributes: textAttrs)
        String("Dumper   : \(engine.dumper.prefix(25))").draw(at: CGPoint(x: 4, y: 40), withAttributes: textAttrs)
        String("Date     : \(engine.dateString.prefix(25))").draw(at: CGPoint(x: 4, y: 52), withAttributes: textAttrs)
        String("Comment  : \(engine.comment.prefix(25))").draw(at: CGPoint(x: 4, y: 64), withAttributes: textAttrs)
        let elapsed = Double(state.t64_ticks) / 64000.0
        let timeStr = String(format: "Time     : %02d:%02d / %02d:%02d  (Fade: %.1fs)",
                             Int(elapsed) / 60, Int(elapsed) % 60, Int(engine.songDuration) / 60, Int(engine.songDuration) % 60, engine.fadeDuration)
        timeStr.draw(at: CGPoint(x: 4, y: 76), withAttributes: [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: NSColor(hex: "#ECECEC")
        ])
    }

    private func drawTags2View(ctx: CGContext, state: SnesApuVisualState, engine: SpcEngine) {
        let textAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: NSColor(hex: "#00FFCC")
        ]
        "Header   : SNES-SPC700 Sound File Data v\(engine.spcVersion)".draw(at: CGPoint(x: 4, y: 4), withAttributes: textAttrs)
        "Emulator : \(engine.emulatorString)".draw(at: CGPoint(x: 4, y: 18), withAttributes: textAttrs)
        "KeyOnMask: 0x\(String(format: "%02X", state.key_on_mask))".draw(at: CGPoint(x: 4, y: 32), withAttributes: textAttrs)

        let pswFlags = "NVPBHIZC"
        "FLAGS    : \(pswFlags)".draw(at: CGPoint(x: 4, y: 46), withAttributes: [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
            .foregroundColor: NSColor(hex: "#FFFF66")
        ])
        let elapsed = Double(state.t64_ticks) / 64000.0
        String(format: "Ticks 64k: %u  (%.2f sec)", state.t64_ticks, elapsed).draw(at: CGPoint(x: 4, y: 64), withAttributes: textAttrs)
    }

    private func drawScript700View(ctx: CGContext, state: SnesApuVisualState) {
        let headAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
            .foregroundColor: NSColor(hex: "#FFFF66")
        ]
        let textAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 9, weight: .regular),
            .foregroundColor: NSColor(hex: "#00FFCC")
        ]
        "SCRIPT700 & HARDWARE TELEMETRY".draw(at: CGPoint(x: 4, y: 4), withAttributes: headAttrs)
        "DIR Base: 0x\(String(format: "%02X00", state.dspReg(0x5D)))   ESA Echo: 0x\(String(format: "%02X00", state.dspReg(0x6D)))".draw(at: CGPoint(x: 4, y: 22), withAttributes: textAttrs)
        "EDL Delay: \(state.dspReg(0x7D) & 0x0F) (\(Int(state.dspReg(0x7D) & 0x0F) * 16)ms)  EFB: \(Int8(bitPattern: state.dspReg(0x0D)))%".draw(at: CGPoint(x: 4, y: 36), withAttributes: textAttrs)
        "FLG Reg : 0x\(String(format: "%02X", state.dspReg(0x6C)))   ENDX Mask: 0x\(String(format: "%02X", state.dspReg(0x7C)))".draw(at: CGPoint(x: 4, y: 50), withAttributes: textAttrs)
        "Clock Cycles (64 kHz Ticks): \(state.t64_ticks)".draw(at: CGPoint(x: 4, y: 66), withAttributes: textAttrs)
    }

    private func drawBar(ctx: CGContext, x: Int, level: Double, width: Int, color: NSColor) {
        let maxH = 38.0
        let h = min(1.0, max(0.0, level)) * maxH
        if h > 0 {
            ctx.setFillColor(color.cgColor)
            ctx.fill(CGRect(x: Double(x), y: 94.0 - h, width: Double(width), height: h))
        }
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let clickX = p.x / (bounds.width / 287.0)
        let clickY = p.y / (bounds.height / 96.0)

        if viewMode == .indicator && clickX >= 135 && clickX <= 285 && clickY >= 20 && clickY <= 38 {
            if event.modifierFlags.contains(.shift) {
                let ratio = max(0.0, min(1.0, (clickX - 140.0) / 140.0))
                if let engine = engine { engine.setStartRepeatMark(to: ratio * engine.songDuration) }
            } else {
                isScrubbing = true
                scrubRatio = max(0.0, min(1.0, (clickX - 140.0) / 140.0))
                if let engine = engine { engine.seek(to: scrubRatio * engine.songDuration) }
            }
            needsDisplay = true
        } else {
            let nextMode = (viewMode.rawValue + 1) % ViewMode.allCases.count
            viewMode = ViewMode(rawValue: nextMode)!
        }
    }

    override func rightMouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let clickX = p.x / (bounds.width / 287.0)
        let clickY = p.y / (bounds.height / 96.0)

        if viewMode == .indicator && clickX >= 135 && clickX <= 285 && clickY >= 20 && clickY <= 38 {
            let ratio = max(0.0, min(1.0, (clickX - 140.0) / 140.0))
            if let engine = engine { engine.setLimitRepeatMark(to: ratio * engine.songDuration) }
            needsDisplay = true
        } else {
            isDetailDecMode.toggle()
        }
    }

    override func mouseDragged(with event: NSEvent) {
        if isScrubbing {
            let p = convert(event.locationInWindow, from: nil)
            let clickX = p.x / (bounds.width / 287.0)
            scrubRatio = max(0.0, min(1.0, (clickX - 140.0) / 140.0))
            needsDisplay = true
        }
    }

    override func mouseUp(with event: NSEvent) {
        if isScrubbing {
            isScrubbing = false
            if let engine = engine { engine.seek(to: scrubRatio * engine.songDuration) }
            needsDisplay = true
        }
    }
}
