// SPDX-License-Identifier: GPL-2.0-or-later
// ARM64 multicore screening of release/acquire mappings. Statistical testing
// cannot prove all x86-TSO behaviours or validate mixed non-atomic SIMD code.
#include <atomic>
#include <barrier>
#include <cassert>
#include <cstdio>
#include <thread>
#if !defined(__aarch64__)
#error "ARM64-only memory litmus"
#endif
constexpr unsigned Repeats=35000;
void MessagePassing() {
    std::atomic<int> data{0},flag{0};
    std::barrier gate(3),done(3);
    int bad=0,observed=0;
    std::jthread producer([&]{
        for(unsigned i=0;i<Repeats;++i){
            gate.arrive_and_wait();
            data.store(1,std::memory_order_relaxed);
            flag.store(1,std::memory_order_release);
            done.arrive_and_wait();
        }
    });
    std::jthread consumer([&]{
        for(unsigned i=0;i<Repeats;++i){
            gate.arrive_and_wait();
            if(flag.load(std::memory_order_acquire)==1){
                ++observed;
                if(data.load(std::memory_order_relaxed)!=1)++bad;
            }
            done.arrive_and_wait();
        }
    });
    for(unsigned i=0;i<Repeats;++i){
        data.store(0,std::memory_order_relaxed);
        flag.store(0,std::memory_order_relaxed);
        gate.arrive_and_wait();done.arrive_and_wait();
    }
    assert(bad==0);
    std::printf("TLOU TSO MP ARM64: rounds=%u observed=%d forbidden=%d PASS\n",
                Repeats,observed,bad);
}
void LoadBuffering() {
    std::atomic<int> x{0},y{0};
    std::barrier gate(3),done(3);
    int a=0,b=0,bad=0;
    std::jthread t0([&]{
        for(unsigned i=0;i<Repeats;++i){
            gate.arrive_and_wait();
            a=y.load(std::memory_order_acquire);
            x.store(1,std::memory_order_release);
            done.arrive_and_wait();
        }
    });
    std::jthread t1([&]{
        for(unsigned i=0;i<Repeats;++i){
            gate.arrive_and_wait();
            b=x.load(std::memory_order_acquire);
            y.store(1,std::memory_order_release);
            done.arrive_and_wait();
        }
    });
    for(unsigned i=0;i<Repeats;++i){
        x.store(0,std::memory_order_relaxed);
        y.store(0,std::memory_order_relaxed);
        gate.arrive_and_wait();done.arrive_and_wait();
        bad+=(a==1&&b==1);
    }
    assert(bad==0);
    std::printf("TLOU TSO LB ARM64: rounds=%u forbidden=%d PASS\n",Repeats,bad);
}
void StoreBuffering() {
    std::atomic<int> x{0},y{0};
    std::barrier gate(3),done(3);
    int a=0,b=0,zeros=0;
    std::jthread t0([&]{
        for(unsigned i=0;i<Repeats;++i){
            gate.arrive_and_wait();
            x.store(1,std::memory_order_release);
            a=y.load(std::memory_order_acquire);
            done.arrive_and_wait();
        }
    });
    std::jthread t1([&]{
        for(unsigned i=0;i<Repeats;++i){
            gate.arrive_and_wait();
            y.store(1,std::memory_order_release);
            b=x.load(std::memory_order_acquire);
            done.arrive_and_wait();
        }
    });
    for(unsigned i=0;i<Repeats;++i){
        x.store(0,std::memory_order_relaxed);
        y.store(0,std::memory_order_relaxed);
        gate.arrive_and_wait();done.arrive_and_wait();
        zeros+=(a==0&&b==0);
    }
    // x86-TSO permits store buffering [0,0]. Never reject over-strong
    // mappings, but report the outcome for later model comparison.
    std::printf("TLOU TSO SB ARM64: rounds=%u x86-allowed-0-0=%d observational\n",
                Repeats,zeros);
}
void IRIW() {
    std::atomic<int> x{0},y{0};
    std::barrier gate(5),done(5);
    int a=0,b=0,c=0,d=0,forbidden=0;
    std::jthread writerX([&]{for(unsigned i=0;i<Repeats;++i){
        gate.arrive_and_wait();x.store(1,std::memory_order_release);done.arrive_and_wait();}});
    std::jthread writerY([&]{for(unsigned i=0;i<Repeats;++i){
        gate.arrive_and_wait();y.store(1,std::memory_order_release);done.arrive_and_wait();}});
    std::jthread readerXY([&]{for(unsigned i=0;i<Repeats;++i){
        gate.arrive_and_wait();a=x.load(std::memory_order_acquire);
        b=y.load(std::memory_order_acquire);done.arrive_and_wait();}});
    std::jthread readerYX([&]{for(unsigned i=0;i<Repeats;++i){
        gate.arrive_and_wait();c=y.load(std::memory_order_acquire);
        d=x.load(std::memory_order_acquire);done.arrive_and_wait();}});
    for(unsigned i=0;i<Repeats;++i){
        x.store(0,std::memory_order_relaxed);y.store(0,std::memory_order_relaxed);
        gate.arrive_and_wait();done.arrive_and_wait();
        forbidden+=(a==1&&b==0&&c==1&&d==0);
    }
    assert(forbidden==0);
    std::printf("TLOU TSO IRIW ARM64: rounds=%u forbidden=%d PASS\n",Repeats,forbidden);
}
int main() {
    MessagePassing();
    LoadBuffering();
    StoreBuffering();
    IRIW();
}
