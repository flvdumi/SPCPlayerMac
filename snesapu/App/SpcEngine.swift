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
    
    func setBitDepth(_ bits: Int) {
            lock.lock()
            defer { lock.unlock() }
            targetBitDepth = bits
            snesapu_set_bit_depth(apu, Int32(bits))
        }

        func setSampleRate(_ rate: Double) {
            lock.lock()
            defer { lock.unlock() }
            guard rate != targetSampleRate else { return }
            targetSampleRate = rate
            snesapu_set_sample_rate(apu, UInt32(rate))
            setupAudioGraph() // Reconfigures source node sample rate on the fly
        }

        func setupAudioGraph() {
            lock.lock()
            defer { lock.unlock() }
            
            let wasRunning = isPlaying && !isPaused
            if let existing = audioEngine, existing.isRunning {
                existing.stop()
            }
            
            let engine = AVAudioEngine()
            let outputNode = engine.outputNode
            let hwFormat = outputNode.outputFormat(forBus: 0)
            
            // 1. Dynamic format matching the user-selected Sample Rate
            let activeRate = targetSampleRate > 0 ? targetSampleRate : 32000.0
            guard let snesFormat = AVAudioFormat(
                commonFormat: .pcmFormatFloat32,
                sampleRate: activeRate,
                channels: 2,
                interleaved: false
            ) else { return }
            
            // 2. Direct rendering into Float32 buffers (no intermediate Int16 buffers or conversions)
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
                
                // Calls C++ directly with live quantization and live resampling applied!
                snesapu_render_float(self.apu, leftOut, rightOut, frames)
                return noErr
            }
            
            // 3. Connect: mainMixerNode adapts activeRate to the Mac's hardware DAC format
            engine.attach(node)
            engine.connect(node, to: engine.mainMixerNode, format: snesFormat)
            engine.connect(engine.mainMixerNode, to: outputNode, format: hwFormat)
            
            self.audioEngine = engine
            self.sourceNode = node
            
            if wasRunning {
                try? engine.start()
            }
        }

        func applyAllParameters() {
            lock.lock()
            defer { lock.unlock() }
            
            let mul = Float(pow(2.0, Double(pitchKeyShift) / 12.0))
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
            snesapu_set_sample_rate(apu, UInt32(targetSampleRate))
            snesapu_set_bit_depth(apu, Int32(targetBitDepth))
            snesapu_set_channels(apu, Int32(isMono ? 1 : 2))
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
        if playTimeMode == .endless {
            snesapu_set_song_length(apu, 0xFFFFFFFF, 0)
        } else {
            let songTicks = UInt32((playTimeMode == .defaultTime ? defaultDuration : songDuration) * 64000.0)
            let fadeTicks = UInt32(fadeDuration * 64000.0)
            snesapu_set_song_length(apu, songTicks, fadeTicks)
        }
        
        
        applyAllParameters()
        play()
        return true
    }
    func getVoiceScope(channel: Int, count: Int = 1024) -> [Int16] {
            var buffer = [Int16](repeating: 0, count: count)
            lock.lock()
            defer { lock.unlock() }
            if let apu = apu {
                snesapu_get_voice_scope(apu, Int32(channel), &buffer, count)
            }
            return buffer
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
        
        let rate = UInt32(targetSampleRate > 0 ? targetSampleRate : 32000)
        let channels: UInt16 = isMono ? 1 : 2
        let bits: Int = targetBitDepth == 0 ? 16 : targetBitDepth
        
        // Configure export APU instance
        snesapu_set_sample_rate(exportApu, rate)
        snesapu_set_bit_depth(exportApu, Int32(bits))
        snesapu_set_channels(exportApu, Int32(channels))
        snesapu_set_speed(exportApu, currentSpeed)
        snesapu_set_amp(exportApu, currentAmp)
        snesapu_set_stereo_separation(exportApu, isMono ? 0 : stereoSepValue)
        snesapu_set_feedback_mixer(exportApu, feedbackValue)
        snesapu_set_pitch_base_hz(exportApu, pitchBaseHz)
        snesapu_set_dsp_options(exportApu, dspOptions)
        snesapu_set_interpolation(exportApu, interpolationMode.rawValue)
        
        let totalDuration = songDuration + fadeDuration
        let totalFrames = Int(totalDuration * Double(rate))
        
        let bytesPerSample: Int
        let bitsPerSampleHeader: UInt16
        let audioFormat: UInt16 // 1 = PCM Integer, 3 = IEEE Float
        
        if bits == -4 || bits == -32 {
            bytesPerSample = 4
            bitsPerSampleHeader = 32
            audioFormat = 3
        } else {
            bytesPerSample = bits / 8
            bitsPerSampleHeader = UInt16(bits)
            audioFormat = 1
        }
        
        let blockAlign = channels * UInt16(bytesPerSample)
        let byteRate = rate * UInt32(blockAlign)
        let pcmByteSize = totalFrames * Int(blockAlign)
        
        var pcmPayload = Data(count: pcmByteSize)
        pcmPayload.withUnsafeMutableBytes { ptr in
            guard let base = ptr.baseAddress else { return }
            // Call the raw void* renderer
            snesapu_render_raw(exportApu, base, totalFrames)
        }
        
        var wavData = Data()
        wavData.append(contentsOf: "RIFF".utf8)
        let chunkSize = UInt32(36 + pcmPayload.count)
        withUnsafeBytes(of: chunkSize.littleEndian) { wavData.append(contentsOf: $0) }
        wavData.append(contentsOf: "WAVEfmt ".utf8)
        withUnsafeBytes(of: UInt32(16).littleEndian) { wavData.append(contentsOf: $0) }
        withUnsafeBytes(of: audioFormat.littleEndian) { wavData.append(contentsOf: $0) }
        withUnsafeBytes(of: channels.littleEndian) { wavData.append(contentsOf: $0) }
        withUnsafeBytes(of: rate.littleEndian) { wavData.append(contentsOf: $0) }
        withUnsafeBytes(of: byteRate.littleEndian) { wavData.append(contentsOf: $0) }
        withUnsafeBytes(of: blockAlign.littleEndian) { wavData.append(contentsOf: $0) }
        withUnsafeBytes(of: bitsPerSampleHeader.littleEndian) { wavData.append(contentsOf: $0) }
        wavData.append(contentsOf: "data".utf8)
        withUnsafeBytes(of: UInt32(pcmPayload.count).littleEndian) { wavData.append(contentsOf: $0) }
        wavData.append(pcmPayload)
        
        try? wavData.write(to: destination)
    }
}
