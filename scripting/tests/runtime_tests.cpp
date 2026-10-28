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
    Runtime vm;vm.load(source,"fixtures.js");vm.install_frame_wait();unsigned echoes=0;bool check_result=true;
    vm.bind(checksum("Echo"),[&](const Parameters& args,const Parameters& params){
        ++echoes;check(get(args,"raw")==Value{2,0x80000000},"Float32 negative-zero bits cross C++ boundary");
        check(get(args,"name")==Value{13,checksum("opaque")},"Name is distinct from integer");
        check(get(args,"value")==get(params,"x"),"Arguments read caller parameters");
        rejects([&]{vm.start(checksum("zero"));},"Reentrant starts rejected");
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
    rejects([&]{vm.start(checksum("zero"),{{0,{99,0}}});},"Bad bridge type rejected");
    std::vector<std::uint32_t> retained;for(unsigned i=0;i<256;++i)retained.push_back(vm.start(checksum("zero")));
    rejects([&]{vm.start(checksum("zero"));},"Retained task count bounded");for(auto task:retained)vm.cancel(task);
    Runtime limited(16*1024*1024,2);rejects([&]{limited.load("while (true) {}","loop.js");},"QuickJS interrupts unbounded JS");
    limited.load("globalThis.recovered = true;","recovery.js");
    rejects([&]{limited.load("globalThis.oom = new ArrayBuffer(64 * 1024 * 1024);","oom.js");},"QuickJS memory limit enforced");
    std::cout<<"QB -> JS -> QuickJS -> C++: typed calls, branches, frame suspension/resume, errors and limits passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
