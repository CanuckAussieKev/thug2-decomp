// NOT ORIGINAL - public rich Runtime integration, including translated QB.
#include "thug2/js_runtime.hpp"
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

// One-shot native allocation fault: QuickJS uses malloc, so this targets the
// owning C++ snapshot decode after JS update has completed, without size guesses.
namespace { bool fail_native_allocation=false; }
void* operator new(std::size_t size) {
    if(fail_native_allocation){fail_native_allocation=false;throw std::bad_alloc();}
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
#endif

using namespace thug2::js;
namespace {
using V = RichValue;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F action, const char* why) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(why);
}
const V& get(const RichParameters& p, std::string_view name) {
    for (auto i=p.rbegin();i!=p.rend();++i) if(i->name==checksum(name)) return i->value;
    throw std::runtime_error("Missing parameter "+std::string(name));
}
bool has(const RichParameters& p,std::string_view name) {
    for(const auto& f:p)if(f.name==checksum(name))return true;
    return false;
}
RichField field(std::string_view n,V v) { return {checksum(n),std::move(v)}; }
const char* minimal =
    "qb.define(1,function*(p){qb.frame(p,true);return p;});"
    "qb.define(2,function*(p){qb.frame(p,true);yield* qb.call(3,[],p);return p;});";

void translated(const std::string& source) {
    const auto payload=V::structure({7,7,8},{V::integer(0xffffffff),V::name(9),
        V::array(10,{V::structure({10},{V::string({0x80,0xff},true)})})});
    const auto array=V::array(10,{payload});
    RichParameters input{field("payload",payload),field("raw",V::string({0xfe,0x80})),
        field("local",V::string({0xff},true)),field("pair",V::pair(0x80000000,0x7fc00001)),
        field("vector",V::vector(0x3f800000,0xff800000,0x7fc00002)),
        field("number",V::floating(0x80000000)),field("number",V::integer(41))};
    const auto original=input;
    RichParameters retained;
    RichSnapshot snapshot;
    {
        Runtime vm; vm.load(source,"rich-runtime.js");vm.installRichFrameWait();
        bool condition=true; unsigned calls=0, verifies=0;
        vm.bindRich(checksum("RichInspect"),[&](const RichParameters& args,const RichParameters& p){
            ++calls; retained=args;
            check(get(args,"data")==payload,"Nested host input");
            for(auto n:{"raw","local","pair","vector"})check(get(args,n)==get(input,n),"Raw host input");
            check(get(p,"number")==V::integer(41),"Typed defaults and input merge");
            check(get(p,"childDefault")==V::string({0x80,0xff},true),"Raw QB local string default");
            check(get(p,"private")==V::integer(999),"Callee private assignment");
            RichParameters expected;
            for(auto n:{"data","raw","local","pair","vector"})expected.push_back(field(n,get(args,n)));
            expected.push_back(field("dupe",V::name(checksum("opaque"))));
            expected.push_back(field("dupe",V::floating(0x80000000)));
            expected.push_back({0,V::name(checksum("flagB"))});
            expected.push_back({0,V::name(checksum("flagA"))});
            check(args==expected,"Ordered typed duplicate/flag merge");
            rejects([&]{vm.start(1);},"Reentrant scalar start");
            rejects([&]{vm.startRich(1);},"Reentrant rich start");
            rejects([&]{vm.update(1,0);},"Reentrant scalar update");
            rejects([&]{vm.updateRich(1,0);},"Reentrant rich update");
            rejects([&]{vm.cancel(1);},"Reentrant cancel");
            rejects([&]{vm.load("","reentrant");},"Reentrant load");
            rejects([&]{vm.bind(1,[](auto&,auto&){return HostResult{};});},"Reentrant scalar bind");
            rejects([&]{vm.bindRich(1,[](auto&,auto&){return RichHostResult{};});},"Reentrant rich bind");
            rejects([&]{vm.installRichFrameWait();},"Reentrant wait bind");
            return RichHostResult{condition,{field("replyHost",V::integer(3)),
                field("replyHost",V::floating(0x80000000)),field("replyHost",payload),
                field("arrayHost",array),field("byteHost",V::string({0xff,0x80},true))},{}};
        });
        vm.bindRich(checksum("RichVerify"),[&](const RichParameters& a,const RichParameters& p){
            ++verifies;check(a.empty(),"Empty host arguments");
            RichParameters matches;for(const auto& f:p)if(f.name==checksum("replyHost"))matches.push_back(f);
            check(matches==RichParameters{field("replyHost",V::floating(0x80000000)),field("replyHost",payload)},
                "Host outputs use JS typed merge");
            return RichHostResult{};
        });
        for(unsigned frame=0;frame<4;frame+=2) {
            auto id=vm.startRich(checksum("richMain"),input);
            rejects([&]{vm.update(id,frame+100);},"Mixed update accepted");
            snapshot=vm.updateRich(id,frame);
            check(snapshot.status=="waiting",snapshot.message.c_str());
            check(get(snapshot.parameters,"reply")==payload&&get(snapshot.parameters,"arrayResult")==array,
                "Waiting snapshot owns returned containers");
            check(vm.updateRich(id,frame).status=="waiting","Same frame wait");
            snapshot=vm.updateRich(id,frame+1);
            check(snapshot.status=="finished",snapshot.message.c_str());
            check(get(snapshot.parameters,"reply")==payload,"Returned structure");
            check(get(snapshot.parameters,"arrayResult")==array,"Returned array");
            check(get(snapshot.parameters,"byteResult")==V::string({0xff,0x80},true),"Returned bytes");
            check(get(snapshot.parameters,"condition")==V::integer(condition?1:0),"Host condition");
            check(!has(snapshot.parameters,"private")&&!has(snapshot.parameters,"childDefault")&&
                !has(snapshot.parameters,"replyHost"),"Callee locals leaked");
            check(get(snapshot.parameters,"done")==V::integer(1),"Root completion");
            check(vm.cancel(id)&&!vm.cancel(id),"Cancel lifecycle");condition=false;
        }
        check(calls==2&&verifies==2,"Host call counts");
        check(input==original,"Native inputs mutated");
        vm.load("qb.module({format:1});qb.define(123,function*(){return [];});","scalar.js");
        auto a=vm.startRich(checksum("richSimple"),input);
        auto b=vm.start(123);
        rejects([&]{vm.updateRich(b,100);},"Scalar task rich update accepted");
        check(vm.update(b,4).status=="finished","Scalar concurrent task");
        check(vm.updateRich(a,4).status=="finished","Rich concurrent task");
        vm.cancel(a);vm.cancel(b);
        rejects([&]{vm.updateRich(a,4);},"Cancelled task update");
        rejects([&]{vm.bindRich(1,{});},"Empty host accepted");
        unsigned wrong=0;
        vm.bind(checksum("Switchable"),[&](auto&,auto&){++wrong;return HostResult{};});
        a=vm.startRich(checksum("richSingle"),input);
        check(vm.updateRich(a,5).status=="fault"&&wrong==0,"Rich task called scalar host");vm.cancel(a);
        vm.bindRich(checksum("Switchable"),[&](auto&,auto&){++wrong;return RichHostResult{};});
        rejects([&]{vm.start(checksum("richSingle"));},"Scalar start must reject format-2 before running");
        a=vm.startRich(checksum("richSingle"),input);
        check(vm.updateRich(a,5).status=="finished"&&wrong==1,"Registration replacement");vm.cancel(a);
        a=vm.startRich(checksum("richMissing"));auto fault=vm.updateRich(a,6);
        check(fault.status=="finished","Missing command must return false");vm.cancel(a);
        vm.bindRich(checksum("Switchable"),[](auto&,auto&){
            return RichHostResult{true,RichParameters(4097,{1,V::integer(1)}),{}};
        });
        a=vm.startRich(checksum("richSingle"),input);
        check(vm.updateRich(a,6).status=="fault","Invalid host output must fault");vm.cancel(a);
        a=vm.startRich(checksum("richSimple"),input);
        check(vm.updateRich(a,6).status=="finished","Invalid output poisoned runtime");vm.cancel(a);
        vm.bindRich(checksum("Switchable"),[](auto&,auto&)->RichHostResult{throw std::runtime_error("ordinary native error");});
        a=vm.startRich(checksum("richSingle"),input);fault=vm.updateRich(a,7);
        check(fault.status=="fault"&&fault.message.find("ordinary native error")!=std::string::npos,
            "Ordinary callback exception");vm.cancel(a);
        // Once all retained tasks are cancelled, additional modules may load.
        for(unsigned i=0;i<2;++i) {
            const auto name=12345+i;
            vm.load("qb.module({format:2,requires:{values:2}});qb.define("+std::to_string(name)+",function*(p){qb.frame(p,true);return p;});","reload.js");
            a=vm.startRich(name,input);
            check(get(vm.updateRich(a,8).parameters,"payload")==payload,"Reload/cancel/observe lifecycle");vm.cancel(a);
        }
    }
    check(get(retained,"data")==payload&&get(snapshot.parameters,"reply")==payload,"Models outlive Runtime");
}

void limits() {
    Runtime vm;vm.load(minimal,"limits.js");
    rejects([&]{vm.startRich(1,RichParameters(4097,{1,V::integer(0)}));},"Field limit");
    auto deep=V::integer(1);for(unsigned i=0;i<130;++i)deep=V::structure({1},{deep});
    rejects([&]{vm.startRich(1,{{1,deep}});},"Depth limit");
    const auto many=V::array(1,std::vector<V>(250000,V::integer(1)));
    rejects([&]{vm.startRich(1,{{1,many},{2,many}});},"Aggregate node limit");
    const auto bytes=V::string(V::Bytes(250000,0xff));
    rejects([&]{vm.startRich(1,RichParameters(18,{1,bytes}));},"Aggregate byte limit");
    auto id=vm.startRich(1);check(id==1,"Failed input created orphan task");
    check(vm.updateRich(id,0).status=="finished","Input failure poisoned runtime");vm.cancel(id);
    for(unsigned i=0;i<256;++i)vm.startRich(1);
    rejects([&]{vm.startRich(1);},"Task capacity");
    check(vm.cancel(2),"Capacity release");vm.startRich(1);
}

void resource_failures() {
    const auto bytes=V::string(V::Bytes(250000,0xff));
    { Runtime vm(1024*1024);vm.load(minimal,"input-oom.js");
      rejects([&]{vm.startRich(1,{{1,bytes}});},"Input OOM missing");
      auto id=vm.startRich(1);check(vm.updateRich(id,0).status=="finished","Input OOM poisoned Runtime"); }
    for(bool native:{false,true}) {
        Runtime vm(1024*1024);vm.load(minimal,"output-oom.js");
        vm.bindRich(3,[&](auto&,auto&)->RichHostResult {
            if(native)throw std::bad_alloc();return {true,{{1,bytes}},{}};
        });
        auto id=vm.startRich(2);
        rejects([&]{vm.updateRich(id,0);},"Host OOM did not escape");
        rejects([&]{vm.startRich(1);},"Fatal host OOM did not poison Runtime");
    }
    { Runtime vm(1024*1024,10);vm.load("qb.define(1,function*(){while(true){}});","interrupt.js");
      auto id=vm.startRich(1);check(vm.updateRich(id,0).status=="fault","Interrupt faults task");
      vm.cancel(id);vm.load("qb.define(2,function*(){return [];});","after-interrupt.js");
      id=vm.startRich(2);check(vm.updateRich(id,1).status=="finished","Other tasks survive interrupt"); }
    { Runtime vm(1024*1024);vm.load(
        "qb.define(1,function*(){new ArrayBuffer(4*1024*1024);return [];});"
        "qb.define(2,function*(){return [];});","local-oom.js");
      auto id=vm.startRich(1);rejects([&]{vm.updateRich(id,0);},"Uncaught generator OOM is terminal");
      rejects([&]{vm.startRich(2);},"Generator OOM did not poison Runtime"); }
    { Runtime vm;vm.load(minimal,"native-snapshot-oom.js");auto id=vm.startRich(1);
      fail_native_allocation=true;
      rejects([&]{vm.updateRich(id,0);},"Native snapshot OOM missing");
      check(!fail_native_allocation,"Native allocation fault was not exercised");
      rejects([&]{vm.startRich(1);},"Native snapshot OOM did not poison Runtime"); }
    { Runtime vm;vm.load(minimal,"native-input-oom.js");fail_native_allocation=true;
      rejects([&]{vm.startRich(1);},"Native input OOM missing");
      auto id=vm.startRich(1);check(id==1&&vm.updateRich(id,0).status=="finished","Native input OOM recovery"); }
}

void malformed_boundaries() {
    const std::string malformed[] = {
        "{type:20}","{type:21}",
        "{type:10,members:[[1,{type:20}]]}",
        "{type:12,elementType:10,elements:[{type:10,members:[[1,{type:21}]]}]}",
        "{type:1,get bits(){throw Error('ordinary getter fault');}}",
        "(()=>{const p=Proxy.revocable([],{});p.revoke();return p.proxy;})()"
    };
    for(const auto& value:malformed) {
        Runtime vm;unsigned called=0;
        vm.bindRich(3,[&](auto&,auto&){++called;return RichHostResult{};});
        vm.load(std::string(minimal)+"qb.define(4,function*(){yield* qb.call(3,[[1,"+value+"]],[]);return [];});",
            "malformed-host.js");
        auto id=vm.startRich(4);auto r=vm.updateRich(id,0);
        check(r.status=="fault"&&called==0,"Invalid host argument was accepted");vm.cancel(id);
        id=vm.startRich(1);check(vm.updateRich(id,1).status=="finished","Host decode left pending exception");
    }
    for(const auto& value:malformed) {
        Runtime vm;
        // Inject an invalid boundary descriptor after the scheduler's own
        // semantic copy. This specifically exercises the native snapshot codec.
        vm.load(std::string(minimal)+"{const old=qb;let once=true;globalThis.qb={...old,update(...a){"
            "const r=old.update(...a);if(once){once=false;r.parameters=[[1,"+value+"]];}return r;}};}",
            "malformed-snapshot.js");
        auto id=vm.startRich(1);rejects([&]{vm.updateRich(id,0);},"Invalid snapshot accepted");vm.cancel(id);
        id=vm.startRich(1);check(vm.updateRich(id,1).status=="finished","Snapshot decode left pending exception");
    }
    for(bool snapshot:{false,true}) {
        Runtime vm(1024*1024);vm.bindRich(3,[](auto&,auto&){return RichHostResult{};});
        const std::string value="{type:1,get bits(){new ArrayBuffer(4*1024*1024);return 1;}}";
        if(snapshot)vm.load(std::string(minimal)+"{const old=qb;globalThis.qb={...old,update(...a){"
            "const r=old.update(...a);r.parameters=[[1,"+value+"]];return r;}};}","snapshot-decode-oom.js");
        else vm.load(std::string(minimal)+"qb.define(4,function*(){yield* qb.call(3,[[1,"+value+"]],[]);return [];});",
            "host-decode-oom.js");
        auto id=vm.startRich(snapshot?1:4);rejects([&]{vm.updateRich(id,0);},"Decode OOM did not escape");
        rejects([&]{vm.startRich(1);},"Decode OOM did not poison Runtime");
    }
    // UTF-16 -> UTF-8 conversion can allocate after qb.update succeeds. Fill
    // the bounded engine heap with retained buffers, then observe a prebuilt
    // wide status string. The same bounded program succeeds with headroom.
    for(bool bounded:{false,true}) {
        Runtime vm(bounded?1024*1024:16*1024*1024);
        vm.load(std::string(minimal)+R"JS(
            {const old=qb, wide='\u0100'.repeat(65536), held=[];
             globalThis.qb={...old,update(...a){
               const r=old.update(...a);r.status=wide;
               let spent=0;
               for(let size=65536;size>=256;size/=2) {
                 while(spent<2*1024*1024) {
                   try {held.push(new ArrayBuffer(size));spent+=size;}catch {break;}
                 }
               }
               return r;
             }};}
        )JS","snapshot-text-oom.js");
        auto id=vm.startRich(1);
        if(!bounded)check(vm.updateRich(id,0).status.size()==131072,"Wide snapshot positive control");
        else {
            bool text_failed=false;
            try {(void)vm.updateRich(id,0);}catch(const std::exception& e) {
                text_failed=std::string(e.what()).find("Cannot read QuickJS string")!=std::string::npos;
            }
            check(text_failed,"Wide snapshot did not exercise string-conversion OOM");
            rejects([&]{vm.startRich(1);},"Snapshot text OOM did not poison Runtime");
        }
    }
}
}
int main(int argc,char** argv) { try {
    check(argc==2,"Expected rich fixture");std::ifstream file(argv[1],std::ios::binary);
    check(bool(file),"Cannot open rich fixture");
    translated(std::string((std::istreambuf_iterator<char>(file)),{}));
    limits();resource_failures();malformed_boundaries();
    std::cout<<"Rich QB/native bridge, modes, ownership, limits and resource failures passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
