// Exercise the production recorder with controlled sound calls, without a
// running guest, audio device, renderer, or network session.
#include "../src/runtime/kernel/sound_rollback.cpp"
#include <cassert>

uint8_t* g_base = nullptr;
namespace host_thread { uint32_t CurrentId() { return 1; } }
bool RollbackIsRewound(uint32_t) { return false; }

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
}
