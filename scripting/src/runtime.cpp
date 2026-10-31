// NOT ORIGINAL - QuickJS ownership and explicit C++ host boundary.
#include "thug2/js_runtime.hpp"
#include "runtime_source.hpp"
#include "value_codec.hpp"
#include <quickjs.h>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
#include <variant>

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
struct CString {
    JSContext* ctx;const char* value;
    ~CString(){JS_FreeCString(ctx,value);}
};
#if defined(__SANITIZE_ADDRESS__)
constexpr std::size_t max_stack_size=4*1024*1024;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr std::size_t max_stack_size=4*1024*1024;
#else
constexpr std::size_t max_stack_size=512*1024;
#endif
#else
constexpr std::size_t max_stack_size=512*1024;
#endif
void validate(const Parameters& parameters){
    if(parameters.size()>4096)throw std::length_error("QB parameter count limit");
    for(const auto& field:parameters)
        if(field.value.type!=1&&field.value.type!=2&&field.value.type!=13)throw std::runtime_error("Unsupported QB bridge value type");
}
std::uint32_t frame_wait_count(const Parameters& args) {
    bool frame=false;std::optional<std::uint32_t> count;
    for(const auto& field:args)if(field.name==0){
        if(field.value.type==2)throw std::runtime_error("Float frame waits are not implemented");
        if(field.value.type==1)count=field.value.bits;
        if(field.value.type==13){
            for(auto flag:{"frame","frames","seconds","second","one_per_frame"})if(field.value.bits==checksum(flag))throw std::runtime_error("Unsupported Wait mode");
            if(field.value.bits==0xc847f2b9)throw std::runtime_error("Real-clock waits are not implemented");
            for(auto flag:{"gameframe","gameframes","game"})if(field.value.bits==checksum(flag))frame=true;
        }
    }
    if(!frame||!count)throw std::runtime_error("Only explicit integer frame waits are supported");
    return *count;
}
}
struct Runtime::Impl {
    enum class Mode { scalar, rich };
    JSRuntime* runtime=nullptr;JSContext* context=nullptr;
    std::map<std::uint32_t,std::variant<Host,RichHost>> hosts;
    std::map<std::uint32_t,Mode> task_modes;
    Mode active_mode=Mode::scalar;
    std::size_t budget,remaining=0;bool busy=false,updating=false,fatal=false,unusable=false,interrupted=false;
    explicit Impl(std::size_t memory,std::size_t interrupts):budget(interrupts){
        if(memory<1024*1024||!budget)throw std::invalid_argument("Invalid QuickJS limits");
        runtime=JS_NewRuntime();if(!runtime)throw std::bad_alloc();
        JS_SetMemoryLimit(runtime,memory);JS_SetMaxStackSize(runtime,max_stack_size);
        JS_SetInterruptHandler(runtime,[](JSRuntime*,void* p){auto& self=*static_cast<Impl*>(p);if(!self.remaining){if(self.updating){self.fatal=true;self.interrupted=true;}return 1;}--self.remaining;return 0;},this);
        context=JS_NewContext(runtime);
        if(!context){JS_FreeRuntime(runtime);runtime=nullptr;throw std::bad_alloc();}
        JS_SetContextOpaque(context,this);
        try{
            Owned global(context,JS_GetGlobalObject(context));check(global.value);
            Owned host(context,JS_NewCFunction(context,host_call,"__qbHost",3));check(host.value);
            if(JS_SetPropertyStr(context,global.value,"__qbHost",host.release())<0)check(JS_EXCEPTION);
        }catch(...){
            JS_FreeContext(context);context=nullptr;JS_FreeRuntime(runtime);runtime=nullptr;throw;
        }
    }
    ~Impl(){if(context)JS_FreeContext(context);if(runtime)JS_FreeRuntime(runtime);}
    struct Entry {
        Impl& self;
        explicit Entry(Impl& s):self(s){
            if(s.busy)throw std::logic_error("Reentrant QuickJS runtime call");
            if(s.unusable)throw std::runtime_error("QuickJS runtime unusable after fatal error");
            s.busy=true;s.fatal=false;s.interrupted=false;s.remaining=s.budget;JS_UpdateStackTop(s.runtime);
        }
        ~Entry(){if(self.fatal)self.unusable=true;self.updating=false;self.active_mode=Mode::scalar;self.busy=false;}
    };
    std::string text(JSValueConst value){
        if(!JS_IsString(value))throw std::runtime_error("Expected QuickJS string");
        std::size_t len=0;const char* p=JS_ToCStringLen(context,&len,value);
        if(!p){if(updating)fatal=true;JS_FreeValue(context,JS_GetException(context));throw std::runtime_error("Cannot read QuickJS string");}
        CString owned{context,p};return std::string(p,len);
    }
    // Uncaught engine errors during update() bypass the JS scheduler's finally
    // block and mark the Runtime unusable. Task-local JS exceptions (including
    // a generator's own caught allocation failure that unwinds its frame inside
    // qb.update) return a terminal fault Snapshot while keeping Runtime usable.
    void check(JSValueConst value){
        if(JS_IsException(value)){
            Owned error(context,JS_GetException(context));
            if(JS_IsNull(error.value)){if(updating)fatal=true;throw std::runtime_error("InternalError: out of memory");}
            const char* p=JS_ToCString(context,error.value);
            if(!p){
                Owned secondary(context,JS_GetException(context));
                if(JS_IsNull(secondary.value)||JS_IsError(error.value)){
                    if(updating)fatal=true;
                    throw std::runtime_error("InternalError: out of memory");
                }
                throw std::runtime_error("Cannot format QuickJS exception");
            }
            CString owned{context,p};std::string out(p);
            if(out=="InternalError")out="InternalError: out of memory";
            if(updating&&(out.find("out of memory")!=std::string::npos||out.find("stack overflow")!=std::string::npos||out.find("interrupted")!=std::string::npos))
                fatal=true;
            throw std::runtime_error(out);
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
        int is_array=JS_IsArray(array);if(is_array<0)check(JS_EXCEPTION);
        if(!is_array)throw std::runtime_error("Expected QB parameter array");
        auto length=get(array,"length");auto n=uint(length.value);if(n>4096)throw std::length_error("QB parameter count limit");
        Parameters result;result.reserve(n);
        for(std::uint32_t i=0;i<n;++i){
            auto field=at(array,i);int pair=JS_IsArray(field.value);if(pair<0)check(JS_EXCEPTION);
            if(!pair)throw std::runtime_error("Expected QB parameter pair");
            auto name=at(field.value,0);auto v=at(field.value,1);
            if(!JS_IsObject(v.value))throw std::runtime_error("Expected QB value object");
            auto type=get(v.value,"type");auto bits=get(v.value,"bits");auto kind=uint(type.value);
            if(kind!=1&&kind!=2&&kind!=13)throw std::runtime_error("Unsupported QB bridge value type");
            result.push_back({uint(name.value),{kind,uint(bits.value)}});
        }
        return result;
    }
    Owned write(const Parameters& parameters){
        validate(parameters);
        Owned out(context,JS_NewArray(context));check(out.value);std::uint32_t index=0;
        for(const auto& field:parameters){
            Owned pair(context,JS_NewArray(context));check(pair.value);Owned value(context,JS_NewObject(context));check(value.value);
            if(JS_SetPropertyStr(context,value.value,"type",JS_NewUint32(context,field.value.type))<0||
               JS_SetPropertyStr(context,value.value,"bits",JS_NewUint32(context,field.value.bits))<0||
               JS_SetPropertyUint32(context,pair.value,0,JS_NewUint32(context,field.name))<0||
               JS_SetPropertyUint32(context,pair.value,1,value.release())<0||
               JS_SetPropertyUint32(context,out.value,index++,pair.release())<0)check(JS_EXCEPTION);
        }
        return out;
    }
    Owned write_rich(const RichParameters& parameters) {
        RichCodec codec(context);
        Owned out(context,codec.encode_parameters(parameters));
        check(out.value);
        return out;
    }
    RichParameters read_rich(JSValueConst array) {
        try {
            RichCodec codec(context);
            return codec.decode_parameters(array);
        } catch(const RichCodecError& error) {
            if(error.resource_failure())fatal=true;
            throw;
        } catch(const std::bad_alloc&) {
            fatal=true;
            throw;
        }
    }
    void require_mode(std::uint32_t task,Mode mode) {
        auto found=task_modes.find(task);
        if(found==task_modes.end())throw std::runtime_error("Unknown native QB task");
        if(found->second!=mode)throw std::logic_error("Wrong QB task transport mode");
    }
    // Preallocate the native map node before JS creates a task. Inserting the
    // extracted node afterwards performs no allocation and cannot orphan a JS
    // task on native std::bad_alloc. Zero is never a scheduler task ID.
    std::uint32_t start_task(std::uint32_t name,JSValueConst args,std::uint32_t fuel,Mode mode) {
        if(task_modes.size()>=256)throw std::length_error("Native QB task capacity limit");
        std::map<std::uint32_t,Mode> reservation;
        auto node=reservation.extract(reservation.emplace(0,mode).first);
        Owned n(context,JS_NewUint32(context,name)),f(context,JS_NewUint32(context,fuel));
        active_mode=mode;
        JSValueConst argv[]{n.value,args,f.value};auto out=method(mode==Mode::scalar?"startScalar":"start",3,argv);
        auto id=uint(out.value);
        if(!id||task_modes.contains(id)){fatal=true;throw std::runtime_error("Invalid QB task identity");}
        node.key()=id;task_modes.insert(std::move(node));
        return id;
    }
    Owned method(const char* name,int argc,JSValueConst* argv){
        Owned global(context,JS_GetGlobalObject(context));check(global.value);auto qb=get(global.value,"qb");auto fn=get(qb.value,name);
        Owned result(context,JS_Call(context,fn.value,qb.value,argc,argv));check(result.value);return result;
    }
    Owned advance(JSValueConst task,JSValueConst frame) {
        JSValueConst argv[]{task,frame};
        try {return method("update",2,argv);}
        catch(const std::bad_alloc&){throw;}
        catch(const std::exception&) {
            if(!interrupted)throw;
            // The uncatchable interrupt skipped JS finally blocks. Discard
            // that generator and explicitly restore the scheduler before
            // observing the fault; never attempt to resume its suspended stack.
            interrupted=false;fatal=false;remaining=budget;
            try {return method("abort",1,argv);}
            catch(...){fatal=true;throw;}
        }
    }
    static void make_uncatchable(JSContext* ctx) {
        JSValue error=JS_GetException(ctx);JS_SetUncatchableError(ctx,error);JS_Throw(ctx,error);
    }
    static JSValue host_call(JSContext* ctx,JSValueConst,int argc,JSValueConst* argv){
        auto& self=*static_cast<Impl*>(JS_GetContextOpaque(ctx));
        HostResult result;
        RichHostResult rich_result;
        bool rich=false;
        try{
            if(argc!=3)throw std::runtime_error("Invalid QB host call");
            auto name=self.uint(argv[0]);auto found=self.hosts.find(name);
            if(found==self.hosts.end())return JS_UNDEFINED;
            rich=std::holds_alternative<RichHost>(found->second);
            if(rich!=(self.active_mode==Mode::rich))throw std::runtime_error("QB host/task transport mode mismatch");
            if(rich) {
                auto args=self.read_rich(argv[1]);auto params=self.read_rich(argv[2]);
                rich_result=std::get<RichHost>(found->second)(args,params);
                RichCodec codec(ctx);codec.validate_parameters(rich_result.outputs);
            } else {
                auto args=self.read(argv[1]);auto params=self.read(argv[2]);
                result=std::get<Host>(found->second)(args,params);
                validate(result.outputs);
            }
        }catch(const std::bad_alloc&){
            self.fatal=true;JS_ThrowOutOfMemory(ctx);make_uncatchable(ctx);return JS_EXCEPTION;
        }catch(const std::exception& e){
            JS_ThrowPlainError(ctx,"%s",e.what());
            if(self.fatal)make_uncatchable(ctx);
            return JS_EXCEPTION;
        }
        catch(...){return JS_ThrowPlainError(ctx,"Unknown C++ QB host exception");}
        try{
            Owned out(ctx,JS_NewObject(ctx));self.check(out.value);
            auto fields=rich?self.write_rich(rich_result.outputs):self.write(result.outputs);
            const bool condition=rich?rich_result.condition:result.condition;
            const auto frames=rich?rich_result.frames:result.frames;
            if(JS_DefinePropertyValueStr(ctx,out.value,"condition",JS_NewBool(ctx,condition),JS_PROP_C_W_E)<0||
               JS_DefinePropertyValueStr(ctx,out.value,"outputs",fields.release(),JS_PROP_C_W_E)<0||
               (frames&&JS_DefinePropertyValueStr(ctx,out.value,"frames",JS_NewUint32(ctx,*frames),JS_PROP_C_W_E)<0))self.check(JS_EXCEPTION);
            return out.release();
        }catch(const std::exception& e){
            self.fatal=true;JS_ThrowPlainError(ctx,"%s",e.what());make_uncatchable(ctx);return JS_EXCEPTION;
        }catch(...){
            self.fatal=true;JS_ThrowOutOfMemory(ctx);make_uncatchable(ctx);return JS_EXCEPTION;
        }
    }
};
Runtime::Runtime(std::size_t memory,std::size_t budget):impl_(std::make_unique<Impl>(memory,budget)){load(qb_runtime_source,"qb-runtime.js");}
Runtime::~Runtime()=default;
void Runtime::bind(std::uint32_t name,Host host){if(impl_->busy)throw std::logic_error("Reentrant QB binding");if(impl_->unusable)throw std::runtime_error("QuickJS runtime unusable after fatal error");if(!host)throw std::invalid_argument("Empty QB host");impl_->hosts[name]=std::move(host);}
void Runtime::bindRich(std::uint32_t name,RichHost host){if(impl_->busy)throw std::logic_error("Reentrant QB binding");if(impl_->unusable)throw std::runtime_error("QuickJS runtime unusable after fatal error");if(!host)throw std::invalid_argument("Empty QB rich host");impl_->hosts[name]=std::move(host);}
void Runtime::install_frame_wait(){
    bind(checksum("Wait"),[](const Parameters& args,const Parameters&){
        return HostResult{true,{},frame_wait_count(args)};
    });
}
void Runtime::installRichFrameWait(){
    bindRich(checksum("Wait"),[](const RichParameters& args,const RichParameters&){
        Parameters scalar;scalar.reserve(args.size());
        for(const auto& field:args){
            const auto type=field.value.type();
            if(type!=1&&type!=2&&type!=13)throw std::runtime_error("Unsupported Wait parameter type");
            scalar.push_back({field.name,{type,field.value.bits()}});
        }
        return RichHostResult{true,{},frame_wait_count(scalar)};
    });
}
void Runtime::load(std::string_view source,std::string_view filename){
    Impl::Entry entry(*impl_);if(source.size()>4*1024*1024)throw std::length_error("JS module exceeds 4 MiB");
    if(source.find('\0')!=std::string_view::npos||filename.find('\0')!=std::string_view::npos)throw std::invalid_argument("Null byte in JS module");
    std::string code(source),label(filename);Owned result(impl_->context,JS_Eval(impl_->context,code.c_str(),code.size(),label.c_str(),JS_EVAL_TYPE_GLOBAL));impl_->check(result.value);
}
std::uint32_t Runtime::start(std::uint32_t name,const Parameters& parameters,std::uint32_t fuel){
    Impl::Entry entry(*impl_);auto args=impl_->write(parameters);
    return impl_->start_task(name,args.value,fuel,Impl::Mode::scalar);
}
std::uint32_t Runtime::startRich(std::uint32_t name,const RichParameters& parameters,std::uint32_t fuel){
    Impl::Entry entry(*impl_);auto args=impl_->write_rich(parameters);
    return impl_->start_task(name,args.value,fuel,Impl::Mode::rich);
}
Snapshot Runtime::update(std::uint32_t task,std::uint32_t frame){
    Impl::Entry entry(*impl_);impl_->require_mode(task,Impl::Mode::scalar);impl_->active_mode=Impl::Mode::scalar;
    try {
    impl_->updating=true;Owned t(impl_->context,JS_NewUint32(impl_->context,task)),f(impl_->context,JS_NewUint32(impl_->context,frame));
    auto result=impl_->advance(t.value,f.value);
    auto status=impl_->get(result.value,"status"),params=impl_->get(result.value,"parameters"),message=impl_->get(result.value,"message");
    return {impl_->text(status.value),impl_->read(params.value),impl_->text(message.value)};
    } catch(const std::bad_alloc&) {impl_->fatal=true;throw;}
}
RichSnapshot Runtime::updateRich(std::uint32_t task,std::uint32_t frame){
    Impl::Entry entry(*impl_);impl_->require_mode(task,Impl::Mode::rich);impl_->active_mode=Impl::Mode::rich;
    try {
    impl_->updating=true;Owned t(impl_->context,JS_NewUint32(impl_->context,task)),f(impl_->context,JS_NewUint32(impl_->context,frame));
    auto result=impl_->advance(t.value,f.value);
    auto status=impl_->get(result.value,"status"),params=impl_->get(result.value,"parameters"),message=impl_->get(result.value,"message");
    return {impl_->text(status.value),impl_->read_rich(params.value),impl_->text(message.value)};
    } catch(const std::bad_alloc&) {impl_->fatal=true;throw;}
}
bool Runtime::cancel(std::uint32_t task){
    Impl::Entry entry(*impl_);Owned t(impl_->context,JS_NewUint32(impl_->context,task));JSValueConst argv[]{t.value};
    auto result=impl_->method("cancel",1,argv);int b=JS_ToBool(impl_->context,result.value);if(b<0)impl_->check(JS_EXCEPTION);
    impl_->task_modes.erase(task);return b==1;
}
} // namespace thug2::js
