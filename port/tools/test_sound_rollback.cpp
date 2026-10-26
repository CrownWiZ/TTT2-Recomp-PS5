// Exercise the production recorder with controlled sound calls, without a
// running guest, audio device, renderer, or network session.
#include "../src/runtime/kernel/sound_rollback.cpp"
#include <algorithm>
#include <cassert>

uint8_t* g_base = nullptr;
namespace host_thread { uint32_t CurrentId() { return 1; } }
static bool trackSoundObject = false;
constexpr uint32_t soundObject = 0x100000, soundHandle = 0x200000;
static unsigned releaseCommands = 0;
bool RollbackIsRewound(uint32_t p) { return trackSoundObject && p >= soundObject && p < soundObject + 32; }
extern "C" PPC_FUNC(__imp__sub_82B1D318)
{
    assert(ctx.r3.u32 == soundHandle && ctx.r4.u32 == 0x72656C65);
    ++releaseCommands;
}

static PPCContext Context(double volume = 0.0)
{
    PPCContext ctx{};
    ctx.r3.u64 = 0x100000;
    ctx.lr = 0x826497E8;
    ctx.f1.f64 = volume;
    ctx.f2.f64 = 2.0;
    ctx.f3.f64 = 3.0;
    return ctx;
}

static void Reset()
{
    SoundRollbackFrame(~0ull, false);
    for (auto& frame : g_frames) frame = FrameCalls{};
}

static void TestMusicLifetime()
{
    std::vector<uint8_t> memory(soundHandle + 32);
    g_base = memory.data();
    trackSoundObject = true;
    // Compare both streamed-music APIs with an already tracked creation API.
    for (uint32_t address : {0x82B1B570u, 0x82B1B5F8u, 0x82B1CAC8u})
    {
        const int index = IndexOf(address);
        for (bool createdDuringReplay : {false, true})
        {
            Reset();
            std::fill(memory.begin(), memory.end(), 0);
            releaseCommands = 0;
            unsigned created = 0;
            auto context = [] {
                auto ctx = Context();
                ctx.r3.u64 = 0x110000; // bank, outside the rewound object
                ctx.r4.u64 = 1;
                ctx.r5.u64 = 32;
                ctx.r6.u64 = soundObject;
                ctx.lr = 0x82649344;
                return ctx;
            };
            auto create = [&](PPCContext& ctx, uint8_t* base) {
                ++created;
                GWrite32(base, ctx.r6.u32 + 4, soundHandle);
                GWrite32(base, soundHandle + 28, 2);
                ctx.r3.u64 = 0;
            };
            SoundRollbackFrame(1, false);
            auto ctx = context();
            if (createdDuringReplay) SoundRollbackFrame(1, true);
            Hook(index, ctx, g_base, create);
            assert(g_frames[1].calls[0].created == soundHandle);

            // A matching replay restores the handle without creating another
            // instance, and settling a retained call must not stop its music.
            GWrite32(g_base, soundObject + 4, 0);
            SoundRollbackFrame(1, true);
            ctx = context();
            Hook(index, ctx, g_base, create);
            assert(created == 1 && GRead32(g_base, soundObject + 4) == soundHandle);
            SoundRollbackSettle(ctx, g_base, 1, 1);
            assert(releaseCommands == 0 && GRead32(g_base, soundHandle + 28) == 2);

            // Omitting the call on a later corrected timeline releases the
            // instance once. Repeated settlement must not drop another ref.
            SoundRollbackFrame(1, true);
            SoundRollbackSettle(ctx, g_base, 1, 1);
            assert(releaseCommands == 1 && GRead32(g_base, soundHandle + 28) == 1);
            SoundRollbackSettle(ctx, g_base, 1, 1);
            assert(releaseCommands == 1 && GRead32(g_base, soundHandle + 28) == 1);

            // A failed creation that leaves an existing handle unchanged
            // must not claim it as new or release it on discard.
            Reset(); releaseCommands = 0;
            GWrite32(g_base, soundObject + 4, soundHandle);
            GWrite32(g_base, soundHandle + 28, 2);
            SoundRollbackFrame(2, false);
            ctx = context();
            Hook(index, ctx, g_base, [](PPCContext& c, uint8_t*) { c.r3.u64 = 0x80000004u; });
            assert(g_frames[2].calls[0].created == 0);
            SoundRollbackFrame(2, true);
            SoundRollbackSettle(ctx, g_base, 2, 2);
            assert(releaseCommands == 0 && GRead32(g_base, soundHandle + 28) == 2);
        }
    }
    Reset();
    trackSoundObject = false;
    g_base = nullptr;
}

int main()
{
    unsigned executed = 0;
    double volume = 0.0;
    auto setVolume = [&](PPCContext& ctx, uint8_t*) {
        ++executed;
        volume = ctx.f1.f64;
        ctx.r3.u64 = 0;
    };

    // A corrected timeline must be able to unmute the same object at the
    // same guest call site. Only the floating-point argument has changed.
    Reset();
    SoundRollbackFrame(1, false);
    auto ctx = Context(0.0);
    Hook(32, ctx, nullptr, setVolume);
    SoundRollbackFrame(1, true);
    ctx = Context(1.0);
    Hook(32, ctx, nullptr, setVolume);
    assert(executed == 2 && volume == 1.0);

    // That corrected call is recorded too, so another rewind does not post
    // it twice. Unused floating registers must not cause a mismatch.
    SoundRollbackFrame(1, true);
    ctx = Context(1.0); ctx.f2.f64 = 99.0;
    Hook(32, ctx, nullptr, setVolume);
    assert(executed == 2);

    // Every component matters for the four three-component sound setters;
    // the pitch setter, like volume, takes only f1.
    for (int index : {27, 28, 29, 46, 48})
    {
        for (int component = 0; component < (index == 46 ? 1 : 3); ++component)
        {
            Reset(); executed = 0;
            SoundRollbackFrame(2, false);
            ctx = Context(1.0);
            Hook(index, ctx, nullptr, setVolume);
            SoundRollbackFrame(2, true);
            ctx = Context(1.0);
            PPCRegister* args[] = {&ctx.f1, &ctx.f2, &ctx.f3};
            args[component]->f64 += 1.0;
            Hook(index, ctx, nullptr, setVolume);
            assert(executed == 2);
        }
    }

    // The gain conversions return f1, not just r3. A matched replay must
    // return the recorded conversion, not leave the input in f1.
    auto convert = [&](PPCContext& ctx, uint8_t*) {
        ++executed;
        ctx.f1.f64 *= 0.25;
        ctx.r3.u64 = 0;
    };
    for (int index : {18, 19})
    {
        Reset(); executed = 0;
        SoundRollbackFrame(3, false);
        ctx = Context(2.0);
        Hook(index, ctx, nullptr, convert);
        assert(ctx.f1.f64 == 0.5);
        SoundRollbackFrame(3, true);
        ctx = Context(2.0);
        Hook(index, ctx, nullptr, convert);
        assert(executed == 1 && ctx.f1.f64 == 0.5);
        SoundRollbackFrame(3, true);
        ctx = Context(4.0);
        Hook(index, ctx, nullptr, convert);
        assert(executed == 2 && ctx.f1.f64 == 1.0);
    }

    // Most sound queries have no floating arguments; keep deduplicating
    // those even if the caller leaves unrelated values in f1-f3.
    Reset(); executed = 0;
    SoundRollbackFrame(4, false);
    ctx = Context(1.0);
    Hook(44, ctx, nullptr, setVolume);
    SoundRollbackFrame(4, true);
    ctx = Context(9.0);
    Hook(44, ctx, nullptr, setVolume);
    assert(executed == 1);
    std::puts("sound rollback: changed volume/vector/pitch arguments, replayed gain results and duplicate suppression passed");
    TestMusicLifetime();
    std::puts("sound rollback: retained, discarded and failed music creations passed for live and replayed frames");
}
