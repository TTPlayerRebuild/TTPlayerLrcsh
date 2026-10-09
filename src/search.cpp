#include "core.h"

namespace ttp::lrc {
namespace {
struct Event {
    HANDLE value;
    Event(bool manual,bool signaled=false):value(CreateEventW(nullptr,manual,signaled,nullptr)){if(!value)throw std::bad_alloc();}
    ~Event(){CloseHandle(value);}
    operator HANDLE()const{return value;}
};
struct State {
    std::mutex mutex;
    Event wake{false},ready_event{true},callback_done{true,true};
    HANDLE thread{};DWORD thread_id{};
    Callback* callback{};
    std::shared_ptr<Catalog> catalog;
    NetworkValue network;
    Service service;
    int provider{},command{};
    bool configured{},initializing{},ready{},stopped{};
    HRESULT startup{E_PENDING},failure{S_OK};
    unsigned long long generation{};
    std::wstring artist,title;
    Candidate selected;
    std::vector<Candidate> rows;
    std::shared_ptr<Abort> abort;
    explicit State(std::shared_ptr<Catalog> c,int i):catalog(std::move(c)),provider(i){}
    ~State(){if(callback)callback->Release();if(thread)CloseHandle(thread);}
    void Stop(){
        std::shared_ptr<Abort> current;
        {std::lock_guard lock(mutex);stopped=true;++generation;command=0;current=abort;SetEvent(wake);}
        if(current)current->Cancel();
    }
    void DetachCallback(){Callback* p{};{std::lock_guard lock(mutex);p=std::exchange(callback,nullptr);}if(p)p->Release();}
    void Started(HRESULT result){std::lock_guard lock(mutex);startup=result;ready=SUCCEEDED(result);failure=result;SetEvent(ready_event);}
    bool Current(unsigned long long id)const{return !stopped&&generation==id;}// mutex held
    template<class F> void Notify(unsigned long long id,F&& f){
        Callback* p{};
        {std::lock_guard lock(mutex);if(!Current(id)||!callback)return;p=callback;ResetEvent(callback_done);}
        struct Finish{State* s;~Finish(){std::lock_guard lock(s->mutex);SetEvent(s->callback_done);}}finish{this};
        // Admission is the cancellation boundary. An admitted callback may
        // finish, and no state lock is held across reentrant host code.
        f(p);
    }
};
DWORD WaitWithSentMessages(HANDLE handle,DWORD timeout){
    const DWORD start=GetTickCount();
    for(;;){
        DWORD remaining=INFINITE;
        if(timeout!=INFINITE){const DWORD elapsed=GetTickCount()-start;if(elapsed>=timeout)return WaitForSingleObject(handle,0);remaining=timeout-elapsed;}
        auto result=MsgWaitForMultipleObjects(1,&handle,FALSE,remaining,QS_SENDMESSAGE);
        if(result!=WAIT_OBJECT_0+1)return result;
        // Process only synchronous sends, never queued WM_CLOSE/commands that
        // would reenter the partially destroyed dialog.
        MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);
    }
}
void Retire(const std::shared_ptr<State>& s){
    s->Stop();if(GetCurrentThreadId()==s->thread_id)return;
    WaitWithSentMessages(s->callback_done,INFINITE);s->DetachCallback();
    // No callbacks can start after Stop or survive the drain. A stuck network
    // cleanup may finish later using its shared state and retained module.
    if(s->thread)WaitWithSentMessages(s->thread,1000);
}
void Work(const std::shared_ptr<State>& s)noexcept{
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try{
        if(FAILED(com))throw Failure(32004,"COM initialization failed");
        Transport transport;transport.Initialize(s->network);s->Started(S_OK);
        for(;;){
            if(WaitForSingleObject(s->wake,INFINITE)!=WAIT_OBJECT_0)throw Failure(32005,"Worker wait failed");
            int command{};unsigned long long id{};std::wstring artist,title;Candidate row;Service service;std::shared_ptr<Abort> abort;
            {std::lock_guard lock(s->mutex);if(s->stopped)break;command=std::exchange(s->command,0);id=s->generation;
                artist=s->artist;title=s->title;row=s->selected;abort=s->abort;service=s->service;}
            if(!command)continue;
            try{
                if(command==1){
                    if(!s->configured)s->catalog->Refresh(transport,s->network,abort);
                    service=s->configured?service:s->catalog->At(s->provider);abort->Check();
                    s->Notify(id,[&](Callback* p){p->OnServer(service.name.c_str());});
                    auto rows=FindLyrics(transport,service,s->network,abort,artist,title);
                    std::vector<std::wstring> artists,titles;std::vector<LPCWSTR> ap,tp;
                    for(const auto& r:rows){artists.push_back(Wide(r.artist));titles.push_back(Wide(r.title));}
                    for(size_t i=0;i<rows.size();++i){ap.push_back(artists[i].c_str());tp.push_back(titles[i].c_str());}
                    {std::lock_guard lock(s->mutex);if(!s->Current(id))continue;s->rows=std::move(rows);s->service=service;}
                    s->Notify(id,[&](Callback* p){p->OnResults(static_cast<int>(ap.size()),ap.data(),tp.data());});
                }else{
                    auto result=DownloadLyric(transport,service,s->network,abort,row);
                    s->Notify(id,[&](Callback* p){p->OnDownload(static_cast<int>(result.text.size()),result.text.c_str(),result.title.c_str(),result.url.c_str());});
                }
            }catch(const Canceled&){
            }catch(const Failure& e){
                auto text=Resource(e.code);if(text.empty())text=Resource(32020);
                if(e.server_message&&*e.what())try{text=Wide(e.what());}catch(...){}
                s->Notify(id,[&](Callback* p){p->OnError(text.c_str());});
            }catch(...){s->Notify(id,[&](Callback* p){p->OnError(Resource(32005).c_str());});}
        }
    }catch(...){
        std::lock_guard lock(s->mutex);s->ready=false;s->failure=E_FAIL;
        if(s->startup==E_PENDING){s->startup=E_FAIL;SetEvent(s->ready_event);}
    }
    if(SUCCEEDED(com))CoUninitialize();
}
struct ThreadContext{std::shared_ptr<State> state;HMODULE pin;};
DWORD WINAPI Worker(void* value){
    HMODULE pin{};{std::unique_ptr<ThreadContext> context(static_cast<ThreadContext*>(value));pin=context->pin;Work(context->state);}
    FreeLibraryAndExitThread(pin,0);
}
struct Unknown{IUnknown* p{};~Unknown(){if(p)p->Release();}};
class SearchObject final:public Search,public Control{
    std::atomic<ULONG> refs_{1};std::atomic<bool> canceled_{};std::mutex mutex_;std::shared_ptr<State> state_;
    std::shared_ptr<State> Snapshot(){std::lock_guard lock(mutex_);return state_;}
    ~SearchObject(){Retire(Snapshot());}
    void Rollback(const std::shared_ptr<State>& s){
        bool canceled;{std::lock_guard lock(s->mutex);canceled=s->stopped;}
        Retire(s);
        if(!canceled){auto fresh=std::make_shared<State>(s->catalog,s->provider);fresh->configured=s->configured;fresh->service=s->service;
            std::lock_guard lock(mutex_);if(state_==s&&!canceled_)state_=std::move(fresh);}
    }
public:
    SearchObject(std::shared_ptr<Catalog> c,int i):state_(std::make_shared<State>(std::move(c),i)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{
        if(!p)return E_POINTER;*p=nullptr;
        if(id==IID_IUnknown||id==SearchId)*p=static_cast<Search*>(this);
        else if(id==ControlId)*p=static_cast<Control*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Configure(LPCWSTR name,LPCWSTR url,DWORD flags)override{
        if(!name||!url)return E_POINTER;
        try{if(flags||!ValidUrl(url))return E_INVALIDARG;auto s=Snapshot();std::lock_guard lock(s->mutex);
            if(s->initializing||s->callback||s->stopped)return E_UNEXPECTED;s->service={name,url};s->configured=true;return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE Cancel()override{canceled_=true;Snapshot()->Stop();return S_OK;}
    HRESULT STDMETHODCALLTYPE Initialize(IUnknown* host,const Network* network)override{
        if(!host)return E_POINTER;if(network&&network->size!=sizeof(Network))return E_INVALIDARG;
        auto s=Snapshot();{std::lock_guard lock(s->mutex);if(s->initializing||s->callback||s->stopped)return E_UNEXPECTED;s->initializing=true;}
        HRESULT hr=E_OUTOFMEMORY;
        try{
            NetworkValue value;if(network){value.type=network->type;value.port=network->port;value.server=network->server?network->server:L"";
                value.user=network->username?network->username:L"";value.password=network->password?network->password:L"";}
            Unknown marker,callback;hr=host->QueryInterface(HostId,reinterpret_cast<void**>(&marker.p));
            if(SUCCEEDED(hr)&&!marker.p)hr=E_NOINTERFACE;
            if(SUCCEEDED(hr)){hr=host->QueryInterface(CallbackId,reinterpret_cast<void**>(&callback.p));if(SUCCEEDED(hr)&&!callback.p)hr=E_NOINTERFACE;}
            if(SUCCEEDED(hr)){
                auto context=std::make_unique<ThreadContext>();context->state=s;
                if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&context->pin))hr=HRESULT_FROM_WIN32(GetLastError());
                else{
                    {std::lock_guard lock(s->mutex);if(s->stopped)hr=E_ABORT;
                        else{s->network=std::move(value);s->callback=static_cast<Callback*>(callback.p);callback.p=nullptr;
                            s->thread=CreateThread(nullptr,0,Worker,context.get(),0,&s->thread_id);if(!s->thread)hr=HRESULT_FROM_WIN32(GetLastError());}}
                    if(FAILED(hr))FreeLibrary(context->pin);
                    else{context.release();auto wait=WaitForSingleObject(s->ready_event,5000);
                        std::lock_guard lock(s->mutex);hr=s->stopped?E_ABORT:wait==WAIT_OBJECT_0?s->startup:E_FAIL;s->initializing=false;}
                }
            }
            if(SUCCEEDED(hr))return hr;
        }catch(...){hr=E_OUTOFMEMORY;}
        try{Rollback(s);}catch(...){}return hr;
    }
    HRESULT STDMETHODCALLTYPE Find(LPCWSTR artist,LPCWSTR title)override{
        if(!title)return E_POINTER;
        try{std::wstring a=artist?artist:L"",t=title;auto next=std::make_shared<Abort>();std::shared_ptr<Abort> old;auto s=Snapshot();
            {std::lock_guard lock(s->mutex);if(s->stopped)return E_ABORT;if(!s->ready||s->initializing)return FAILED(s->failure)?s->failure:E_UNEXPECTED;
                s->artist=std::move(a);s->title=std::move(t);old=std::exchange(s->abort,std::move(next));s->rows.clear();++s->generation;s->command=1;SetEvent(s->wake);}
            if(old)old->Cancel();return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE Download(int index)override{
        try{auto next=std::make_shared<Abort>();std::shared_ptr<Abort> old;auto s=Snapshot();
            {std::lock_guard lock(s->mutex);if(s->stopped)return E_ABORT;if(!s->ready||s->initializing)return FAILED(s->failure)?s->failure:E_UNEXPECTED;
                if(index<0||static_cast<size_t>(index)>=s->rows.size())return E_INVALIDARG;
                s->selected=s->rows[index];old=std::exchange(s->abort,std::move(next));++s->generation;s->command=2;SetEvent(s->wake);}
            if(old)old->Cancel();return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE GetExtra(LPWSTR* title,LPWSTR* url)override{
        if(!title||!url)return E_POINTER;*title=*url=nullptr;
        try{auto s=Snapshot();{std::lock_guard lock(s->mutex);if(s->configured)return E_FAIL;}
            std::lock_guard lock(s->catalog->mutex);if(s->catalog->extra_title.empty()||s->catalog->extra_url.empty())return E_FAIL;
            *title=Copy(s->catalog->extra_title);*url=Copy(s->catalog->extra_url);return S_OK;
        }catch(...){CoTaskMemFree(*title);CoTaskMemFree(*url);*title=*url=nullptr;return E_OUTOFMEMORY;}
    }
};
}
Search* CreateSearch(std::shared_ptr<Catalog> catalog,int index){return new SearchObject(std::move(catalog),index);}
}
