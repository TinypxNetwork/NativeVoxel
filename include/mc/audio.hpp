#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mc {

enum class SoundCategory : std::uint8_t {
    Master, Music, Record, Weather, Block, Hostile, Neutral, Player, Ambient
};

struct ResolvedSound {
    std::string resource; // e.g. minecraft:sounds/step/stone1.ogg
    SoundCategory category = SoundCategory::Master;
    float volume = 1, pitch = 1;
    bool stream = false;
};

struct DecodedSound {
    unsigned sampleRate = 0, channels = 0;
    std::vector<std::int16_t> samples; // signed 16-bit interleaved PCM
    std::size_t frames() const noexcept { return channels ? samples.size()/channels : 0; }
};

struct SoundProperties {
    float gain = 1, pitch = 1, maxDistance = 16;
};

// Pure event definitions/resolution. No audio device is opened by this class.
class SoundRegistry {
public:
    SoundRegistry();
    ~SoundRegistry();
    SoundRegistry(SoundRegistry&&) noexcept;
    SoundRegistry& operator=(SoundRegistry&&) noexcept;
    SoundRegistry(const SoundRegistry&) = delete;
    SoundRegistry& operator=(const SoundRegistry&) = delete;
    bool load(const std::string& assetDirectory, std::string& error);
    // Merge one sounds.json document using original 1.8.9 replace/append rules.
    bool mergeJson(const std::string& json, const std::string& nameSpace, std::string& error);
    // seed is a java.util.Random internal 48-bit seed; use javaSeed() to seed it.
    bool resolve(const std::string& event, std::uint64_t& seed, ResolvedSound& result,
                 std::string& error) const;
    std::uint32_t weight(const std::string& event) const;
    std::vector<std::string> eventNames() const;
    std::size_t eventCount() const noexcept;
    const std::vector<std::string>& warnings() const noexcept;
    static std::uint64_t javaSeed(std::uint64_t seed) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    // nullOutput=true never loads OpenAL or opens an audio device; useful for tests.
    bool initialize(const std::string& assetDirectory, std::string& error, bool nullOutput = false);
    bool play(const std::string& soundName, double x, double y, double z,
              float volume = 1, float pitch = 1, bool relative = false);
    // Supply interpolated EYE position. Angles are Minecraft degrees, not radians.
    void updateListener(double x, double y, double z, float yawDegrees, float pitchDegrees);
    void tick(); // service streams and retire finished voices, at least 20 times/sec
    void stopAll();
    void setMasterVolume(float volume);
    void setCategoryVolume(SoundCategory category, float volume);
    bool enabled() const noexcept;
    bool nullOutput() const noexcept;
    const std::string& lastError() const noexcept;
    std::size_t activeVoices() const noexcept;
    std::size_t decodedBufferCount() const noexcept;
    const SoundRegistry& registry() const noexcept;

    // CPU-only decoder; it cannot play audio. maxFrames bounds decoded allocation.
    static bool decodeVorbisFile(const std::string& path, DecodedSound& result,
                                std::string& error, std::size_t maxFrames = 48000u * 60u * 15u);
    static SoundProperties properties(const ResolvedSound& sound, float volume, float pitch,
                                      float categoryVolume = 1, float masterVolume = 1) noexcept;
    static float attenuation(double distance, float maxDistance, bool relative = false) noexcept;
    static std::array<float, 6> listenerOrientation(float yawDegrees, float pitchDegrees) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mc
