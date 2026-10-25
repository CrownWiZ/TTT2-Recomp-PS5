#!/usr/bin/env python3
"""Compare the worker wait hook against generated PPC code, without running the game.

Requires generated sources (--game-source); they are read, never modified.
Checks full register/memory parity and critical-section/semaphore calls, including
signed counter boundaries and counters released by another thread. Also measures
consumer CPU time and release latency in a controlled blocked-call workload.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = r'''
#include "@HOOK@"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>
#include <ctime>
#include <algorithm>
using namespace std::chrono_literals;
static constexpr uint32_t pool = 256;
struct Call { uint64_t kind, a, b, c, active, barriers; bool operator==(const Call&) const = default; };
static std::vector<Call> calls;
static bool trySuccess;
static void Note(unsigned kind, PPCContext& ctx, uint8_t* base) {
    calls.push_back({kind,ctx.r3.u64,ctx.r4.u64,ctx.r5.u64,GRead32(base,pool+8),GRead32(base,pool+212)});
}
PPC_FUNC(__imp__RtlTryEnterCriticalSection) { Note(0,ctx,base); ctx.r3.u64=trySuccess; }
PPC_FUNC(__imp__RtlEnterCriticalSection) { Note(1,ctx,base); GWrite32(base,pool+56,1); ctx.r3.u64=0; }
PPC_FUNC(__imp__RtlLeaveCriticalSection) { Note(2,ctx,base); ctx.r3.u64=0; }
PPC_FUNC(sub_82BEE7A0) { Note(3,ctx,base); ctx.r3.u64=0x12345678; }
@GENERATED@
struct Snapshot {
    PPCContext ctx{};
    alignas(32) std::array<uint8_t,16384> memory{};
    std::vector<Call> trace;
    double cpuUs=0, lagUs=0;
};
static double CpuUs() {
    timespec t{}; assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0);
    return t.tv_sec*1e6 + t.tv_nsec*.001;
}
static Snapshot Run(bool native, int active, int barriers, int releaseField, int released, bool success) {
    Snapshot result;
    auto* base=result.memory.data();
    // Distinct stack/register contents catch accidental clobbers in the hook.
    std::fill(result.memory.begin(),result.memory.end(),uint8_t(0xa5));
    result.ctx.r1.u64=8192; result.ctx.r3.u64=pool; result.ctx.r4.u64=0xfedcba9876543210ull;
    result.ctx.r29.u64=0x1020304050607080ull; result.ctx.r30.u64=0x8877665544332211ull;
    result.ctx.r31.u64=0x87654321abcdef01ull; result.ctx.lr=0x81234560;
    GWrite32(base,pool+8,uint32_t(active)); GWrite32(base,pool+212,uint32_t(barriers));
    GWrite32(base,pool+204,0x1234); GWrite32(base,pool+56,0);
    trySuccess=success; calls.clear();
    std::atomic<bool> entered{false};
    std::atomic<int64_t> releasedAt{0};
    std::thread producer;
    if (releaseField) producer=std::thread([&] {
        while(!entered.load(std::memory_order_acquire)) std::this_thread::yield();
        std::this_thread::sleep_for(3ms);
        releasedAt.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),std::memory_order_release);
        // Emulate the generated guest's volatile store to the shared counter.
        *reinterpret_cast<volatile uint32_t*>(base+pool+releaseField)=__builtin_bswap32(uint32_t(released));
    });
    const double cpu=CpuUs();
    entered.store(true,std::memory_order_release);
    if(native) sub_82AD0040(result.ctx,base); else __imp__sub_82AD0040(result.ctx,base);
    result.cpuUs=CpuUs()-cpu;
    const auto finished=std::chrono::steady_clock::now();
    if(producer.joinable()) producer.join();
    const auto release=releasedAt.load(std::memory_order_acquire);
    if(release) result.lagUs=(std::chrono::duration_cast<std::chrono::nanoseconds>(finished.time_since_epoch()).count()-release)*.001;
    result.trace=calls;
    return result;
}
int main() {
    unsigned checks=0;
    const std::array<std::array<int,4>,10> cases{{
        {0,0,0,0},{0,1,0,0},{1,1,0,0},{-1,0,0,0},
        {INT32_MIN,INT32_MAX,0,0},{INT32_MIN,INT32_MIN,0,0},{0,INT32_MAX,0,0},
        {3,1,8,1},{2,0,212,2},{0,-1,212,0}
    }};
    for(bool success:{false,true}) for(auto values:cases) {
        auto reference=Run(false,values[0],values[1],values[2],values[3],success);
        auto candidate=Run(true,values[0],values[1],values[2],values[3],success);
        assert(std::memcmp(&reference.ctx,&candidate.ctx,sizeof(PPCContext))==0);
        assert(reference.memory==candidate.memory);
        assert(reference.trace==candidate.trace);
        // Signaling happens only after observing the released predicate.
        for(auto c:candidate.trace) if(c.kind==3) assert(int32_t(c.active)<=int32_t(uint32_t(c.barriers)+1u));
        ++checks;
    }
    double originalCpu=0,nativeCpu=0;
    std::vector<double> originalLag,nativeLag;
    for(int i=0;i<64;++i) {
        // Reverse the order of alternating pairs to reduce order bias.
        for(bool native : {bool(i&1),!bool(i&1)}) {
            auto r=Run(native,3,1,8,1,true);
            (native?nativeCpu:originalCpu)+=r.cpuUs;
            (native?nativeLag:originalLag).push_back(r.lagUs);
        }
    }
    std::sort(originalLag.begin(),originalLag.end()); std::sort(nativeLag.begin(),nativeLag.end());
    std::printf("PASS parity=%u original_cpu_ms=%.3f candidate_cpu_ms=%.3f reduction=%.1f%% original_lag_p95_us=%.3f candidate_lag_p95_us=%.3f\n",
        checks,originalCpu/1000,nativeCpu/1000,100*(1-nativeCpu/originalCpu),originalLag[60],nativeLag[60]);
}
'''


def function(source, name, alias=False):
    pattern = (r'__attribute__\(\(alias\("' + re.escape(name) + r'"\)\)\).*?\n' if alias else '')
    pattern += r'PPC_FUNC_IMPL\(' + re.escape(name) + r'\) \{.*?^\}'
    match = re.search(pattern, source, re.M | re.S)
    if not match:
        raise RuntimeError(f'Missing generated function: {name}')
    return match.group()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-source',type=Path,required=True)
    parser.add_argument('--compiler',default='clang++')
    args=parser.parse_args()
    game=args.game_source.resolve(); ppc=game/'re/ppc'; recomp=game/'tools/XenonRecomp'
    generated=function((ppc/'ppc_recomp.232.cpp').read_text(),'__imp__sub_82AD0040')
    saves=(ppc/'ppc_recomp.263.cpp').read_text()
    generated=function(saves,'__imp____savegprlr_29',True)+'\n'+function(saves,'__imp____restgprlr_29',True)+'\n'+generated
    with tempfile.TemporaryDirectory(prefix='ttt2-worker-wait-') as temp:
        path=Path(temp); source=path/'test.cpp'; exe=path/'test'
        source.write_text(CHECKER.replace('@HOOK@',str(ROOT/'port/src/runtime/kernel/worker_barrier_wait.cpp')).replace('@GENERATED@',generated))
        subprocess.run([args.compiler,'-O2','-std=c++20','-pthread','-msse4.1',
                        '-I'+str(ppc),'-I'+str(recomp/'XenonUtils'),'-I'+str(recomp/'thirdparty/simde'),
                        str(source),'-o',str(exe)],check=True)
        for mode in (0,-1,10,50,100):
            env=dict(os.environ,TTT2_WORKER_BARRIER_WAIT_US=str(mode))
            env.pop('TTT2_WORKER_BARRIER_WAIT_FILE',None); env.pop('TTT2_WORKER_BARRIER_PROFILE',None)
            print('mode',mode,flush=True)
            subprocess.run([str(exe)],env=env,check=True,timeout=15)

if __name__=='__main__': main()
