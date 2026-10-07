// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_exploration.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <cstring>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>

namespace mc_exploration {
namespace {
int TileAt(int v) { return v >= 0 ? v / 16 : -static_cast<int>((15LL - v) / 16); }
std::uint64_t Hash(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
void U32(std::string& s, std::uint32_t n) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>(n >> (i * 8)));
}
struct Parser {
    const std::string& s; std::size_t at{};
    bool U32(std::uint32_t& n) {
        if (s.size() - at < 4) return false;
        n = 0;
        for (int i=0;i<4;++i) n |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[at++])) << (i*8);
        return true;
    }
    bool Bytes(void* out, std::size_t size) {
        if (size > s.size() - at) return false;
        std::memcpy(out, s.data()+at, size); at += size; return true;
    }
};
}
std::filesystem::path DefaultDirectory() {
#ifndef __ANDROID__
    if (const char* p = std::getenv("XDG_DATA_HOME"); p && std::filesystem::path(p).is_absolute())
        return std::filesystem::path(p) / "minecraft-companion";
    if (const char* p = std::getenv("HOME"); p && std::filesystem::path(p).is_absolute())
        return std::filesystem::path(p) / ".local/share/minecraft-companion";
#endif
    return {};
}
History::History(std::string key, int dim, std::filesystem::path dir)
    : world{std::move(key)}, dimension{dim}, directory{std::move(dir)} {
    if (directory.empty()) diagnostic = "History is session only; configure data_directory";
}
std::filesystem::path History::File() const {
    // Hash keeps filenames short and safe; the complete key is verified on loading, so even a
    // hash collision cannot join maps. Unknown/corrupt files are preserved, never overwritten.
    return directory / ("map-" + std::to_string(Hash(world)) + "-" + std::to_string(dimension) + ".mch");
}
bool History::Load() {
    if (directory.empty()) return true;
    const auto fail = [&] { blocked = true; diagnostic = "History file is invalid or unreadable; saved file preserved"; return false; };
    const int fd=::open(File().c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) { if (errno==ENOENT) return true; return fail(); }
    struct stat status{};
    constexpr std::size_t Maximum = MaxTiles * (1024+8) + MaxRoute * 12 + 2048;
    if (::fstat(fd,&status) || !S_ISREG(status.st_mode) || status.st_size<28 || static_cast<std::uint64_t>(status.st_size)>Maximum) {
        ::close(fd); return fail();
    }
    std::string bytes(static_cast<std::size_t>(status.st_size),'\0');
    std::size_t received{};
    while (received<bytes.size()) {
        const auto n=::read(fd,bytes.data()+received,bytes.size()-received);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { ::close(fd); return fail(); }
        received+=n;
    }
    ::close(fd);
    Parser p{bytes}; char magic[8]{};
    if (!p.Bytes(magic,8) || std::memcmp(magic,"MCHIST01",8)) return fail();
    std::uint32_t key_size{}, dim{}, count{}, points{}, checksum{};
    if (!p.U32(key_size) || key_size > 256 || key_size > bytes.size()-p.at) return fail();
    std::string key(key_size,'\0');
    if (!p.Bytes(key.data(),key_size) || key != world || !p.U32(dim) || dim != static_cast<unsigned>(dimension) ||
        !p.U32(count) || count > MaxTiles || !p.U32(points) || points > MaxRoute) return fail();
    std::map<std::pair<int,int>,Tile> loaded;
    for (std::uint32_t i=0;i<count;++i) {
        std::uint32_t x{},z{}; Tile tile{};
        if (!p.U32(x)||!p.U32(z)||!p.Bytes(tile.data(),tile.size())) return fail();
        const auto tx=static_cast<std::int32_t>(x), tz=static_cast<std::int32_t>(z);
        if (std::abs(static_cast<long long>(tx))>1875001 || std::abs(static_cast<long long>(tz))>1875001 ||
            !loaded.emplace(std::pair{tx,tz},tile).second) return fail();
    }
    std::vector<Point> loaded_route;
    for (std::uint32_t i=0;i<points;++i) {
        std::uint32_t x{},z{},start{};
        if (!p.U32(x)||!p.U32(z)||!p.U32(start)||start>1) return fail();
        const auto px=static_cast<std::int32_t>(x), pz=static_cast<std::int32_t>(z);
        if (std::abs(static_cast<long long>(px))>30000000 || std::abs(static_cast<long long>(pz))>30000000) return fail();
        loaded_route.push_back({px,pz,start!=0});
    }
    const auto checksum_at=p.at;
    if (!p.U32(checksum) || p.at!=bytes.size() || static_cast<std::uint32_t>(Hash(bytes.substr(0,checksum_at)))!=checksum) return fail();
    tiles=std::move(loaded); route=std::move(loaded_route); dirty=false; return true;
}
bool History::Save() {
    if (directory.empty() || blocked || !dirty) return !blocked;
    std::string bytes{"MCHIST01"}; U32(bytes,world.size()); bytes += world;
    U32(bytes,dimension); U32(bytes,tiles.size()); U32(bytes,route.size());
    for (const auto& [pos,tile]:tiles) { U32(bytes,pos.first); U32(bytes,pos.second); bytes.append(reinterpret_cast<const char*>(tile.data()),tile.size()); }
    for (const auto& point:route) { U32(bytes,point.x); U32(bytes,point.z); U32(bytes,point.start); }
    U32(bytes,static_cast<std::uint32_t>(Hash(bytes)));
    std::error_code ec; std::filesystem::create_directories(directory,ec);
    const auto fail = [&] { diagnostic="History could not be saved; retained in this session"; return false; };
    if (ec) return fail();
    const auto status=std::filesystem::symlink_status(File(),ec);
    if ((ec && ec!=std::errc::no_such_file_or_directory) ||
        (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))) return fail();
    static std::atomic<unsigned> sequence{};
    const auto temp=File().string()+".tmp-"+std::to_string(::getpid())+"-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
    const int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if (fd<0) return fail();
    std::size_t written{}; bool success=true;
    while (written<bytes.size()) {
        const auto n=::write(fd,bytes.data()+written,bytes.size()-written);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { success=false; break; }
        written+=n;
    }
    if (success && ::fsync(fd)) success=false;
    if (::close(fd)) success=false;
    if (success && ::rename(temp.c_str(),File().c_str())) success=false;
    if (!success) { ::unlink(temp.c_str()); return fail(); }
    dirty=false;
    if (diagnostic.starts_with("History could not")) diagnostic.clear();
    return true;
}
void History::Merge(int ox,int oz,const mc_assets::Image& image) {
    if (image.width>256 || image.height>256 || image.rgba.size()!=static_cast<std::size_t>(image.width)*image.height*4) return;
    for (unsigned j=0;j<image.height;++j) for (unsigned i=0;i<image.width;++i) {
        const auto* pixel=image.rgba.data()+(static_cast<std::size_t>(j)*image.width+i)*4;
        if (!pixel[3]) continue; // an unloaded or unreadable column never erases known terrain
        const int x=ox+static_cast<int>(i),z=oz+static_cast<int>(j);
        const std::pair pos{TileAt(x),TileAt(z)};
        auto it=tiles.find(pos);
        if (it==tiles.end()) {
            if (tiles.size()==MaxTiles) { diagnostic="History tile limit reached; existing terrain retained"; continue; }
            it=tiles.emplace(pos,Tile{}).first;
        }
        auto* stored=it->second.data()+((z&15)*16+(x&15))*4;
        if (std::memcmp(stored,pixel,4)) { std::memcpy(stored,pixel,4); dirty=true; }
    }
}
void History::Visit(int x,int z,bool start) {
    if (!route.empty() && !start && std::abs(static_cast<long long>(x)-route.back().x)<4 && std::abs(static_cast<long long>(z)-route.back().z)<4) return;
    if (route.size()==MaxRoute) { diagnostic="History route limit reached; existing routes retained"; return; }
    route.push_back({x,z,start || route.empty()}); dirty=true;
}
mc_assets::Image History::Draw(int ox,int oz,int size,bool routes) const {
    mc_assets::Image image; if (size<1 || size>256) return image;
    image.width=image.height=size; image.rgba.resize(static_cast<std::size_t>(size)*size*4);
    for (int j=0;j<size;++j) for (int i=0;i<size;++i) {
        const int x=ox+i,z=oz+j; const auto it=tiles.find({TileAt(x),TileAt(z)});
        if (it!=tiles.end()) std::memcpy(image.rgba.data()+(static_cast<std::size_t>(j)*size+i)*4,it->second.data()+((z&15)*16+(x&15))*4,4);
    }
    if (routes) for (std::size_t i=0;i<route.size();++i) {
        const auto& p=route[i]; int x=p.x-ox,z=p.z-oz;
        const auto& previous=(i && !p.start)?route[i-1]:p;
        const int tx=previous.x-ox,tz=previous.z-oz;
        // Teleports are separate route segments; cap work and never draw a false huge traverse.
        if (std::abs(x-tx)>512 || std::abs(z-tz)>512) continue;
        const int dx=std::abs(tx-x), sx=x<tx?1:-1, dz=-std::abs(tz-z), sz=z<tz?1:-1;
        int error=dx+dz;
        for (;;) {
            if (x>=0&&z>=0&&x<size&&z<size) { auto* px=image.rgba.data()+(static_cast<std::size_t>(z)*size+x)*4; px[0]=255;px[1]=214;px[2]=95;px[3]=255; }
            if (x==tx&&z==tz) break;
            const int e=2*error; if (e>=dz) {error+=dz;x+=sx;} if (e<=dx) {error+=dx;z+=sz;}
        }
    }
    return image;
}
}
