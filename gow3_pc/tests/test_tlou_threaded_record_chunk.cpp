// Actual old/new RecordChunks; modeled Scheduler handoff (mutex, recycling,
// 32 KiB kicks and frame-end drain), without a Vulkan driver or guest memory.
#include "tlou_record_chunks_both.h"
#include <barrier>
#include <condition_variable>
#include <deque>
#include <sched.h>
#include <thread>
#include <time.h>
#include <string>
#include <fstream>

using Clock=std::chrono::steady_clock;
static constexpr u64 Copies=7013;
static constexpr unsigned Frames=24;
static double CpuNs(){timespec ts{};assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts));return double(ts.tv_sec)*1e9+ts.tv_nsec;}
static void Pin(unsigned cpu){cpu_set_t set;CPU_ZERO(&set);CPU_SET(cpu,&set);assert(!pthread_setaffinity_np(pthread_self(),sizeof(set),&set));}
struct Context {
    std::atomic<u64> done{0};
    u64 count=0,sum=0,boundaries=0;
    unsigned bytes=0;
    std::vector<unsigned char> source,dest;
    explicit Context(unsigned n):bytes(n),source(Copies*n),dest(Copies*n,0){
        for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<unsigned char>(i*37+i/113);
    }
};
static void Copy(const Gow3Copy::Item& item){
    auto& c=*static_cast<Context*>(item.context);
    if(c.bytes)std::memcpy(reinterpret_cast<void*>(item.destination),reinterpret_cast<const void*>(item.source),item.size);
    ++c.count;c.sum+=item.extra;
}
static Gow3Copy::Item MakeItem(Context& c,u64 index){
    const auto offset=((index-1)%Copies)*c.bytes;
    return {.run=Copy,.context=&c,.source=c.bytes?reinterpret_cast<u64>(c.source.data()+offset):0,
            .destination=c.bytes?reinterpret_cast<u64>(c.dest.data()+offset):0,.size=c.bytes,.extra=index};
}
struct Measurement {double producer,consumer,wall;unsigned allocations;};
template<class Chunk,bool Batch> class Harness {
    std::mutex mutex;
    std::condition_variable ready_cv,idle_cv;
    std::deque<std::unique_ptr<Chunk>> ready;
    std::vector<std::unique_ptr<Chunk>> free;
    bool busy=false,stop=false,sleeping=false;
    std::atomic<unsigned> queued{0};
public:
    std::unique_ptr<Chunk> current;
    unsigned allocations=0;
    Harness(){for(unsigned i=0;i<32;++i)free.push_back(std::make_unique<Chunk>());current=Acquire();}
    std::unique_ptr<Chunk> Acquire(){
        std::lock_guard guard(mutex);
        if(free.empty()){++allocations;return std::make_unique<Chunk>();}
        auto result=std::move(free.back());free.pop_back();return result;
    }
    void Publish(){
        if(current->Empty())return;
        {std::lock_guard guard(mutex);ready.push_back(std::move(current));queued.store(ready.size(),std::memory_order_release);if(sleeping)ready_cv.notify_one();}
        current=Acquire();
    }
    template<class F> void Record(F&& f){
        if(!current->Push(std::forward<F>(f))){Publish();assert(current->Push(std::forward<F>(f)));}
    }
    void RecordCopy(const Gow3Copy::Item& item,u64 sequence,Context& c){
        if constexpr(Batch){
            if(!current->PushHostCopy(item,sequence,c.done)){Publish();assert(current->PushHostCopy(item,sequence,c.done));}
        }else{
            // Same nested item/copy closure and completion store as RecordHostCopy.
            auto copy=[item]{item.run(item);};
            Record([copy,&c,sequence](int){copy();c.done.store(sequence,std::memory_order_release);});
        }
    }
    void Kick(){if(current->Size()>=32*1024)Publish();}
    void Drain(){Publish();std::unique_lock guard(mutex);idle_cv.wait(guard,[&]{return ready.empty()&&!busy;});}
    void Stop(){std::lock_guard guard(mutex);stop=true;ready_cv.notify_one();}
    void Run(){
        for(;;){
            const auto until=Clock::now()+std::chrono::microseconds(20);
            for(unsigned spins=1;queued.load(std::memory_order_acquire)==0;++spins){
#if defined(__aarch64__)
                asm volatile("yield" ::: "memory");
#elif defined(__x86_64__)
                __builtin_ia32_pause();
#endif
                if(!(spins&255)&&Clock::now()>=until)break;
            }
            std::unique_ptr<Chunk> chunk;
            {std::unique_lock guard(mutex);sleeping=true;ready_cv.wait(guard,[&]{return stop||!ready.empty();});sleeping=false;
                if(ready.empty())return;
                chunk=std::move(ready.front());ready.pop_front();queued.store(ready.size(),std::memory_order_release);busy=true;
            }
            chunk->Execute(0);
            {std::lock_guard guard(mutex);free.push_back(std::move(chunk));busy=false;if(ready.empty())idle_cv.notify_all();}
        }
    }
};
template<bool Batch> static Measurement Sample(unsigned group,unsigned bytes,unsigned producer_cpu,unsigned consumer_cpu){
    using Chunk=std::conditional_t<Batch,Vulkan::RecordChunk,Vulkan::LegacyRecordChunk>;
    Context c(bytes);Harness<Chunk,Batch> h;
    std::barrier start(2);double consumer_cpu_ns=0;
    std::thread worker([&]{Pin(consumer_cpu);const auto at=CpuNs();start.arrive_and_wait();h.Run();consumer_cpu_ns=CpuNs()-at;});
    Pin(producer_cpu);
    const auto cpu_start=CpuNs(),wall_start=double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
    start.arrive_and_wait();
    for(unsigned frame=0;frame<Frames;++frame){
        for(u64 index=1;index<=Copies;++index){
            const auto sequence=frame*Copies+index;
            h.RecordCopy(MakeItem(c,sequence),sequence,c);
            if(index%group==0||index==Copies){
                h.Record([&c,sequence](int){assert(c.count==sequence);assert(c.done.load(std::memory_order_acquire)==sequence);++c.boundaries;});
                h.Kick();
            }
        }
        h.Drain();
        assert(c.done.load(std::memory_order_acquire)==(frame+1)*Copies);
    }
    const auto wall_end=double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
    const auto producer_cpu_ns=CpuNs()-cpu_start;
    h.Stop();worker.join();
    assert(c.count==Copies*Frames&&c.sum==(Copies*Frames)*(Copies*Frames+1)/2);
    assert(c.boundaries==Frames*((Copies+group-1)/group));assert(c.source==c.dest);
    return {producer_cpu_ns/(Copies*Frames),consumer_cpu_ns/(Copies*Frames),(wall_end-wall_start)/(Copies*Frames),h.allocations};
}
static std::pair<Measurement,Measurement> MedianPair(unsigned group,unsigned bytes,unsigned pcpu,unsigned ccpu){
    std::array<Measurement,7> old{},next{};
    for(unsigned i=0;i<8;++i){
        Measurement a,b;
        if(i%2){b=Sample<true>(group,bytes,pcpu,ccpu);a=Sample<false>(group,bytes,pcpu,ccpu);}
        else{a=Sample<false>(group,bytes,pcpu,ccpu);b=Sample<true>(group,bytes,pcpu,ccpu);}
        if(i){old[i-1]=a;next[i-1]=b;}
    }
    const auto less=[](const auto&a,const auto&b){return a.wall<b.wall;};
    std::sort(old.begin(),old.end(),less);std::sort(next.begin(),next.end(),less);return {old[3],next[3]};
}
int main(int argc,char**argv){
    cpu_set_t original;assert(!sched_getaffinity(0,sizeof(original),&original));std::vector<unsigned> cpus;
    for(unsigned i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&original))cpus.push_back(i);
    if(cpus.size()<2){std::puts("SKIP: two allowed CPUs required");return 0;}
    const auto core=[](unsigned cpu){
        const auto base="/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/topology/";
        std::ifstream p(base+"physical_package_id"),c(base+"core_id");std::string package,id;p>>package;c>>id;
        return package.empty()||id.empty()?std::string("unknown"):package+":"+id;
    };
    const auto first_core=core(cpus[0]);
    for(std::size_t i=1;i<cpus.size();++i)if(core(cpus[i])!="unknown"&&core(cpus[i])!=first_core){std::swap(cpus[1],cpus[i]);break;}
    std::printf("THREAD_PLACEMENT producer_cpu=%u core=%s consumer_cpu=%u core=%s allowed_cpus=%zu\n",cpus[0],core(cpus[0]).c_str(),cpus[1],core(cpus[1]).c_str(),cpus.size());
    const bool correctness=argc>1&&std::string(argv[1])=="--correctness-only";
    for(bool prefetch:{false,true}){
        Gow3Toggle::prefetch_enabled=prefetch;
        for(unsigned bytes:{0u,256u,2048u})for(unsigned group:{1u,2u,4u,8u,16u}){
            if(correctness){Sample<false>(group,bytes,cpus[0],cpus[1]);Sample<true>(group,bytes,cpus[0],cpus[1]);continue;}
            const auto [a,b]=MedianPair(group,bytes,cpus[0],cpus[1]);
            std::printf("THREADED prefetch=%u bytes=%u group=%u old_prod=%.2f new_prod=%.2f old_cons=%.2f new_cons=%.2f old_wall=%.2f new_wall=%.2f ns/copy; delta_prod_frame=%.4f delta_cons_frame=%.4f delta_wall_frame=%.4f ms; extra_chunks_old=%u new=%u\n",
                        prefetch,bytes,group,a.producer,b.producer,a.consumer,b.consumer,a.wall,b.wall,(a.producer-b.producer)*Copies/1e6,(a.consumer-b.consumer)*Copies/1e6,(a.wall-b.wall)*Copies/1e6,a.allocations,b.allocations);
            std::fflush(stdout);
        }
    }
    assert(!sched_setaffinity(0,sizeof(original),&original));
    std::puts("Cross-thread order, exact callback checksum, copied bytes, completion boundaries and chunk recycling PASS");
}
