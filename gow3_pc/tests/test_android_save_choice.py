#!/usr/bin/env python3
"""Run the actual Android overlay/IPC path: select, cancel, stale/invalid replies, pad fallback."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'gpu/shim/gow3_overlay.cpp').read_text().split('#else // !__ANDROID__')[0]
source=source.replace('#include "gow3_overlay.h"','')
prefix=r'''
#include <cstdint>
#include <string>
#include <vector>
#include <cassert>
#include <filesystem>
#include <cstring>
#include "gow3gpu.h"
#include "android_choice_bridge.h"
using u32=uint32_t;
namespace Vulkan {struct Instance{};}
namespace vk {using Format=int;struct CommandBuffer{};struct ImageView{};struct Extent2D{};}
namespace Gow3Png {struct Image{};}
struct SDL_Window{};union SDL_Event{};
#define __ANDROID__ 1
'''
suffix=r'''
#endif
static uint64_t request_id(const std::string& dir) {
 unsigned char header[16]{};std::ifstream in(dir+"/choice.request",std::ios::binary);in.read(reinterpret_cast<char*>(header),16);
 assert(in.gcount()==16 && std::memcmp(header,"TLCHOIC2",8)==0);return AndroidChoice::Get64(header+8);
}
static void reply(const std::string& dir,uint64_t id,int choice) {
 std::vector<unsigned char> bytes{'T','L','C','H','R','P','L','1'};
 AndroidChoice::Put64(bytes,id);AndroidChoice::Put32(bytes,static_cast<uint32_t>(choice));
 std::ofstream out(dir+"/choice.reply",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
int main(int argc,char** argv) {
 assert(argc==3);std::string dir=argv[1];setenv("GOW3_GPU_USER_DIR",dir.c_str(),1);
 Gow3SaveChoiceEntry entries[2]={{"SAVE0001","The Last of Us","The Outskirts","Actual checkpoint","/tmp/first.png",1700000000,42,0},
 {"SAVE0002","The Last of Us","Bill's Town","Second checkpoint","",1700000100,43,0}};
 Gow3Overlay::BeginSaveChoice("Load game",entries,2,0);
 assert(Gow3Overlay::CapturesInput());uint64_t id=request_id(dir);
 std::filesystem::copy_file(dir+"/choice.request",argv[2],std::filesystem::copy_options::overwrite_existing);
 reply(dir,id-1,1);assert(Gow3Overlay::PollChoice()==-1);
 reply(dir,id,99);assert(Gow3Overlay::PollChoice()==-1);
 reply(dir,id,1);assert(Gow3Overlay::PollChoice()==1);assert(!Gow3Overlay::CapturesInput());
 assert(!std::filesystem::exists(dir+"/choice.request"));
 Gow3Overlay::BeginSaveChoice("Load game",entries,2,0);uint64_t next=request_id(dir);assert(next!=id);
 reply(dir,id,0);assert(Gow3Overlay::PollChoice()==-1);
 reply(dir,next,-2);assert(Gow3Overlay::PollChoice()==-2);assert(!Gow3Overlay::CapturesInput());
 Gow3Overlay::PublishPad(0);Gow3Overlay::BeginSaveChoice("Load game",entries,2,1);
 Gow3Overlay::PublishPad(0x10);assert(Gow3Overlay::PollChoice()==-1);
 Gow3Overlay::PublishPad(0);assert(Gow3Overlay::PollChoice()==-1);
 Gow3Overlay::PublishPad(0x4000);assert(Gow3Overlay::PollChoice()==0);
 assert(!Gow3Overlay::CapturesInput());
 Gow3Overlay::PublishPad(0);Gow3Overlay::BeginSaveChoice("Load game",entries,2,0);
 Gow3Overlay::PublishPad(0x2000);assert(Gow3Overlay::PollChoice()==-2);
 Gow3Overlay::PublishPad(0);Gow3Overlay::BeginSaveChoice("Load game",entries,2,0);
 Gow3Overlay::CancelSaveChoice();assert(!Gow3Overlay::CapturesInput());assert(!std::filesystem::exists(dir+"/choice.request"));
 std::puts("PASS: actual Android save selection, cancellation, generation isolation, bounds and controller fallback");
}
'''
with tempfile.TemporaryDirectory(prefix='tlou-choice-') as temp:
    p=Path(temp)/'overlay.cpp';p.write_text(prefix+source+suffix)
    binary=Path(temp)/'choice'
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-pthread','-I',str(root/'gpu'),'-I',str(root/'gpu/shim'),str(p),'-o',str(binary)],check=True)
    fixture=root.parent/'android-launcher/app/build/native-save-choice.bin'
    fixture.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(binary),temp,str(fixture)],check=True)
