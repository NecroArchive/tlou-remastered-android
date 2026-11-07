#!/usr/bin/env python3
"""Compile the real worker policy/constructor: explicit zero creates NO threads."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'gpu/shadps4/video_core/renderer_vulkan/vk_draw_prep.cpp').read_text()
def function(signature):
    start=source.index(signature)
    at=source.index('{',start)
    if signature=='DrawPreparation::DrawPreparation(':
        at=source.rfind('{',start,source.index('    worker_count =',start))
    depth=1;end=at+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
code=r'''
#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
using u32=uint32_t;
namespace Gow3Threads { u32 Available(){return 8;} }
struct PipelineCache {};
struct DrawPreparation {
 PipelineCache& pipeline_cache;
 u32 worker_count=999;
 std::jthread scanner;
 std::vector<std::jthread> workers;
 std::condition_variable cv;
 DrawPreparation(PipelineCache&);
 ~DrawPreparation();
 void ScannerLoop(std::stop_token){}
 void WorkerLoop(std::stop_token,u32){}
};
'''+function('u32 DefaultWorkerCount()')+'\n'+function('DrawPreparation::DrawPreparation(')+'\n'+function('DrawPreparation::~DrawPreparation()')+r'''
int main() {
 PipelineCache cache;
 setenv("GOW3_PREP_WORKERS","0",1);
 { DrawPreparation draw(cache);assert(draw.worker_count==0);assert(draw.workers.empty());assert(!draw.scanner.joinable()); }
 unsetenv("GOW3_PREP_WORKERS");
 { DrawPreparation draw(cache);assert(draw.worker_count==4);assert(draw.workers.size()==4);assert(draw.scanner.joinable()); }
 setenv("GOW3_PREP_WORKERS","2",1);
 { DrawPreparation draw(cache);assert(draw.worker_count==2);assert(draw.workers.size()==2);assert(draw.scanner.joinable()); }
 puts("PASS: actual constructor: explicit zero=0 workers/0 scanner; absent=automatic; explicit positive respected");
}
'''
with tempfile.TemporaryDirectory(prefix='tlou-workers-') as temp:
    path=Path(temp)/'workers.cpp';path.write_text(code)
    binary=Path(temp)/'workers'
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-pthread',str(path),'-o',str(binary)],check=True)
    result=subprocess.check_output([str(binary)],text=True)
    assert 'workers: 0 (0 scanner,' in result
    assert 'workers: 4 (1 scanner,' in result
    print(result,end='')
