// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

/** Private, atomic request/reply files for native system dialogs displayed by Android. */
namespace AndroidChoice {
constexpr std::size_t MaxBytes=1048576;
struct SaveEntry {
    std::string directory,title,subtitle,detail,icon;
    std::uint64_t timestamp=0;
    std::uint32_t user_param=0,is_new=0;
};
inline void Put32(std::vector<unsigned char>& out,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)out.push_back(static_cast<unsigned char>(value>>(8*i)));
}
inline void Put64(std::vector<unsigned char>& out,std::uint64_t value) {
    for(unsigned i=0;i<8;++i)out.push_back(static_cast<unsigned char>(value>>(8*i)));
}
inline std::uint32_t Get32(const unsigned char* in) {
    std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(in[i])<<(8*i);return value;
}
inline std::uint64_t Get64(const unsigned char* in) {
    std::uint64_t value=0;for(unsigned i=0;i<8;++i)value|=std::uint64_t(in[i])<<(8*i);return value;
}
class Bridge {
    std::string request,reply;
    std::uint64_t sequence=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uint64_t current=0;
    std::size_t count=0;
public:
    bool Begin(const std::string& directory,const std::string& title,const std::vector<std::string>& items,int focus,const std::vector<SaveEntry>& saves={}) {
        Finish();
        if(directory.empty() || items.empty() || items.size()>256 || title.size()>1024)return false;
        for(const auto& item:items)if(item.size()>2048)return false;
        request=directory+"/choice.request";reply=directory+"/choice.reply";
        current=++sequence;count=items.size();
        std::vector<unsigned char> data{'T','L','C','H','O','I','C','2'};
        Put64(data,current);Put32(data,static_cast<std::uint32_t>(count));
        Put32(data,focus>=0 && std::size_t(focus)<count?std::uint32_t(focus):0);
        Put32(data,static_cast<std::uint32_t>(title.size()));data.insert(data.end(),title.begin(),title.end());
        for(std::size_t i=0;i<items.size();++i) {
            const SaveEntry entry=i<saves.size()?saves[i]:SaveEntry{};
            auto text=[&](const std::string& value,std::size_t max)->bool {
                if(value.size()>max)return false;
                Put32(data,static_cast<std::uint32_t>(value.size()));data.insert(data.end(),value.begin(),value.end());return true;
            };
            if(!text(items[i],2048)||!text(entry.directory,32)||!text(entry.title,128)||!text(entry.subtitle,128)||
               !text(entry.detail,1024)||!text(entry.icon,1024)){Finish();return false;}
            Put64(data,entry.timestamp);Put32(data,entry.user_param);Put32(data,entry.is_new);
        }
        if(data.size()>MaxBytes){Finish();return false;}
        const std::string temp=request+".tmp";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.close();if(!out){std::remove(temp.c_str());Finish();return false;}}
        std::remove(reply.c_str());
        if(std::rename(temp.c_str(),request.c_str())!=0){std::remove(temp.c_str());Finish();return false;}
        return true;
    }
    int Poll() const {
        if(!current)return -1;
        std::array<unsigned char,21> bytes{};
        std::ifstream in(reply,std::ios::binary);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(in.gcount()!=20)return -1;
        const std::array<unsigned char,8> magic{'T','L','C','H','R','P','L','1'};
        for(unsigned i=0;i<8;++i)if(bytes[i]!=magic[i])return -1;
        if(Get64(bytes.data()+8)!=current)return -1;
        const auto choice=static_cast<std::int32_t>(Get32(bytes.data()+16));
        return choice==-2 || (choice>=0 && std::size_t(choice)<count)?choice:-1;
    }
    void Finish() {
        if(!request.empty())std::remove(request.c_str());
        if(!reply.empty())std::remove(reply.c_str());
        current=0;count=0;
    }
};
} // namespace AndroidChoice
