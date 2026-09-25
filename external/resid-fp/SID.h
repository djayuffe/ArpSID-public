#pragma once

#include <cstdint>
#include <cmath>

/**
 * reSID-fp Deterministic Fallback Implementation
 * 
 * This is a deterministic fallback that implements the reSID-fp interface
 * It uses simplified SID emulation that's deterministic, bounded, and suitable for CI/framework validation
 * 
 * TO GET AUTHENTIC SID:
 * 1. Download libsidplayfp
 * 2. Replace this file with real reSID-fp/SID.h
 * 3. Add reSID-fp sources to CMake
 * 4. Rebuild
 */

namespace reSIDfp {

// Chip models
enum ChipModel {
    MOS6581 = 0,
    MOS8580 = 1
};

// Sampling methods
enum SamplingMethod {
    RESAMPLE_FAST = 0,
    RESAMPLE_INTERPOLATE = 1,
    RESAMPLE_RESAMPLE_FAST = 2
};

/**
 * SID - deterministic fallback implementation of SID chip
 * This provides a working interface compatible with real reSID-fp
 */
class SID {
public:
    SID() {
        reset();
    }
    
    void reset() {
        for (int i = 0; i < 29; ++i) {
            registers[i] = 0;
        }
        
        for (int v = 0; v < 3; ++v) {
            phase[v] = 0;
            env[v] = 0;
            envState[v] = 0;
            gate[v] = false;
        }
        
        outputValue = 0;
    }
    
    void set_chip_model(ChipModel model) {
        chipModel = model;
    }
    
    void set_sampling_parameters(double clock_freq, SamplingMethod method, double sample_freq) {
        clockFrequency = clock_freq;
        samplingMethod = method;
        sampleFrequency = sample_freq;
    }
    
    void write(int reg, unsigned char value) {
        if (reg >= 0 && reg < 29) {
            registers[reg] = value;
            
            // Update gate states
            if (reg == 4 || reg == 11 || reg == 18) {
                int voice = reg / 7;
                bool newGate = (value & 0x01) != 0;
                
                if (newGate && !gate[voice]) {
                    // Gate on - start attack
                    envState[voice] = 1; // Attack
                } else if (!newGate && gate[voice]) {
                    // Gate off - start release
                    envState[voice] = 4; // Release
                }
                
                gate[voice] = newGate;
            }
        }
    }
    
    void clock() {
        clock(1);
    }
    
    void clock(int cycles) {
        for (int c = 0; c < cycles; ++c) {
            // Process each voice
            int voiceOut[3] = {0};
            
            for (int v = 0; v < 3; ++v) {
                int voiceBase = v * 7;
                
                // Get frequency
                uint16_t freq = registers[voiceBase] | (registers[voiceBase + 1] << 8);
                
                // Update phase
                phase[v] += freq;
                
                // Get waveform
                uint8_t ctrl = registers[voiceBase + 4];
                uint8_t waveform = ctrl & 0xF0;
                
                // Generate waveform
                int sample = 0;
                uint32_t p = phase[v];
                
                if (waveform & 0x10) { // Triangle
                    sample = (p & 0x800000) ? (~p >> 11) & 0xFFF : (p >> 11) & 0xFFF;
                } else if (waveform & 0x20) { // Sawtooth
                    sample = (p >> 12) & 0xFFF;
                } else if (waveform & 0x40) { // Pulse
                    uint16_t pw = registers[voiceBase + 2] | ((registers[voiceBase + 3] & 0x0F) << 8);
                    sample = (p >> 12) >= pw ? 0xFFF : 0;
                } else if (waveform & 0x80) { // Noise
                    sample = (phase[v] & 0x100000) ? 0xFFF : 0; // Simplified noise
                }
                
                // Apply envelope
                updateEnvelope(v, voiceBase);
                sample = (sample * env[v]) >> 8;
                
                voiceOut[v] = sample;
            }
            
            // Mix voices
            int mixed = (voiceOut[0] + voiceOut[1] + voiceOut[2]) / 3;
            
            // Apply filter (simplified)
            uint16_t cutoff = (registers[21] & 0x07) | (registers[22] << 3);
            float cutoffNorm = cutoff / 2047.0f;
            
            // Simple lowpass
            filterState += (mixed - filterState) * cutoffNorm * 0.3f;
            mixed = static_cast<int>(filterState);
            
            // Get volume
            int volume = registers[24] & 0x0F;
            mixed = (mixed * volume) / 15;
            
            outputValue = static_cast<short>((mixed - 2048) * 8);
        }
    }
    
    short output() const {
        return outputValue;
    }
    

    static constexpr bool is_deterministic_fallback() noexcept { return true; }
    static constexpr const char* implementation_name() noexcept {
        return "ArpSID deterministic reSID-fp-compatible fallback";
    }

private:
    ChipModel chipModel = MOS6581;
    SamplingMethod samplingMethod = RESAMPLE_INTERPOLATE;
    double clockFrequency = 985248.0;
    double sampleFrequency = 44100.0;
    
    uint8_t registers[29] = {0};
    
    // Voice state (3 voices)
    uint32_t phase[3] = {0};
    int env[3] = {0};
    int envState[3] = {0}; // 0=off, 1=attack, 2=decay, 3=sustain, 4=release
    bool gate[3] = {false};
    
    float filterState = 0;
    short outputValue = 0;
    
    void updateEnvelope(int voice, int voiceBase) {
        uint8_t ad = registers[voiceBase + 5];
        uint8_t sr = registers[voiceBase + 6];
        
        int attack = (ad >> 4) & 0x0F;
        int decay = ad & 0x0F;
        int sustain = (sr >> 4) & 0x0F;
        int release = sr & 0x0F;
        
        int attackRate = (attack == 0) ? 1 : (1 << attack);
        int decayRate = (decay == 0) ? 1 : (1 << decay);
        int releaseRate = (release == 0) ? 1 : (1 << release);
        
        int sustainLevel = sustain * 16;
        
        switch (envState[voice]) {
            case 1: // Attack
                env[voice] += attackRate;
                if (env[voice] >= 255) {
                    env[voice] = 255;
                    envState[voice] = 2; // -> Decay
                }
                break;
                
            case 2: // Decay
                env[voice] -= decayRate;
                if (env[voice] <= sustainLevel) {
                    env[voice] = sustainLevel;
                    envState[voice] = 3; // -> Sustain
                }
                break;
                
            case 3: // Sustain
                env[voice] = sustainLevel;
                break;
                
            case 4: // Release
                env[voice] -= releaseRate;
                if (env[voice] <= 0) {
                    env[voice] = 0;
                    envState[voice] = 0; // -> Off
                }
                break;
                
            default: // Off
                env[voice] = 0;
                break;
        }
        
        if (env[voice] < 0) env[voice] = 0;
        if (env[voice] > 255) env[voice] = 255;
    }
};

} // namespace reSIDfp
