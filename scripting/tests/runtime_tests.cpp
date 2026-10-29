// NOT ORIGINAL - translated synthetic QB exercised inside actual QuickJS.
#include "thug2/js_runtime.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace thug2::js;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
Value get(const Parameters& p,std::string_view name){for(auto i=p.rbegin();i!=p.rend();++i)if(i->name==checksum(name))return i->value;throw std::runtime_error("Missing test parameter "+std::string(name));}
bool has(const Parameters& p,std::string_view name){for(const auto& f:p)if(f.name==checksum(name))return true;return false;}
template<class F> void rejects(F fn,const char* why){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}check(caught,why);}
}
int main(int argc,char** argv){try{
    check(argc==2,"Expected translated fixtures");std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Cannot open fixtures");std::string source((std::istreambuf_iterator<char>(in)),{});
    rejects([]{Runtime(512*1024,10);},"Sub-1MiB memory limit rejected");
    rejects([]{Runtime(16*1024*1024,0);},"Zero interrupt budget rejected");
    Runtime vm;vm.load(source,"fixtures.js");vm.install_frame_wait();unsigned echoes=0;bool check_result=true;
    rejects([&]{vm.bind(checksum("Empty"),{});},"Empty host callback rejected");
    rejects([&]{vm.load(std::string_view("1\0+1",4),"null.js");},"Null byte in JS source rejected");
    vm.bind(checksum("Echo"),[&](const Parameters& args,const Parameters& params){
        ++echoes;check(get(args,"raw")==Value{2,0x80000000},"Float32 negative-zero bits cross C++ boundary");
        check(get(args,"name")==Value{13,checksum("opaque")},"Name is distinct from integer");
        check(get(args,"value")==get(params,"x"),"Arguments read caller parameters");
        rejects([&]{vm.start(checksum("zero"));},"Reentrant starts rejected");
        rejects([&]{vm.update(1,0);},"Reentrant updates rejected");
        rejects([&]{vm.cancel(1);},"Reentrant cancels rejected");
        rejects([&]{vm.load("1;","reentrant.js");},"Reentrant loads rejected");
        rejects([&]{vm.bind(checksum("Other"),[](const Parameters&,const Parameters&){return HostResult{};});},"Reentrant binds rejected");
        return HostResult{true,{{checksum("host_result"),{1,123}}},{}};
    });
    vm.bind(checksum("Check"),[&](const Parameters&,const Parameters&){return HostResult{check_result,{},{}};});
    vm.bind(checksum("Throws"),[](const Parameters&,const Parameters&)->HostResult{throw std::runtime_error("host failed");});
    auto id=vm.start(checksum("main"),{{checksum("x"),{1,42}}});auto r=vm.update(id,0);
    check(r.status=="waiting",r.message.c_str());check(echoes==1,"Host called once before yield");
    check(get(r.parameters,"x")==Value{1,42}&&get(r.parameters,"marker").bits==55,"Caller args override defaults; defaults retained");
    check(get(r.parameters,"host_result").bits==123&&!has(r.parameters,"result"),"Host output merged before callee wait");
    check(vm.update(id,0).status=="waiting"&&echoes==1,"Same frame idempotent");
    r=vm.update(id,1);check(r.status=="finished",r.message.c_str());
    check(get(r.parameters,"result").bits==42&&get(r.parameters,"checked").bits==1&&get(r.parameters,"done").bits==1,"Callee return, host condition and root return");
    check(!has(r.parameters,"private"),"Callee locals isolated");check(vm.update(id,1).status=="finished","Finished task stable");vm.cancel(id);
    check_result=false;id=vm.start(checksum("main"));check(vm.update(id,2).status=="waiting","Second main waits");r=vm.update(id,3);
    check(r.status=="finished"&&get(r.parameters,"result").bits==11&&get(r.parameters,"checked").bits==0,"Defaults and false host branch");vm.cancel(id);
    id=vm.start(checksum("zero"));r=vm.update(id,4);check(r.status=="finished"&&get(r.parameters,"done").bits==1,"Wait zero same frame");vm.cancel(id);
    id=vm.start(checksum("two"));check(vm.update(id,5).status=="waiting"&&vm.update(id,6).status=="waiting"&&vm.update(id,7).status=="finished","Wait two counts updates, including setting update");vm.cancel(id);
    id=vm.start(checksum("negative"));check(vm.update(id,8).status=="waiting"&&vm.update(id,9).status=="waiting","Negative int counter wraps uint32");vm.cancel(id);
    id=vm.start(checksum("float"));r=vm.update(id,10);check(r.status=="finished"&&get(r.parameters,"result")==Value{2,0x80000000},"Float type preserved through return");vm.cancel(id);
    id=vm.start(checksum("float"),{{checksum("x"),{1,7}},{0,{13,101}},{0,{13,102}},{0,{13,101}}});r=vm.update(id,10);
    check(r.status=="finished"&&get(r.parameters,"result")==Value{1,7},"Integer overrides numeric float default");
    unsigned flags=0;for(const auto& f:r.parameters)if(f.name==0&&f.value.type==13)++flags;
    check(flags==2,"Different unnamed name flags coexist; identical ones replace");vm.cancel(id);
    for(auto value:{Value{1,0},Value{1,1},Value{2,0x80000000}}){
        id=vm.start(checksum("expression"),{{checksum("x"),value}});r=vm.update(id,11);check(r.status=="finished"&&get(r.parameters,"truth").bits==unsigned(value.bits!=0),"QB expression conditions test raw word");vm.cancel(id);
    }
    id=vm.start(checksum("expression"),{{checksum("x"),{13,7}}});r=vm.update(id,11);
    check(r.status=="fault"&&r.message.find("checksum expression dispatch")!=std::string::npos,"Name expressions are not silently treated as booleans");vm.cancel(id);
    id=vm.start(checksum("callcondition"));r=vm.update(id,12);check(r.status=="finished"&&get(r.parameters,"truth").bits==1&&get(r.parameters,"done").bits==1,"QB-call condition runs callee then true body");vm.cancel(id);
    id=vm.start(checksum("spread"),{{checksum("x"),{13,0x12345678}}});r=vm.update(id,13);check(r.status=="finished"&&get(r.parameters,"forwarded")==Value{13,0x12345678},"Parameter spread keeps typed names");vm.cancel(id);
    for(auto script:{"unknown","throws","recursive","badwait","expression","floatwait","mixedwait"}){
        id=vm.start(checksum(script));r=vm.update(id,14);check(r.status=="fault"&&r.message.find("QB offset")!=std::string::npos,"Faults identify original QB source site");vm.cancel(id);
    }
    id=vm.start(checksum("zero"),{},0);r=vm.update(id,15);check(r.status=="fault"&&r.message.find("fuel")!=std::string::npos,"Statement fuel bounded");
    rejects([&]{vm.update(id,14);},"Backward frame rejected");rejects([&]{vm.load(source,"duplicate.js");},"Loading with retained tasks rejected");
    check(vm.cancel(id)&&!vm.cancel(id),"Cancellation releases task");rejects([&]{vm.start(checksum("absent"));},"Unknown root rejected");
    rejects([&]{vm.start(checksum("zero"),{{checksum("x"),{1,1}},{0,{99,0}}});},"Partial bad bridge type rejected cleanly");
    vm.bind(checksum("NoSuchHost"),[](const Parameters&,const Parameters&)->HostResult{return {true,{{checksum("ok"),{1,1}},{checksum("bad"),{99,0}}},{}};});
    id=vm.start(checksum("unknown"));r=vm.update(id,16);check(r.status=="fault"&&r.message.find("Unsupported QB bridge value type")!=std::string::npos,"Bad host output type faults task");vm.cancel(id);
    id=vm.start(checksum("zero"));r=vm.update(id,16);check(r.status=="finished"&&get(r.parameters,"done").bits==1,"Runtime recovers after recoverable task fault");vm.cancel(id);
    std::vector<std::uint32_t> retained;for(unsigned i=0;i<256;++i)retained.push_back(vm.start(checksum("zero")));
    rejects([&]{vm.start(checksum("zero"));},"Retained task count bounded");for(auto task:retained)vm.cancel(task);
    Runtime limited(16*1024*1024,2);rejects([&]{limited.load("while (true) {}","loop.js");},"QuickJS interrupts unbounded JS");
    rejects([&]{limited.load("throw Symbol('bad');","symbol.js");},"Symbol exception cleared without leaking pending exception");
    rejects([&]{limited.load("throw Object.create(null);","nullproto.js");},"Null-prototype exception cleared");
    rejects([&]{limited.load("throw { toString() { throw new Error('nested'); } };","badtostring.js");},"Throwing toString exception cleared");
    limited.load("globalThis.recovered = true;","recovery.js");
    rejects([&]{limited.load("globalThis.oom = new ArrayBuffer(64 * 1024 * 1024);","oom.js");},"QuickJS memory limit enforced");
    {
        Runtime gradual(1024*1024,100000);bool oom_caught=false;
        try{gradual.load("const a=[];while(true)a.push(new Uint8Array(256));","gradual_oom.js");}
        catch(const std::exception& e){oom_caught=std::string(e.what()).find("out of memory")!=std::string::npos;}
        check(oom_caught,"Gradual QuickJS OOM reports out of memory");
    }
    {
        Runtime task_oom(1024*1024,100000);
        task_oom.load("qb.define(1,function*(){new ArrayBuffer(4*1024*1024);return [];});qb.define(2,function*(){return [[1,{type:1,bits:9}]];});","oom-generator.js");
        auto t=task_oom.start(1);auto snap=task_oom.update(t,0);
        check(snap.status=="fault"&&snap.message.find("out of memory")!=std::string::npos,"Task-local generator OOM faults task Snapshot");
        task_oom.cancel(t);
        auto next=task_oom.start(2);snap=task_oom.update(next,1);
        check(snap.status=="finished"&&snap.parameters.size()==1&&snap.parameters[0].value.bits==9,"Runtime remains usable after isolated generator OOM fault");
        task_oom.cancel(next);
    }
    {
        Runtime fatal_interrupt(16*1024*1024,2);
        fatal_interrupt.load("qb.define(1, function* () { while (true) {} });","runaway.js");
        auto tid=fatal_interrupt.start(1);
        rejects([&]{fatal_interrupt.update(tid,0);},"Interrupt budget during update escapes as fatal C++ error");
        rejects([&]{fatal_interrupt.update(tid,1);},"Runtime is terminal after fatal update interrupt");
        rejects([&]{fatal_interrupt.start(1);},"Start rejected on terminal runtime");
        rejects([&]{fatal_interrupt.cancel(tid);},"Cancel rejected on terminal runtime");
    }
    {
        Runtime fatal_alloc;fatal_alloc.load(source,"fixtures.js");
        fatal_alloc.bind(checksum("NoSuchHost"),[](const Parameters&,const Parameters&)->HostResult{throw std::bad_alloc();});
        auto tid=fatal_alloc.start(checksum("unknown"));
        rejects([&]{fatal_alloc.update(tid,0);},"Host std::bad_alloc escapes update as fatal C++ error");
        rejects([&]{fatal_alloc.start(checksum("zero"));},"Runtime is terminal after host bad_alloc");
    }
    {
        Runtime fatal_js_oom(1024*1024,100000);fatal_js_oom.load(source,"fixtures.js");
        fatal_js_oom.load("globalThis.hog = new Uint8Array(256 * 1024);","hog.js");
        fatal_js_oom.bind(checksum("NoSuchHost"),[](const Parameters&,const Parameters&)->HostResult{
            Parameters big;big.reserve(4096);for(std::uint32_t i=0;i<4096;++i)big.push_back({i+1,{1,i}});
            return {true,std::move(big),{}};
        });
        auto tid=fatal_js_oom.start(checksum("unknown"));
        rejects([&]{fatal_js_oom.update(tid,0);},"QuickJS OOM in host response escapes update as fatal C++ error");
        rejects([&]{fatal_js_oom.update(tid,1);},"Runtime is terminal after QuickJS OOM in update");
    }
    std::cout<<"QB -> JS -> QuickJS -> C++: typed calls, branches, frame suspension/resume, errors and limits passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
