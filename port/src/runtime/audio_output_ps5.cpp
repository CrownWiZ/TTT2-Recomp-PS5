// PS5 audio output (interface: audio_output.h) — blocking sceAudioOut, no SDL.
//
// The SDL build paces the guest mixer with render credits handed back by the
// device callback. On the console the submission itself is the pacing point:
// sceAudioOutOutput blocks until the hardware has consumed the buffer, so a
// healthy submit costs roughly one frame period and the credit pool stays
// topped up by the same +1 rule the SDL completion callback applies. The
// six-channel guest mix downmixes to stereo S16 for the MAIN port, the same
// stereo shape SDL's stream produces on the desktop builds.
#include "audio_output.h"
#include "kernel/sce_ps5.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>

AudioPcmConversion ConvertAudioPcm(const uint8_t* guest,
                                   std::array<float, AUDIO_PCM_VALUES>& interleaved)
{
    AudioPcmConversion result;
    for (size_t sample = 0; sample < AUDIO_PCM_SAMPLES; ++sample)
        for (size_t channel = 0; channel < AUDIO_PCM_CHANNELS; ++channel)
        {
            const uint8_t* p = guest + (channel * AUDIO_PCM_SAMPLES + sample) * 4;
            const uint32_t bits = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                                  (uint32_t(p[2]) << 8) | uint32_t(p[3]);
            float value = __builtin_bit_cast(float, bits);
            if (!std::isfinite(value)) { value = 0; ++result.nonfinite; }
            result.nonzero |= value != 0;
            interleaved[sample * AUDIO_PCM_CHANNELS + channel] = value;
        }
    return result;
}

struct AudioOutputState
{
    explicit AudioOutputState(uint32_t id) : driver(id) { }
    mutable std::mutex mutex;
    const uint32_t driver;
    int32_t handle = -1;
    bool initialized = false;
    bool ready = false;
    bool closed = false;
    uint32_t renderCredits = AUDIO_OUTPUT_PRIME_FRAMES;
    uint64_t fallbackNextNs = 0;
    std::array<float, AUDIO_PCM_VALUES> staging{};
    std::chrono::steady_clock::time_point nextAllowedSubmit{};
    AudioOutputStats stats;
};

namespace {
// SDL's completion callback is the render-wakeup point on the desktop; a
// finished console submit plays the same role for WaitAudioOutputWake users.
struct RenderWake
{
    std::mutex mutex;
    std::condition_variable event;
    uint64_t version = 0;
};
RenderWake& RenderSignal() { static auto* signal = new RenderWake; return *signal; }
void NotifyRender()
{
    auto& signal = RenderSignal();
    { std::lock_guard lock(signal.mutex); ++signal.version; }
    signal.event.notify_one();
}
} // namespace

bool InitializeHostAudio()
{
    if (const char* e = std::getenv("TTT2_AUDIO_SILENT"); e && *e == '1') return true;
    if (sceAudioOutInit() != 0)
    {
        std::fprintf(stderr, "[snd-output] sceAudioOutInit failed; silent pacing\n");
        return false;
    }
    std::fprintf(stderr, "[snd-output] sceAudioOut: stereo S16 48000Hz, %zu frames per submit\n",
                 AUDIO_PCM_SAMPLES);
    return true;
}

uint64_t AudioOutputWakeVersion()
{
    auto& signal = RenderSignal();
    std::lock_guard lock(signal.mutex);
    return signal.version;
}

void WaitAudioOutputWake(uint64_t version, uint64_t timeoutNs)
{
    auto& signal = RenderSignal();
    std::unique_lock lock(signal.mutex);
    signal.event.wait_for(lock, std::chrono::nanoseconds(timeoutNs),
                          [&] { return signal.version != version; });
}

AudioOutputClient::AudioOutputClient(uint32_t driver)
    : state_(std::make_shared<AudioOutputState>(driver))
{
    std::lock_guard lock(state_->mutex);
    state_->initialized = true;
    int32_t userId = SCE_USER_SERVICE_USER_ID_INVALID;
    if (sceUserServiceGetInitialUser(&userId) != 0) userId = SCE_USER_SERVICE_USER_ID_INVALID;
    state_->handle = sceAudioOutOpen(userId, SCE_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                                     uint32_t(AUDIO_PCM_SAMPLES), 48000,
                                     SCE_AUDIO_OUT_MODE_STEREO);
    if (state_->handle >= 0)
    {
        state_->ready = true;
        std::fprintf(stderr, "[snd-output] driver %08X: sceAudioOut handle %d (user %d)\n",
                     driver, state_->handle, userId);
    }
    else
    {
        state_->stats.failed = true;
        std::fprintf(stderr, "[snd-output] driver %08X: sceAudioOutOpen failed; silent pacing\n", driver);
    }
    state_->stats.ready = state_->ready;
}

AudioOutputClient::~AudioOutputClient() { Close(); }

void AudioOutputClient::Close()
{
    std::lock_guard lock(state_->mutex);
    if (state_->closed || !state_->initialized) { state_->closed = true; return; }
    if (state_->handle >= 0)
    {
        sceAudioOutClose(state_->handle);
        state_->handle = -1;
    }
    state_->ready = false;
    state_->stats.ready = false;
    state_->closed = true;
}

void AudioOutputClient::Submit(const uint8_t* guest)
{
    std::lock_guard lock(state_->mutex);
    ++state_->stats.received;
    if (state_->closed)
    {
        ++state_->stats.dropped;
        return;
    }
    if (state_->stats.failed || !state_->ready)
    {
        ++state_->stats.dropped;
        NotifyRender();
        return;
    }
    const auto conversion = ConvertAudioPcm(guest, state_->staging);
    state_->stats.nonzero += conversion.nonzero;
    state_->stats.nonfinite += conversion.nonfinite;
    // Stereo downmix: front + 70% center + surround + half the LFE, clamped.
    std::array<int16_t, AUDIO_PCM_SAMPLES * 2> stereo{};
    for (size_t sample = 0; sample < AUDIO_PCM_SAMPLES; ++sample)
    {
        const float* src = state_->staging.data() + sample * AUDIO_PCM_CHANNELS;
        const float fl = src[0], fr = src[1], fc = src[2], lfe = src[3], bl = src[4], br = src[5];
        const double left  = double(fl) + 0.7 * double(fc) + 0.7 * double(bl) + 0.5 * double(lfe);
        const double right = double(fr) + 0.7 * double(fc) + 0.7 * double(br) + 0.5 * double(lfe);
        stereo[sample * 2]     = int16_t(std::clamp(left,  -32768.0, 32767.0));
        stereo[sample * 2 + 1] = int16_t(std::clamp(right, -32768.0, 32767.0));
    }
    ++state_->stats.submitted;
    const int result = sceAudioOutOutput(state_->handle, stereo.data());
    ++state_->stats.completed;
    state_->stats.deviceQueued = result >= 0 ? 0 : 1;
    if (result < 0) ++state_->stats.dropped;
    // The submit already consumed a frame period; top the credit pool up the
    // way the SDL completion callback does (capped at the prime size).
    state_->renderCredits = std::min<uint32_t>(state_->renderCredits + 1,
                                               AUDIO_OUTPUT_PRIME_FRAMES);
    NotifyRender();
}

bool AudioOutputClient::TryAcquireRenderCredit(uint64_t nowNs)
{
    std::lock_guard lock(state_->mutex);
    if (!state_->initialized) return true;
    if (state_->stats.failed)
    {
        if (nowNs < state_->fallbackNextNs) return false;
        state_->fallbackNextNs = nowNs + AUDIO_PCM_FRAME_NS;
        return true;
    }
    if (!state_->renderCredits) return false;
    --state_->renderCredits;
    return true;
}

AudioOutputStats AudioOutputClient::Stats() const
{
    std::lock_guard lock(state_->mutex);
    auto result = state_->stats;
    result.renderCredits = state_->renderCredits;
    return result;
}
