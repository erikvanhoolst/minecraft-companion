// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_biomes.h"
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
    static bool Draw(Map& map,Fixture& f,std::string world,int dimension,std::uint64_t serial,bool fresh=true,std::uint64_t owner=0,bool biomes=false,bool scoped=true) {
        Map::Request r; r.serial=serial;r.world=std::move(world);r.dimension=dimension;r.scoped=scoped;r.biomes=biomes;r.view=16;r.player=Fixture::Base+0x100;r.layout=&f.layout;r.colors[1]=0x112233;r.player_x=r.player_z=1000;r.dimension_object=owner;
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
    static void Follow(Map& map,int id,std::uint64_t object) { map.FollowDimension(id,object); }
    static void PublishAt(Map& map,Fixture& f,double x,double z) {
        map.serial=std::max<std::uint64_t>(map.serial,1);map.player_was_ready=true;
        map.Publish(f.host,true,Map::Place{x,64,z,0});
    }
    static void Biomes(Map& map,bool on) { map.show_biomes=on; }
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
        mc_map::Map portal("{\"world_keys\":[\"A\"],\"world_key\":\"A\",\"data_directory\":\"\"}");
        mc_map::MapTestAccess::PublishAt(portal,f,-17.5,40.2);
        Check(f.ints["world.dimension_auto"]==0&&f.ints["map.portal_ok"]==0,"an unconfirmed manual dimension has no portal coordinates");
        mc_map::MapTestAccess::Follow(portal,0,Fixture::Base+0x8000);
        mc_map::MapTestAccess::PublishAt(portal,f,-17.5,40.2);
        Check(f.ints["world.dimension_auto"]==1&&f.texts["world.diag"]=="Choose world, then Confirm","a read dimension only needs the world confirmed");
        Check(f.ints["map.portal_ok"]==1&&f.texts["map.portal_label"]=="Nether"&&f.texts["map.portal_coords"]=="X -3  Z 5","Overworld blocks divide by eight, rounding down");
        Check(!portal.OnAction("world_dimension",0)&&f.ints["world.dimension"]==0,"the manual dimension is refused while the game's is known");
        Check(portal.OnAction("world_confirm",0),"world confirmed");
        mc_map::MapTestAccess::Follow(portal,1,Fixture::Base+0x9000);
        mc_map::MapTestAccess::PublishAt(portal,f,-3,5);
        Check(f.ints["world.ready"]==1&&f.ints["world.dimension"]==1&&f.texts["world.label"]=="A / Nether","a portal keeps the confirmed world and switches dimension");
        Check(f.texts["map.portal_label"]=="Overworld"&&f.texts["map.portal_coords"]=="X -24  Z 40","Nether blocks multiply by eight");
        mc_map::MapTestAccess::Follow(portal,-1,0);
        mc_map::MapTestAccess::PublishAt(portal,f,-3,5);
        Check(f.ints["world.dimension"]==1&&f.ints["world.dimension_auto"]==1,"an unreadable tick keeps the last dimension");
        mc_map::MapTestAccess::Follow(portal,2,Fixture::Base+0xA000);
        mc_map::MapTestAccess::PublishAt(portal,f,0,0);
        Check(f.ints["map.portal_ok"]==0&&f.texts["map.portal_coords"].empty(),"the End has no portal coordinates");
    }
    {
        mc_map::Map owned(session_config.c_str());
        f.layout.chunk_dimension=0x8; f.Put<std::uint64_t>(0x1008,Fixture::Base+0x8000);
        Check(mc_map::MapTestAccess::Draw(owned,f,"A",0,1,true,Fixture::Base+0x8000),"chunks of the player's dimension draw");
        Check(owned.Load(f.host,"mapview/1")->rgba[3]==255,"owned chunk is captured");
        Check(mc_map::MapTestAccess::Draw(owned,f,"B",1,2,true,Fixture::Base+0x9000),"a view of another dimension still completes");
        Check(owned.Load(f.host,"mapview/2")->rgba[3]==0,"chunks left over from the other dimension are never recorded");
        f.layout.chunk_dimension=0;
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
    {
        Check(std::string{mc_biomes::Name(21)}=="Jungle"&&std::string{mc_biomes::Name(149)}=="Jungle M"&&
              !mc_biomes::Name(40)&&!mc_biomes::Name(mc_biomes::Unknown),"biome names by legacy id");
        Check(mc_biomes::Color(1)==0x8DB360&&mc_biomes::Color(129)!=0&&mc_biomes::Color(129)!=mc_biomes::Color(1)&&
              mc_biomes::Color(200)==0,"variants are lighter, unknown ids have no colour");
        std::uint8_t px[4]={0,0,0,255}; mc_biomes::Tint(px,7);
        Check(px[2]==178&&px[0]==0&&px[3]==255,"biome colour with 30% terrain");
        std::uint8_t grey[4]={100,100,100,255}; mc_biomes::Tint(grey,mc_biomes::Unknown);
        Check(grey[0]==60&&grey[1]==60&&grey[3]==255,"terrain without a biome is greyed");
        std::uint8_t empty[4]={}; mc_biomes::Tint(empty,mc_biomes::Unknown);
        Check(empty[3]==0,"unknown ground stays paper");

        History biomes("Biomes",0,root);
        std::vector<std::uint8_t> ids(256,4); ids[0]=21;
        biomes.Merge(0,0,terrain,&ids);
        std::vector<std::uint8_t> unknown_ids(256,mc_biomes::Unknown);
        biomes.Merge(0,0,unknown,&unknown_ids);
        std::vector<std::uint8_t> drawn_ids;
        const auto plain=biomes.Draw(0,0,16,false,false,&drawn_ids);
        Check(plain.rgba==terrain.rgba&&drawn_ids[0]==21&&drawn_ids[1]==4,"unread columns keep their recorded biome");
        const auto tinted=biomes.Draw(0,0,16,false,true);
        Check(tinted.rgba[0]==(0x53*7+10*3)/10&&tinted.rgba[4]==(0x05*7+10*3)/10,"biome view tints recorded terrain");
        Check(biomes.Save(),"biome history saves");
        History reread("Biomes",0,root); std::vector<std::uint8_t> reread_ids;
        Check(reread.Load()&&reread.Draw(0,0,16,false,false,&reread_ids).rgba==terrain.rgba&&reread_ids==drawn_ids,"biomes survive reload");
        {std::ifstream in(reread.File(),std::ios::binary);char magic[8]{};in.read(magic,8);Check(std::string(magic,8)=="MCHIST02","new files are version 2");}

        // A version 1 file (terrain only) from before biomes loads with unknown biomes.
        History old_format("Old",0,root);
        std::string bytes{"MCHIST01"};
        const auto u32=[&](std::uint32_t n){for(int i=0;i<4;++i)bytes.push_back(static_cast<char>(n>>(i*8)));};
        const std::string key="Old"; u32(key.size()); bytes+=key; u32(0); u32(1); u32(0);
        u32(0); u32(0); bytes.append(reinterpret_cast<const char*>(terrain.rgba.data()),1024);
        std::uint64_t h=14695981039346656037ULL; for(unsigned char c:bytes){h^=c;h*=1099511628211ULL;}
        u32(static_cast<std::uint32_t>(h));
        {std::ofstream out(old_format.File(),std::ios::binary);out<<bytes;}
        std::vector<std::uint8_t> old_ids;
        Check(old_format.Load()&&old_format.Draw(0,0,16,false,false,&old_ids).rgba==terrain.rgba&&old_ids[5]==mc_biomes::Unknown,"version 1 history loads without biomes");
    }
    {
        f.layout.chunk_biomes=0x300;
        for (unsigned i=0;i<256;++i) f.Put<std::uint8_t>(0x1300+i,i<64?21:1);
        f.Put<std::uint8_t>(0x1300+255,mc_biomes::Unknown);
        mc_map::Map biomes(session_config.c_str()); mc_map::MapTestAccess::Biomes(biomes,true);
        Check(mc_map::MapTestAccess::Draw(biomes,f,"",0,1,true,0,true,false),"live biome view draws");
        const auto picture=biomes.Load(f.host,"mapview/1");
        const int red=0x11*220/255; // level ground is shaded 220/255, as the game's map does
        Check(picture&&picture->rgba[0]==(0x53*7+red*3)/10&&picture->rgba[4*4*16]==(0x8D*7+red*3)/10,"biomes follow the heightmap's z * 16 + x order");
        Check((*picture).rgba[255*4]<100&&(*picture).rgba[255*4]==(*picture).rgba[255*4+1],"an unknown biome id greys its column");
        mc_map::MapTestAccess::Publish(biomes,f);
        Check(f.ints["map.biomes"]==1&&f.texts["map.legend0.name"]=="Plains"&&f.texts["map.legend1.name"]=="Jungle"&&
              f.texts["map.legend1.swatch"]=="module:mc:biome/21"&&f.ints["map.legend2.present"]==0,"legend lists biomes by area");
        mc_map::MapTestAccess::Biomes(biomes,false);mc_map::MapTestAccess::Publish(biomes,f);
        Check(f.ints["map.biomes"]==0&&f.ints["map.legend0.present"]==0,"terrain view hides the legend");
        Check(biomes.OnAction("map_biomes",0)&&(mc_map::MapTestAccess::Publish(biomes,f),f.ints["map.biomes"]==1),"Biomes toggles the view");
        const auto swatch=mc_map::Map::Swatch("biome/21");
        Check(swatch&&swatch->width==8&&swatch->rgba[(9*4)]==0x53&&swatch->rgba[0]==40,"legend swatch with a border");
        Check(!mc_map::Map::Swatch("biome/40")&&!mc_map::Map::Swatch("biome/21x"),"swatch keys are validated");
        f.layout.chunk_biomes=0;
    }
    std::filesystem::remove_all(root);
    std::puts("Exploration history, isolation, persistence and stale worker checks passed");
}
