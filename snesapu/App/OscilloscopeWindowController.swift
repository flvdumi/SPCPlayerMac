import Cocoa

final class OscilloscopeView: NSView {
    weak var engine: SpcEngine?
    private let sampleCount = 1024
    private let displayCount = 512

    // Smoothed visual activity per channel to eliminate rapid 60Hz flickering
    private var smoothedActivity: [CGFloat] = Array(repeating: 0.0, count: 8)
    private var lastTriggerIdx: [Int] = Array(repeating: 0, count: 8)

    // Distinct neon CRT phosphor colors for the 8 SNES voice channels
    private let channelColors: [NSColor] = [
        NSColor(hex: "#00FF66"), // Ch 1: Phosphor Green
        NSColor(hex: "#00E5FF"), // Ch 2: Cyan
        NSColor(hex: "#FFDD00"), // Ch 3: Amber Yellow
        NSColor(hex: "#FF3366"), // Ch 4: Coral Red
        NSColor(hex: "#BF55EC"), // Ch 5: Neon Purple
        NSColor(hex: "#FF8800"), // Ch 6: Tangerine
        NSColor(hex: "#3388FF"), // Ch 7: Azure
        NSColor(hex: "#00FFAA")  // Ch 8: Mint Spring
    ]

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext, let engine = engine else { return }

        // Dark oscilloscope background
        ctx.setFillColor(NSColor(hex: "#0D0D10").cgColor)
        ctx.fill(bounds)

        let laneHeight = bounds.height / 8.0

        for ch in 0..<8 {
            let laneY = CGFloat(ch) * laneHeight
            let midY = laneY + (laneHeight / 2.0)
            let isMuted = (engine.channelMuteMask & (1 << ch)) != 0
            let themeColor = channelColors[ch]

            // 1. Grid divider and center zero-reference line
            ctx.setStrokeColor(NSColor(hex: "#1A1A1E").cgColor)
            ctx.setLineWidth(1.0)
            ctx.strokeLineSegments(between: [CGPoint(x: 0, y: laneY), CGPoint(x: bounds.width, y: laneY)])

            ctx.setStrokeColor(NSColor(hex: "#141418").cgColor)
            ctx.strokeLineSegments(between: [CGPoint(x: 0, y: midY), CGPoint(x: bounds.width, y: midY)])

            // 2. Fetch raw voice samples
            let rawSamples = engine.getVoiceScope(channel: ch, count: sampleCount)

            // Measure peak amplitude in this window
            var peakAmp: Int32 = 0
            for s in rawSamples {
                let absVal = abs(Int32(s))
                if absVal > peakAmp { peakAmp = absVal }
            }

            // Smooth decay envelope follower: fast attack, smooth 180ms exponential decay
            let rawActivity: CGFloat = (!isMuted && peakAmp > 120) ? 1.0 : 0.0
            if rawActivity > smoothedActivity[ch] {
                smoothedActivity[ch] = rawActivity // instant attack on new note
            } else {
                smoothedActivity[ch] = max(0.0, smoothedActivity[ch] * 0.88) // smooth release fade
            }
            let activity = smoothedActivity[ch]

            // 3. Channel Label (STEADY - NEVER STROBES TO GRAY BETWEEN NOTES)
            let labelAttrs: [NSAttributedString.Key: Any] = [
                .font: NSFont.monospacedSystemFont(ofSize: 10, weight: .bold),
                // Text stays its signature color permanently unless explicitly muted
                .foregroundColor: isMuted ? NSColor(hex: "#555555") : themeColor
            ]
            let labelText = "CH \(ch + 1)" + (isMuted ? " [MUTE]" : "")
            labelText.draw(at: CGPoint(x: 8, y: laneY + 4), withAttributes: labelAttrs)

            // 4. Stable Trigger Selection
            var startIdx = 0
            let searchLimit = min(256, sampleCount - displayCount)

            if peakAmp > 150 && !isMuted {
                // Adaptive hysteresis threshold based on note volume
                let triggerThreshold = max(Int32(64), peakAmp / 8)
                var foundTrigger = false

                for i in 1..<searchLimit {
                    if rawSamples[i - 1] <= 0 && rawSamples[i] > 0 &&
                       (Int32(rawSamples[i]) - Int32(rawSamples[i - 1]) >= triggerThreshold) {
                        startIdx = i
                        lastTriggerIdx[ch] = i
                        foundTrigger = true
                        break
                    }
                }
                // Fall back to previous phase lock if edge was temporarily missed
                if !foundTrigger {
                    startIdx = min(lastTriggerIdx[ch], searchLimit)
                }
            }

            // 5. Build Waveform Path
            let path = CGMutablePath()
            let xStep = bounds.width / CGFloat(displayCount - 1)

            if activity > 0.01 && !isMuted {
                var first = true
                for i in 0..<displayCount {
                    let sIdx = startIdx + i
                    guard sIdx < rawSamples.count else { break }
                    let norm = CGFloat(rawSamples[sIdx]) / 32768.0

                    let px = CGFloat(i) * xStep
                    let py = midY - (norm * (laneHeight * 0.42))

                    if first {
                        path.move(to: CGPoint(x: px, y: py))
                        first = false
                    } else {
                        path.addLine(to: CGPoint(x: px, y: py))
                    }
                }

                // Phosphor Trace with smooth activity alpha
                ctx.saveGState()
                ctx.setShadow(offset: .zero, blur: 4.0 * activity, color: themeColor.withAlphaComponent(0.5 * activity).cgColor)
                ctx.setStrokeColor(themeColor.withAlphaComponent(0.25 + 0.75 * activity).cgColor)
                ctx.setLineWidth(1.4)
                ctx.setLineJoin(.round)
                ctx.setLineCap(.round)
                ctx.addPath(path)
                ctx.strokePath()
                ctx.restoreGState()

            } else {
                // Silent state: clean, completely motionless flat zero-line
                ctx.saveGState()
                ctx.setStrokeColor(NSColor(hex: "#1E1E24").cgColor)
                ctx.setLineWidth(1.0)
                ctx.strokeLineSegments(between: [CGPoint(x: 0, y: midY), CGPoint(x: bounds.width, y: midY)])
                ctx.restoreGState()
            }
        }
    }
}

final class OscilloscopeWindowController: NSWindowController {
    private let engine: SpcEngine
    private var scopeView: OscilloscopeView!
    private var displayTimer: Timer?

    init(engine: SpcEngine) {
        self.engine = engine
        let win = NSWindow(
            contentRect: NSRect(x: 100, y: 100, width: 700, height: 600),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        win.title = "S-DSP 8-Channel Oscilloscope"
        win.minSize = NSSize(width: 400, height: 350)
        win.backgroundColor = NSColor(hex: "#0D0D10")
        super.init(window: win)

        scopeView = OscilloscopeView(frame: win.contentView!.bounds)
        scopeView.autoresizingMask = [.width, .height]
        scopeView.engine = engine
        win.contentView?.addSubview(scopeView)

        // Steady 60 FPS refresh
        displayTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            self?.scopeView.needsDisplay = true
        }
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        displayTimer?.invalidate()
    }
}

