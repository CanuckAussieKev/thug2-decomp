// NOT ORIGINAL - QuickJS ownership and explicit C++ host boundary.
#include "thug2/js_runtime.hpp"
#include "runtime_source.hpp"
#include <quickjs.h>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace thug2::js {
std::uint32_t checksum(std::string_view text){
    std::uint32_t crc=0xffffffff;
    for(unsigned char c:text){if(c>='A'&&c<='Z')c+='a'-'A';if(c=='/')c='\\';crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320:0);}
    return crc;
}
namespace {
struct Owned {
    JSContext* ctx;JSValue value;
    Owned(JSContext* c,JSValue v):ctx(c),value(v){}
    ~Owned(){JS_FreeValue(ctx,value);}
    Owned(const Owned&)=delete;Owned& operator=(const Owned&)=delete;
    Owned(Owned&& other) noexcept:ctx(other.ctx),value(other.value){other.value=JS_UNDEFINED;}
    JSValue release(){auto out=value;value=JS_UNDEFINED;return out;}
};
}
struct Runtime::Impl {
    JSRuntime* runtime=nullptr;JSContext* context=nullptr;
    std::map<std::uint32_t,Host> hosts;
    std::size_t budget,remaining=0;bool busy=false;
    explicit Impl(std::size_t memory,std::size_t interrupts):budget(interrupts){
        if(memory<1024*1024||!budget)throw std::invalid_argument("Invalid QuickJS limits");
        runtime=JS_NewRuntime();if(!runtime)throw std::bad_alloc();
        JS_SetMemoryLimit(runtime,memory);JS_SetMaxStackSize(runtime,512*1024);
        JS_SetInterruptHandler(runtime,[](JSRuntime*,void* p){auto& self=*static_cast<Impl*>(p);if(!self.remaining)return 1;--self.remaining;return 0;},this);
        context=JS_NewContext(runtime);
        if(!context){JS_FreeRuntime(runtime);runtime=nullptr;throw std::bad_alloc();}
        JS_SetContextOpaque(context,this);
        Owned global(context,JS_GetGlobalObject(context));
        JS_SetPropertyStr(context,global.value,"__qbHost",JS_NewCFunction(context,host_call,"__qbHost",3));
    }
    ~Impl(){if(context)JS_FreeContext(context);if(runtime)JS_FreeRuntime(runtime);}
    struct Entry {
        Impl& self;
        explicit Entry(Impl& s):self(s){if(s.busy)throw std::logic_error("Reentrant QuickJS runtime call");s.busy=true;s.remaining=s.budget;JS_UpdateStackTop(s.runtime);}
        ~Entry(){self.busy=false;}
    };
    std::string text(JSValueConst value){
        const char* p=JS_ToCString(context,value);if(!p)throw std::runtime_error("Cannot read QuickJS string");
        std::string out(p);JS_FreeCString(context,p);return out;
    }
    void check(JSValueConst value){
        if(JS_IsException(value)){
            Owned error(context,JS_GetException(context));
            throw std::runtime_error(text(error.value));
        }
    }
    Owned get(JSValueConst obj,const char* key){Owned out(context,JS_GetPropertyStr(context,obj,key));check(out.value);return out;}
    Owned at(JSValueConst obj,std::uint32_t index){Owned out(context,JS_GetPropertyUint32(context,obj,index));check(out.value);return out;}
    std::uint32_t uint(JSValueConst value){
        double n;
        if(!JS_IsNumber(value)||JS_ToFloat64(context,&n,value)<0||!std::isfinite(n)||n<0||n>4294967295.0||std::floor(n)!=n)
            throw std::runtime_error("Expected QB uint32");
        return static_cast<std::uint32_t>(n);
    }
    Parameters read(JSValueConst array){
        if(!JS_IsArray(context,array))throw std::runtime_error("Expected QB parameter array");
        auto length=get(array,"length");auto n=uint(length.value);if(n>4096)throw std::length_error("QB parameter count limit");
        Parameters result;result.reserve(n);
        for(std::uint32_t i=0;i<n;++i){
            auto field=at(array,i);auto name=at(field.value,0);auto v=at(field.value,1);
            auto type=get(v.value,"type");auto bits=get(v.value,"bits");auto kind=uint(type.value);
            if(kind!=1&&kind!=2&&kind!=13)throw std::runtime_error("Unsupported QB bridge value type");
            result.push_back({uint(name.value),{kind,uint(bits.value)}});
        }
        return result;
    }
    Owned write(const Parameters& parameters){
        if(parameters.size()>4096)throw std::length_error("QB parameter count limit");
        Owned out(context,JS_NewArray(context));check(out.value);std::uint32_t index=0;
        for(const auto& field:parameters){
            if(field.value.type!=1&&field.value.type!=2&&field.value.type!=13)throw std::runtime_error("Unsupported QB bridge value type");
            Owned pair(context,JS_NewArray(context));check(pair.value);Owned value(context,JS_NewObject(context));check(value.value);
            if(JS_SetPropertyStr(context,value.value,"type",JS_NewUint32(context,field.value.type))<0||
               JS_SetPropertyStr(context,value.value,"bits",JS_NewUint32(context,field.value.bits))<0||
               JS_SetPropertyUint32(context,pair.value,0,JS_NewUint32(context,field.name))<0||
               JS_SetPropertyUint32(context,pair.value,1,value.release())<0||
               JS_SetPropertyUint32(context,out.value,index++,pair.release())<0)throw std::runtime_error("QuickJS allocation failed");
        }
        return out;
    }
    Owned method(const char* name,int argc,JSValueConst* argv){
        Owned global(context,JS_GetGlobalObject(context));auto qb=get(global.value,"qb");auto fn=get(qb.value,name);
        Owned result(context,JS_Call(context,fn.value,qb.value,argc,argv));check(result.value);return result;
    }
    static JSValue host_call(JSContext* ctx,JSValueConst,int argc,JSValueConst* argv){
        auto& self=*static_cast<Impl*>(JS_GetContextOpaque(ctx));
        try{
            if(argc!=3)throw std::runtime_error("Invalid QB host call");
            auto name=self.uint(argv[0]);auto found=self.hosts.find(name);
            if(found==self.hosts.end())throw std::runtime_error("Unbound QB host "+std::to_string(name));
            auto args=self.read(argv[1]);auto params=self.read(argv[2]);auto result=found->second(args,params);
            Owned out(ctx,JS_NewObject(ctx));self.check(out.value);auto fields=self.write(result.outputs);
            if(JS_SetPropertyStr(ctx,out.value,"condition",JS_NewBool(ctx,result.condition))<0||
               JS_SetPropertyStr(ctx,out.value,"outputs",fields.release())<0)throw std::bad_alloc();
            if(result.frames&&JS_SetPropertyStr(ctx,out.value,"frames",JS_NewUint32(ctx,*result.frames))<0)throw std::bad_alloc();
            return out.release();
        }catch(const std::exception& e){return JS_ThrowInternalError(ctx,"%s",e.what());}
        catch(...){return JS_ThrowInternalError(ctx,"Unknown C++ QB host exception");}
    }
};
Runtime::Runtime(std::size_t memory,std::size_t budget):impl_(std::make_unique<Impl>(memory,budget)){load(qb_runtime_source,"qb-runtime.js");}
Runtime::~Runtime()=default;
void Runtime::bind(std::uint32_t name,Host host){if(impl_->busy)throw std::logic_error("Reentrant QB binding");if(!host)throw std::invalid_argument("Empty QB host");impl_->hosts[name]=std::move(host);}
void Runtime::install_frame_wait(){
    bind(checksum("Wait"),[](const Parameters& args,const Parameters&){
        bool frame=false;std::optional<std::uint32_t> count;
        for(const auto& field:args)if(field.name==0){
            if(field.value.type==2)throw std::runtime_error("Float frame waits are not implemented");
            if(field.value.type==1)count=field.value.bits;
            if(field.value.type==13){
                for(auto flag:{"seconds","second","one_per_frame"})if(field.value.bits==checksum(flag))throw std::runtime_error("Unsupported Wait mode");
                if(field.value.bits==0xc847f2b9)throw std::runtime_error("Real-clock waits are not implemented");
                for(auto flag:{"frame","frames","gameframe","gameframes","game"})if(field.value.bits==checksum(flag))frame=true;
            }
        }
        if(!frame||!count)throw std::runtime_error("Only explicit integer frame waits are supported");
        return HostResult{true,{},count};
    });
}
void Runtime::load(std::string_view source,std::string_view filename){
    Impl::Entry entry(*impl_);if(source.size()>4*1024*1024)throw std::length_error("JS module exceeds 4 MiB");
    std::string code(source),label(filename);Owned result(impl_->context,JS_Eval(impl_->context,code.c_str(),code.size(),label.c_str(),JS_EVAL_TYPE_GLOBAL));impl_->check(result.value);
}
std::uint32_t Runtime::start(std::uint32_t name,const Parameters& parameters,std::uint32_t fuel){
    Impl::Entry entry(*impl_);auto args=impl_->write(parameters);
    Owned n(impl_->context,JS_NewUint32(impl_->context,name)),f(impl_->context,JS_NewUint32(impl_->context,fuel));
    JSValueConst argv[]{n.value,args.value,f.value};auto out=impl_->method("start",3,argv);return impl_->uint(out.value);
}
Snapshot Runtime::update(std::uint32_t task,std::uint32_t frame){
    Impl::Entry entry(*impl_);Owned t(impl_->context,JS_NewUint32(impl_->context,task)),f(impl_->context,JS_NewUint32(impl_->context,frame));
    JSValueConst argv[]{t.value,f.value};auto result=impl_->method("update",2,argv);
    auto status=impl_->get(result.value,"status"),params=impl_->get(result.value,"parameters"),message=impl_->get(result.value,"message");
    return {impl_->text(status.value),impl_->read(params.value),impl_->text(message.value)};
}
bool Runtime::cancel(std::uint32_t task){
    Impl::Entry entry(*impl_);Owned t(impl_->context,JS_NewUint32(impl_->context,task));JSValueConst argv[]{t.value};auto result=impl_->method("cancel",1,argv);return JS_ToBool(impl_->context,result.value)==1;
}
} // namespace thug2::js
