#!/usr/bin/env python3
"""Check deadline waiting and overshoot history with a deterministic clock."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = r'''
#include "host_deadline_wait.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace std::chrono_literals;
using Waiter = host_deadline::Waiter;
struct Timer {
    Waiter::Clock::time_point now{};
    Waiter::Duration overshoot{};
    std::vector<Waiter::Clock::time_point> sleeps;
    unsigned yields = 0;
    void Wait(Waiter& waiter, Waiter::Clock::time_point deadline, Waiter::Duration margin) {
        waiter.Wait(deadline, margin, [&] { return now; },
            [&](auto wake) { assert(wake > now); sleeps.push_back(wake); now = wake + overshoot; },
            [&] { now += 1us; ++yields; });
        assert(now >= deadline);
    }
};
int main() {
    unsigned checks = 0;
    for(auto limit : {0us, 1us, 100us, 500us, 750us, 1000us, 1500us}) {
        Waiter waiter;
        Timer timer;
        for(int i = 0; i < 300; ++i) {
            auto deadline = timer.now + 16667us;
            timer.overshoot = i % 7 == 0 ? 5000us : i % 3 == 0 ? 800us : 50us;
            timer.Wait(waiter, deadline, limit);
            assert(timer.sleeps.back() >= deadline - limit);
            assert(timer.sleeps.back() <= deadline);
            ++checks;
        }
    }
    {
        Waiter waiter;
        Timer timer;
        auto next = timer.now + 16667us;
        timer.Wait(waiter, next, 1500us);
        assert(timer.sleeps.back() == next - 1500us); ++checks;
        next += 16667us;
        timer.Wait(waiter, next, 1500us);
        assert(timer.sleeps.back() == next - 750us); ++checks;
        next += 16667us;
        timer.overshoot = 900us;
        timer.Wait(waiter, next, 1500us);
        timer.overshoot = 0us;
        for(int i = 0; i < 128; ++i) {
            next += 16667us;
            timer.Wait(waiter, next, 1500us);
            assert(timer.sleeps.back() == next - 1400us); ++checks;
        }
        next += 16667us;
        timer.Wait(waiter, next, 1500us);
        assert(timer.sleeps.back() == next - 750us); ++checks;
        next += 16667us;
        timer.Wait(waiter, next, 100us);
        assert(timer.sleeps.back() == next - 100us); ++checks;
        const auto slept = timer.sleeps.size(); const auto yielded = timer.yields;
        timer.Wait(waiter, timer.now - 1us, 1500us);
        assert(timer.sleeps.size() == slept && timer.yields == yielded); ++checks;
    }
    {
        Waiter waiter;
        Timer timer;
        timer.overshoot = -100us; // A spurious early wake still must wait to the deadline.
        timer.Wait(waiter, timer.now + 10000us, 1500us);
        auto deadline = timer.now + 10000us;
        timer.Wait(waiter, deadline, 1500us);
        assert(timer.sleeps.back() == deadline - 750us); ++checks;
    }
    {
        Waiter waiter;
        Timer timer;
        timer.Wait(waiter, timer.now + 100us, 1500us);
        assert(timer.sleeps.empty() && timer.yields == 100); ++checks;
    }
    std::printf("PASS %u deadline/history checks\n", checks);
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--emit-source", type=Path)
    args = parser.parse_args()
    if args.emit_source:
        args.emit_source.write_text(SOURCE)
        return
    with tempfile.TemporaryDirectory(prefix="deadline-wait-") as directory:
        source = Path(directory) / "check.cpp"
        binary = Path(directory) / "check"
        source.write_text(SOURCE)
        flags = ["-fsanitize=address,undefined", "-g"] if args.sanitize else []
        subprocess.run([args.cxx, "-std=c++20", "-O2", "-pthread", *flags,
                        "-I", str(ROOT / "port/src/runtime"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == "__main__":
    main()
