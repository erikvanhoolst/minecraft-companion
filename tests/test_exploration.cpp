// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_exploration.h"
#include "mc_map.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <thread>

void Check(bool ok,const char* message) { if (!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); } }
using mc_exploration::History;
mc_assets::Image Solid(int size=16) { mc_assets::Image i; i.width=i.height=size; i.rgba.resize(size*size*4); for (std::size_t j=0;j<i.rgba.size();j+=4) {i.rgba[j]=10;i.rgba[j+1]=20;i.rgba[j+2]=30;i.rgba[j+3]=255;} return i; }
struct Fixture {
    static constexpr std::uint64_t Base=0x1000000000ULL;
    std::vector<std::uint8_t> memory=std::vector<std::uint8_t>(0x10000);
    EdenDsmodHostApi host{};
    std::map<std::string,std::int64_t> ints;
    std::map<std::string,std::string> texts;
    mc_reader::Layout layout{};
    std::mutex mutex;
    std::condition_variable wake;
    bool pause{}, entered{}, resume{};
    Fixture() {
        layout.player_chunk_views=0x10; layout.view_bounds=0x20; layout.view_chunks=0x50;
        layout.chunk_pos=0; layout.chunk_heightmap=0x20; layout.chunk_subchunks=0x240;
        host.userdata=this;
        host.publish_i64=[](void* p,const char* k,std::int64_t v){static_cast<Fixture*>(p)->ints[k]=v;};
        host.publish_f64=[](void*,const char*,double){};
        host.publish_text=[](void* p,const char* k,const char* v){static_cast<Fixture*>(p)->texts[k]=v;};
        host.is_mapped=[](void* p,std::uint64_t at,std::uint64_t size)->EdenDsmodBool {auto& f=*static_cast<Fixture*>(p); return at>=Base && at-Base<=f.memory.size() && size<=f.memory.size()-(at-Base);};
        host.read_memory=[](void* p,std::uint64_t at,void* out,std::size_t size)->EdenDsmodBool {
            auto& f=*static_cast<Fixture*>(p);
            if (at==Base+0x110) {
                std::unique_lock lock{f.mutex};
                if (f.pause) { f.entered=true; f.wake.notify_all(); f.wake.wait(lock,[&]{return f.resume;}); }
            }
            std::memcpy(out,f.memory.data()+at-Base,size); return true;
        };
        Put<std::uint64_t>(0x110,Base+0x200); Put<std::uint64_t>(0x118,0);
        const std::int32_t bounds[9]={0,0,0,0,0,0,1,1,1}; Bytes(0x220,bounds,sizeof(bounds));
        Put<std::uint64_t>(0x250,Base+0x400); Put<std::uint64_t>(0x258,Base+0x410);
        Put<std::uint64_t>(0x400,Base+0x1000);
        for (unsigned i=0;i<256;++i) Put<std::uint16_t>(0x1020+i*2,1);
        Put<std::uint64_t>(0x1240,Base+0x3000);
        std::memset(memory.data()+0x3000,1,4096);
    }
    void Bytes(std::size_t offset,const void* data,std::size_t size) {std::memcpy(memory.data()+offset,data,size);}
    template<class T> void Put(std::size_t offset,T value) { Bytes(offset,&value,sizeof(value)); }
};
namespace mc_map {
struct MapTestAccess {
    static bool Draw(Map& map,Fixture& f,std::string world,int dimension,std::uint64_t serial,bool fresh=true) {
        Map::Request r; r.serial=serial;r.world=std::move(world);r.dimension=dimension;r.scoped=true;r.view=16;r.player=Fixture::Base+0x100;r.layout=&f.layout;r.colors[1]=0x112233;r.player_x=r.player_z=1000;
        { std::lock_guard lock{map.mutex}; r.generation=map.generation; map.pending=r; }
        if (!fresh) map.Invalidate();
        return map.DrawRequest(f.host,r);
    }
    static void Start(Map& map,Fixture& f) {
        Map::Request r;r.serial=1;r.view=16;r.player=Fixture::Base+0x100;r.layout=&f.layout;r.colors[1]=0x112233;r.world="Auto";r.scoped=true;r.player_x=r.player_z=1000;
        std::lock_guard lock{map.mutex};r.generation=map.generation;map.pending=r;map.worker_host=f.host;
        map.worker=std::thread{&Map::Worker,&map};map.wake.notify_one();
    }
    static void Invalidate(Map& map) { map.Invalidate(); }
    static void Publish(Map& map,Fixture& f,bool player=true) {
        map.serial=std::max<std::uint64_t>(map.serial,1);map.player_was_ready=player;
        map.Publish(f.host,player,Map::Place{});
    }
    static bool Await(Map& map) {
        std::unique_lock lock{map.mutex};
        return map.wake.wait_for(lock,std::chrono::seconds(2),[&]{return map.pictures[0].serial!=0;});
    }
};
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("mc-exploration-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto terrain=Solid();
    History a("Village",0,root); Check(a.Load(),"new store loads empty");
    a.Merge(-16,-16,terrain); Check(a.TileCount()==1,"negative positions share correct tile");
    auto unknown=terrain; std::fill(unknown.rgba.begin(),unknown.rgba.end(),0);
    a.Merge(-16,-16,unknown);
    Check(a.Draw(-16,-16,16,false).rgba==terrain.rgba,"unloaded terrain never erases recorded pixels");
    a.Visit(-12,-12,true);a.Visit(-8,-12,false);a.Visit(-4,-12,true);
    const auto drawn=a.Draw(-16,-16,16);
    Check(drawn.rgba[(4*16+6)*4]==255,"old route is visible over retained terrain");
    Check(drawn.rgba[(4*16+10)*4]==10,"disconnected route points never join");
    Check(a.Save(),"valid history saves");
    History reloaded("Village",0,root); Check(reloaded.Load(),"saved history reloads");
    Check(reloaded.Draw(-16,-16,16).rgba==drawn.rgba && reloaded.RouteCount()==3,"terrain and routes survive module reload");
    History dim("Village",1,root), other("Other",0,root);
    Check(dim.Load()&&other.Load()&&dim.TileCount()==0&&other.TileCount()==0,"world and dimension stores are isolated");
    Check(dim.File()!=a.File()&&other.File()!=a.File(),"scope filenames are distinct");
    {std::fstream f(a.File(),std::ios::in|std::ios::out|std::ios::binary);f.seekp(42);char bad=5;f.write(&bad,1);}
    const auto corrupt_size=std::filesystem::file_size(a.File());
    History corrupt("Village",0,root);Check(!corrupt.Load()&&corrupt.TileCount()==0,"checksum rejects corruption without partial terrain");
    corrupt.Merge(0,0,terrain);Check(!corrupt.Save()&&std::filesystem::file_size(a.File())==corrupt_size,"corrupt saved file is never overwritten");
    History wrong("Identity",0,root);std::filesystem::copy_file(reloaded.File(),wrong.File());
    Check(!wrong.Load(),"file identity must match selected world");
    const auto outside=root/"outside"; {std::ofstream f(outside);f<<"keep";}
    History symlink("Symlink",0,root);std::filesystem::create_symlink(outside,symlink.File());
    Check(!symlink.Load(),"history refuses symlink source files");
    History symlink_save("Symlink",0,root);symlink_save.Merge(0,0,terrain);
    Check(!symlink_save.Save()&&std::filesystem::file_size(outside)==4,"history refuses symlink destination without modifying its target");
    History bounded("Bounded",0);
    for(std::size_t i=0;i<History::MaxTiles+1;++i) bounded.Merge(static_cast<int>(i)*16,0,Solid(1));
    Check(bounded.TileCount()==History::MaxTiles,"tile memory is bounded without losing existing discovery");
    for(std::size_t i=0;i<History::MaxRoute+1;++i) bounded.Visit(static_cast<int>(i)*4,0,true);
    Check(bounded.RouteCount()==History::MaxRoute&&!bounded.Diagnostic().empty(),"route limit is bounded and reported");
    const auto session_config=std::string{"{\"world_key\":\"A\",\"data_directory\":\"\"}"};
    Fixture f;
    {
        mc_map::Map identity("{\"world_keys\":[\"A\",\"B\"],\"world_key\":\"A\",\"data_directory\":\"\"}");
        mc_map::MapTestAccess::Publish(identity,f);
        Check(f.ints["world.ready"]==0&&f.texts["world.key"]=="A","startup identity requires explicit confirmation");
        Check(identity.OnAction("world_confirm",0),"world confirmation succeeds for a present player");
        mc_map::MapTestAccess::Publish(identity,f);
        Check(f.ints["world.ready"]==1,"confirmed scope publishes ready for waypoints");
        identity.OnAction("world_next",0);mc_map::MapTestAccess::Publish(identity,f);
        Check(f.ints["world.ready"]==0&&f.texts["world.key"]=="B","changing world immediately requires fresh confirmation");
        identity.OnAction("world_dimension",0);mc_map::MapTestAccess::Publish(identity,f);
        Check(f.ints["world.dimension"]==1&&f.ints["world.ready"]==0,"changing dimension keeps scope unconfirmed");
        mc_map::MapTestAccess::Publish(identity,f,false);
        Check(!identity.OnAction("world_confirm",0),"a missing player cannot confirm an identity");
    }
    {
        mc_map::Map map(session_config.c_str());
        Check(mc_map::MapTestAccess::Draw(map,f,"A",0,1),"loaded chunk draws successfully");
        const auto live=map.Load(f.host,"mapview/1");Check(live&&live->rgba[3]==255,"chunk terrain is captured");
        f.Put<std::uint64_t>(0x400,0);
        Check(mc_map::MapTestAccess::Draw(map,f,"A",0,2),"history renders without any loaded chunks");
        const auto saved=map.Load(f.host,"mapview/2");Check(saved&&saved->rgba==live->rgba,"unloaded map preserves prior terrain");
        Check(mc_map::MapTestAccess::Draw(map,f,"B",0,3),"other world has separate view");
        const auto b=map.Load(f.host,"mapview/3");Check(b&&b->rgba[3]==0,"other world never borrows previous pixels");
        Check(mc_map::MapTestAccess::Draw(map,f,"A",0,4),"session store restores when revisiting world");
        Check(map.Load(f.host,"mapview/4")->rgba==live->rgba,"session scopes retain their discoveries");
        Check(!mc_map::MapTestAccess::Draw(map,f,"A",0,5,false)&&!map.Load(f.host,"mapview/4"),"scene invalidation rejects stale request and drops old pictures");
        Check(!map.Load(f.host,"mapview/1junk"),"asset keys reject trailing junk");
    }
    {
        mc_map::Map map(session_config.c_str()); f.pause=true;
        auto render=std::async(std::launch::async,[&]{return mc_map::MapTestAccess::Draw(map,f,"A",0,10);});
        {std::unique_lock lock{f.mutex};Check(f.wake.wait_for(lock,std::chrono::seconds(2),[&]{return f.entered;}),"worker fixture reached chunk read");}
        mc_map::MapTestAccess::Invalidate(map);
        {std::lock_guard lock{f.mutex};f.resume=true;f.wake.notify_all();}
        Check(!render.get()&&!map.Load(f.host,"mapview/10"),"a world switch during rendering cannot publish stale pixels");
        f.pause=false;
    }
    {
        f.Put<std::uint64_t>(0x400,Fixture::Base+0x1000);
        mc_map::Map map(session_config.c_str());mc_map::MapTestAccess::Start(map,f);
        // No Load callback is made until after the capture: this runs with the tab hidden.
        Check(mc_map::MapTestAccess::Await(map),"worker finishes within fixture deadline");
        auto captured=map.Load(f.host,"mapview/1");Check(captured&&captured->rgba[3]==255,"background worker captures exploration with map tab hidden");
    }
    {
        const std::string config="{\"data_directory\":\""+root.string()+"\"}";
        {mc_map::Map map(config.c_str());mc_map::MapTestAccess::Start(map,f);Check(mc_map::MapTestAccess::Await(map),"persistent background capture finishes");}
        History auto_history("Auto",0,root);Check(auto_history.Load()&&auto_history.TileCount()==1,"shutdown flush preserves captures across reload");
    }
    std::filesystem::remove_all(root);
    std::puts("Exploration history, isolation, persistence and stale worker checks passed");
}
