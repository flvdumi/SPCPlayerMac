import Foundation
import AVFoundation

protocol SpcEngineDelegate: AnyObject {
    func spcEngineDidFinishTrack(_ engine: SpcEngine)
}

final class SpcEngine {
    enum PlayOrder: Int {
        case stop = 0, next = 1, previous = 2, random = 3, shuffle = 4, repeatOne = 5
    }

    enum PlayTimeMode: Int {
        case endless = 0, id666Priority = 1, defaultTime = 2
    }

    enum Interpolation: UInt8 {
        case none = 0, linear = 1, cubic = 2, gauss = 3
    }

    weak var delegate: SpcEngineDelegate?

    private(set) var apu: UnsafeMutableRawPointer?
    private var audioEngine: AVAudioEngine?
    private var sourceNode: AVAudioSourceNode?

    private(set) var isPlaying = false
    private(set) var isPaused = false
    private let lock = NSRecursiveLock()

    var currentSpcURL: URL?
    var rawSpcData: Data?

    // Audio Engine State
    var channelMuteMask: UInt8 = 0x00
    var channelNoiseMask: UInt8 = 0x00
    var currentAmp: Float = 1.0
    var currentSpeed: Float = 1.0
    var stereoSepValue: UInt32 = 32768 // 32768 = 50% [Normal SNES Stereo]
    var feedbackValue: UInt32 = 0
    var pitchBaseHz: UInt32 = 32000
    var pitchKeyShift: Int = 0
    var pitchSyncSpeed: Bool = false
    var dspOptions: UInt32 = 0x01 | 0x800
    var interpolationMode: Interpolation = .gauss

    var targetSampleRate: Double = 32000.0
    var targetBitDepth: Int = 16
    var isMono: Bool = false

    static let ampPresets: [Float] = [
        0.05, 0.10, 0.15, 0.20, 0.25, 0.33, 0.40, 0.50,
        0.67, 0.75, 0.80, 0.90, 1.00, 1.10, 1.25, 1.33,
        1.50, 2.00, 2.50, 3.00, 4.00
    ]
    static let speedPresets: [Float] = [
        0.25, 0.33, 0.50, 0.67, 0.75, 0.80, 0.90, 1.00,
        1.10, 1.25, 1.33, 1.50, 2.00, 2.50, 3.00, 4.00
    ]

    var playOrder: PlayOrder = .next
    var playTimeMode: PlayTimeMode = .id666Priority
    var defaultDuration: Double = 180.0
    var songDuration: Double = 180.0
    var fadeDuration: Double = 10.0
    var waitDuration: Double = 3.0

    var seekTimeStep: Double = 5.0
    var isSeekAsync: Bool = true
    var isSeekFast: Bool = true

    var isTimeRepeatActive = false
    var repeatStartTime: Double = 0.0
    var repeatLimitTime: Double = 0.0

    var title = "(Unknown)"
    var game = "(Unknown)"
    var artist = "(Unknown)"
    var dumper = "(Unknown)"
    var comment = ""
    var dateString = ""
    var emulatorString = "Unknown"
    var spcVersion: UInt8 = 0

    private let maxFramesPerRender = 8192
    private var rawPcmBuffer: UnsafeMutablePointer<Int16>

    init() {
        self.rawPcmBuffer = UnsafeMutablePointer<Int16>.allocate(capacity: maxFramesPerRender * 2)
        self.apu = snesapu_create()
        setupAudioGraph()
    }

    deinit {
        stop()
        if let apu = apu { snesapu_destroy(apu) }
        rawPcmBuffer.deallocate()
    }

    func setupAudioGraph() {
        lock.lock()
        defer { lock.unlock() }

        if let existing = audioEngine, existing.isRunning {
            existing.stop()
        }

        let engine = AVAudioEngine()

        // 1. Lock output to the Mac hardware's current native rate (e.g. 48 kHz or 44.1 kHz).
        // This stops AVAudioEngine from forcing macOS or other apps to 32 kHz.
        let outputNode = engine.outputNode
        let hwFormat = outputNode.outputFormat(forBus: 0)

        // 2. SNES APU always renders at native 32,000 Hz.
        guard let snesFormat = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 32000.0, channels: 2, interleaved: false) else { return }

        let pcmPtr = self.rawPcmBuffer

        // 3. Source node only fetches audio from C++ and normalizes it to Float32.
        // Zero DSP or post-processing logic in Swift.
        let node = AVAudioSourceNode(format: snesFormat) { [weak self] _, _, frameCount, audioBufferList -> OSStatus in
            guard let self = self else { return noErr }
            let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
            guard let leftOut = buffers[0].mData?.assumingMemoryBound(to: Float.self),
                  let rightOut = buffers[1].mData?.assumingMemoryBound(to: Float.self) else { return noErr }

            let frames = Int(frameCount)
            guard self.isPlaying && !self.isPaused, self.lock.try() else {
                memset(leftOut, 0, frames * MemoryLayout<Float>.size)
                memset(rightOut, 0, frames * MemoryLayout<Float>.size)
                return noErr
            }
            defer { self.lock.unlock() }

            let renderFrames = min(frames, self.maxFramesPerRender)
            snesapu_render(self.apu, pcmPtr, renderFrames)

            let scale: Float = 1.0 / 32768.0
            for i in 0..<renderFrames {
                leftOut[i]  = Float(pcmPtr[i * 2])     * scale
                rightOut[i] = Float(pcmPtr[i * 2 + 1]) * scale
            }

            return noErr
        }

        // 4. Attach & Connect: mainMixerNode resamples 32 kHz -> 48 kHz in-memory without touching the DAC.
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: snesFormat)
        engine.connect(engine.mainMixerNode, to: outputNode, format: hwFormat)

        self.audioEngine = engine
        self.sourceNode = node
        try? engine.start()
    }

    func applyAllParameters() {
        lock.lock()
        defer { lock.unlock() }

        let mul = Float(pow(2.0, Double(pitchKeyShift) / 12.0))

        // C++ DSP handles Mono by setting stereo separation to 0
        let effectiveStereo = isMono ? 0 : stereoSepValue

        snesapu_set_channel_mute(apu, channelMuteMask)
        snesapu_set_channel_noise(apu, channelNoiseMask)
        snesapu_set_speed(apu, currentSpeed)
        snesapu_set_amp(apu, currentAmp)
        snesapu_set_stereo_separation(apu, effectiveStereo)
        snesapu_set_feedback_mixer(apu, feedbackValue)
        snesapu_set_pitch_base_hz(apu, pitchBaseHz)
        snesapu_set_pitch_multiplier(apu, mul)
        snesapu_set_pitch_sync_speed(apu, pitchSyncSpeed)
        snesapu_set_dsp_options(apu, dspOptions)
        snesapu_set_interpolation(apu, interpolationMode.rawValue)
    }

    func loadSPC(url: URL) -> Bool {
        guard let data = try? Data(contentsOf: url), data.count >= 66048 else { return false }
        stop()

        self.rawSpcData = data
        self.currentSpcURL = url

        let ok = data.withUnsafeBytes { ptr -> Bool in
            guard let base = ptr.bindMemory(to: UInt8.self).baseAddress else { return false }
            return snesapu_load_spc(apu, base, data.count)
        }
        guard ok else { return false }

        let baseNoExt = url.deletingPathExtension()
        for ext in ["700", "7se", "700.txt", "7se.txt"] {
            let sURL = baseNoExt.appendingPathExtension(ext)
            if FileManager.default.fileExists(atPath: sURL.path),
               let scriptStr = try? String(contentsOf: sURL, encoding: .utf8) {
                snesapu_load_script(apu, scriptStr)
                break
            }
        }

        parseID666(data: data)
        resetRepeatMarks()
        applyAllParameters()
        play()
        return true
    }

    private func parseID666(data: Data) {
        func extractString(from offset: Int, length: Int) -> String {
            guard offset + length <= data.count else { return "" }
            let sub = data.subdata(in: offset..<(offset + length))
            return String(bytes: sub, encoding: .isoLatin1)?.trimmingCharacters(in: .controlCharacters.union(.whitespaces)) ?? ""
        }

        self.spcVersion = data[0x25]
        self.title   = extractString(from: 0x2E, length: 32)
        self.game    = extractString(from: 0x4E, length: 32)
        self.dumper  = extractString(from: 0x6E, length: 16)
        self.comment = extractString(from: 0x7E, length: 32)
        self.dateString = extractString(from: 0x9E, length: 11)
        self.artist  = extractString(from: 0xB1, length: 32)

        let emuID = data[0xD2] & 0x0F
        switch emuID {
        case 1: self.emulatorString = "ZSNES"
        case 2: self.emulatorString = "Snes9x"
        case 3: self.emulatorString = "ZST2SPC"
        case 7: self.emulatorString = "Snes9xpp"
        case 8: self.emulatorString = "SNESGT"
        default: self.emulatorString = "Unknown"
        }

        let durationStr = extractString(from: 0xA9, length: 3)
        let fadeStr = extractString(from: 0xAC, length: 5)
        self.songDuration = Double(durationStr) ?? 180.0
        if self.songDuration <= 0 { self.songDuration = 180.0 }
        self.fadeDuration = (Double(fadeStr) ?? 10000.0) / 1000.0
        self.repeatLimitTime = self.songDuration
    }

    func play() {
        isPlaying = true
        isPaused = false
        if !(audioEngine?.isRunning ?? false) {
            try? audioEngine?.start()
        }
    }

    func pause() { isPaused = true }
    func resume() { isPaused = false }

    func stop() {
        isPlaying = false
        isPaused = false
        snesapu_reset(apu)
    }

    func getVisualState() -> SnesApuVisualState {
        var state = SnesApuVisualState()
        snesapu_get_visual_state(apu, &state)
        return state
    }

    func stepVolume(up: Bool) {
        if up {
            if let next = Self.ampPresets.first(where: { $0 > currentAmp + 0.001 }) {
                currentAmp = next
            }
        } else {
            if let prev = Self.ampPresets.last(where: { $0 < currentAmp - 0.001 }) {
                currentAmp = prev
            }
        }
        applyAllParameters()
    }

    func stepSpeed(up: Bool) {
        if up {
            if let next = Self.speedPresets.first(where: { $0 > currentSpeed + 0.001 }) {
                currentSpeed = next
            }
        } else {
            if let prev = Self.speedPresets.last(where: { $0 < currentSpeed - 0.001 }) {
                currentSpeed = prev
            }
        }
        applyAllParameters()
    }

    func seek(to seconds: Double) {
        guard let data = rawSpcData else { return }
        let clampedSec = max(0.0, min(songDuration + fadeDuration, seconds))
        let targetTicks = UInt32(clampedSec * 64000.0)

        lock.lock()
        defer { lock.unlock() }

        var state = SnesApuVisualState()
        snesapu_get_visual_state(apu, &state)

        if targetTicks < state.t64_ticks {
            data.withUnsafeBytes { ptr in
                guard let base = ptr.bindMemory(to: UInt8.self).baseAddress else { return }
                _ = snesapu_load_spc(apu, base, data.count)
            }
            applyAllParameters()
        }

        snesapu_seek(apu, targetTicks)
    }

    func setStartRepeatMark(to seconds: Double) {
        repeatStartTime = max(0.0, min(seconds, repeatLimitTime))
        isTimeRepeatActive = true
    }

    func setLimitRepeatMark(to seconds: Double) {
        repeatLimitTime = max(repeatStartTime, min(seconds, songDuration))
        isTimeRepeatActive = true
    }

    func resetRepeatMarks() {
        isTimeRepeatActive = false
        repeatStartTime = 0.0
        repeatLimitTime = songDuration
    }

    func exportWav(to destination: URL) {
        guard let raw = rawSpcData else { return }
        guard let exportApu = snesapu_create() else { return }
        defer { snesapu_destroy(exportApu) }

        let ok = raw.withUnsafeBytes { ptr -> Bool in
            guard let base = ptr.bindMemory(to: UInt8.self).baseAddress else { return false }
            return snesapu_load_spc(exportApu, base, raw.count)
        }
        guard ok else { return }

        let sampleRate = UInt32(targetSampleRate > 0 ? targetSampleRate : 32000)
        let mul = Float(pow(2.0, Double(pitchKeyShift) / 12.0))
        let effectiveStereo = isMono ? 0 : stereoSepValue

        snesapu_set_sample_rate(exportApu, sampleRate)
        snesapu_set_channel_mute(exportApu, channelMuteMask)
        snesapu_set_channel_noise(exportApu, channelNoiseMask)
        snesapu_set_speed(exportApu, currentSpeed)
        snesapu_set_amp(exportApu, currentAmp)
        snesapu_set_stereo_separation(exportApu, effectiveStereo)
        snesapu_set_feedback_mixer(exportApu, feedbackValue)
        snesapu_set_pitch_base_hz(exportApu, pitchBaseHz)
        snesapu_set_pitch_multiplier(exportApu, mul)
        snesapu_set_dsp_options(exportApu, dspOptions)
        snesapu_set_interpolation(exportApu, interpolationMode.rawValue)

        let totalFrames = Int(songDuration * 32000.0)
        var pcmData = [Int16](repeating: 0, count: totalFrames * 2)
        snesapu_render(exportApu, &pcmData, totalFrames)

        let channels: UInt16 = isMono ? 1 : 2
        var data = Data()
        data.append(contentsOf: "RIFF".utf8)

        var pcmPayload = Data()
        for i in 0..<totalFrames {
            if isMono {
                var monoVal = Int16((Int32(pcmData[i * 2]) + Int32(pcmData[i * 2 + 1])) / 2)
                withUnsafeBytes(of: &monoVal) { pcmPayload.append(contentsOf: $0) }
            } else {
                var l = pcmData[i * 2]
                var r = pcmData[i * 2 + 1]
                withUnsafeBytes(of: &l) { pcmPayload.append(contentsOf: $0) }
                withUnsafeBytes(of: &r) { pcmPayload.append(contentsOf: $0) }
            }
        }

        let bitsPerSample: UInt16 = 16
        let blockAlign = channels * 2
        let byteRate = 32000 * UInt32(blockAlign)
        let subchunk2Size = UInt32(pcmPayload.count)
        let chunkSize = UInt32(36 + subchunk2Size)

        data.append(contentsOf: withUnsafeBytes(of: chunkSize.littleEndian) { Array($0) })
        data.append(contentsOf: "WAVEfmt ".utf8)
        data.append(contentsOf: withUnsafeBytes(of: UInt32(16).littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt16(1).littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: channels.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: UInt32(32000).littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: byteRate.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: blockAlign.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: bitsPerSample.littleEndian) { Array($0) })
        data.append(contentsOf: "data".utf8)
        data.append(contentsOf: withUnsafeBytes(of: subchunk2Size.littleEndian) { Array($0) })
        data.append(pcmPayload)

        try? data.write(to: destination)
    }
}
