#include "core.h"

namespace ttp::lrc {
HMODULE module{};
namespace {
struct State {
    std::mutex mutex;
    HANDLE wake{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    Callback* callback{};
    std::shared_ptr<Catalog> catalog;
    NetworkValue network;
    Service service;
    int provider{},command{};
    bool configured{},busy{},stopped{};
    std::wstring artist,title;
    Candidate selected;
    std::vector<Candidate> rows;
    std::shared_ptr<Abort> abort;
    explicit State(std::shared_ptr<Catalog> c,int i):catalog(std::move(c)),provider(i) {
        if(!wake)throw std::bad_alloc();
    }
    ~State(){if(callback)callback->Release();if(wake)CloseHandle(wake);}
    void Stop() {
        std::shared_ptr<Abort> current;
        {std::lock_guard lock(mutex);stopped=true;current=abort;SetEvent(wake);}
        if(current)current->Cancel();
    }
    bool Alive(){std::lock_guard lock(mutex);return !stopped;}
    void Idle(){std::lock_guard lock(mutex);busy=false;}
};
void Work(const std::shared_ptr<State>& s) noexcept {
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try {
        Transport transport;
        for(;;) {
            WaitForSingleObject(s->wake,INFINITE);
            int command{};std::wstring artist,title;Candidate row;std::shared_ptr<Abort> abort;
            {std::lock_guard lock(s->mutex);if(s->stopped)break;
                command=std::exchange(s->command,0);artist=s->artist;title=s->title;row=s->selected;abort=s->abort;}
            if(!command)continue;
            try {
                if(command==1) {
                    if(!s->configured)s->catalog->Refresh(transport,s->network,abort);
                    const auto service=s->configured?s->service:s->catalog->At(s->provider);
                    abort->Check();
                    if(s->Alive())s->callback->OnServer(service.name.c_str());
                    auto rows=FindLyrics(transport,service,s->network,abort,artist,title);
                    std::vector<std::wstring> artists,titles;
                    std::vector<LPCWSTR> ap,tp;
                    for(const auto& r:rows){artists.push_back(Wide(r.artist));titles.push_back(Wide(r.title));}
                    for(size_t i=0;i<rows.size();++i){ap.push_back(artists[i].c_str());tp.push_back(titles[i].c_str());}
                    {std::lock_guard lock(s->mutex);s->rows=std::move(rows);s->service=service;s->busy=false;}
                    if(s->Alive())s->callback->OnResults(static_cast<int>(ap.size()),ap.data(),tp.data());
                } else {
                    auto result=DownloadLyric(transport,s->service,s->network,abort,row);
                    s->Idle();
                    if(s->Alive())s->callback->OnDownload(static_cast<int>(result.text.size()),result.text.c_str(),result.title.c_str(),result.url.c_str());
                }
            } catch(const Canceled&) {s->Idle();
            } catch(const Failure& e) {
                s->Idle();
                if(s->Alive()){
                    auto text=Resource(e.code);if(text.empty())text=Resource(32020);
                    // Server messages are UTF-8. Unknown HTTP errors use the localized fallback.
                    if(e.server_message && *e.what())try{text=Wide(e.what());}catch(...){}
                    s->callback->OnError(text.c_str());
                }
            } catch(...) {s->Idle();if(s->Alive())s->callback->OnError(Resource(32005).c_str());}
        }
    } catch(...) {s->Idle();try{if(s->Alive())s->callback->OnError(Resource(32005).c_str());}catch(...) {}}
    if(SUCCEEDED(com))CoUninitialize();
}
struct ThreadContext {std::shared_ptr<State> state;HMODULE pin;};
DWORD WINAPI Worker(void* value) {
    HMODULE pin{};
    {
        std::unique_ptr<ThreadContext> context(static_cast<ThreadContext*>(value));
        pin=context->pin;Work(context->state);
        // Release all C++ state while this DLL is still mapped. In particular a
        // callback can release the final Search reference on this worker.
    }
    FreeLibraryAndExitThread(pin,0);
}
class SearchObject final : public Search,public Control {
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<State> state_;
    HANDLE thread_{};DWORD thread_id_{};
    ~SearchObject(){state_->Stop();if(thread_){if(GetCurrentThreadId()!=thread_id_)WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);}}
public:
    SearchObject(std::shared_ptr<Catalog> c,int i):state_(std::make_shared<State>(std::move(c),i)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {
        if(!p)return E_POINTER;*p=nullptr;
        if(id==IID_IUnknown||id==SearchId)*p=static_cast<Search*>(this);
        else if(id==ControlId)*p=static_cast<Control*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Configure(LPCWSTR name,LPCWSTR url,DWORD flags) override {
        if(!name||!url)return E_POINTER;
        try{if(flags||!ValidUrl(url))return E_INVALIDARG;std::lock_guard lock(state_->mutex);
            if(state_->callback||state_->stopped)return E_UNEXPECTED;
            state_->service={name,url};state_->configured=true;return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE Cancel() override{state_->Stop();return S_OK;}
    HRESULT STDMETHODCALLTYPE Initialize(IUnknown* host,const Network* network) override {
        if(!host)return E_POINTER;if(network&&network->size!=sizeof(Network))return E_INVALIDARG;
        try {
            std::lock_guard lock(state_->mutex);
            if(state_->callback||state_->stopped)return E_UNEXPECTED;
            IUnknown* marker{};auto hr=host->QueryInterface(HostId,reinterpret_cast<void**>(&marker));
            if(FAILED(hr))return hr;if(!marker)return E_NOINTERFACE;marker->Release();
            Callback* callback{};hr=host->QueryInterface(CallbackId,reinterpret_cast<void**>(&callback));
            if(FAILED(hr))return hr;if(!callback)return E_NOINTERFACE;
            state_->callback=callback;
            if(network){auto& n=state_->network;n.type=network->type;n.port=network->port;
                n.server=network->server?network->server:L"";n.user=network->username?network->username:L"";n.password=network->password?network->password:L"";}
            auto context=std::make_unique<ThreadContext>();context->state=state_;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&context->pin))return HRESULT_FROM_WIN32(GetLastError());
            thread_=CreateThread(nullptr,0,Worker,context.get(),0,&thread_id_);
            if(!thread_){FreeLibrary(context->pin);return HRESULT_FROM_WIN32(GetLastError());}
            context.release();return S_OK;
        } catch(...) {return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE Find(LPCWSTR artist,LPCWSTR title) override {
        if(!artist||!title)return E_POINTER;
        try{std::lock_guard lock(state_->mutex);if(state_->stopped)return E_ABORT;if(!thread_)return E_UNEXPECTED;
            if(state_->busy)return HRESULT_FROM_WIN32(ERROR_BUSY);
            state_->artist=artist;state_->title=title;state_->abort=std::make_shared<Abort>();
            state_->rows.clear();state_->busy=true;state_->command=1;SetEvent(state_->wake);return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE Download(int index) override {
        try{std::lock_guard lock(state_->mutex);if(state_->stopped)return E_ABORT;if(!thread_)return E_UNEXPECTED;
            if(index<0||static_cast<size_t>(index)>=state_->rows.size())return E_INVALIDARG;
            if(state_->busy)return HRESULT_FROM_WIN32(ERROR_BUSY);
            state_->selected=state_->rows[index];state_->abort=std::make_shared<Abort>();
            state_->busy=true;state_->command=2;SetEvent(state_->wake);return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE GetExtra(LPWSTR* title,LPWSTR* url) override {
        if(!title||!url)return E_POINTER;*title=*url=nullptr;
        try{std::lock_guard lock(state_->catalog->mutex);
            if(state_->catalog->extra_title.empty()||state_->catalog->extra_url.empty())return E_FAIL;
            *title=Copy(state_->catalog->extra_title);
            *url=Copy(state_->catalog->extra_url);if(!*title||!*url)throw std::bad_alloc();return S_OK;
        }catch(...){CoTaskMemFree(*title);CoTaskMemFree(*url);*title=*url=nullptr;return E_OUTOFMEMORY;}
    }
};
class CreatorObject final : public Creator {
    std::atomic<ULONG> refs_{1};std::shared_ptr<Catalog> catalog_;int index_;
public:
    CreatorObject(std::shared_ptr<Catalog> c,int i):catalog_(std::move(c)),index_(i){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=CreatorId)return E_NOINTERFACE;*p=static_cast<Creator*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Create(Search** p) override {if(!p)return E_POINTER;*p=nullptr;try{*p=new SearchObject(catalog_,index_);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
    HRESULT STDMETHODCALLTYPE Name(LPWSTR* p) override {if(!p)return E_POINTER;*p=nullptr;try{*p=Copy(catalog_->At(index_).name);return *p?S_OK:E_OUTOFMEMORY;}catch(...){return E_OUTOFMEMORY;}}
};
class AddInObject final : public AddIn {
    std::atomic<ULONG> refs_{1};std::shared_ptr<Catalog> catalog_{Services()};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=AddInId)return E_NOINTERFACE;*p=static_cast<AddIn*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Enumerate(int i,GUID* id,IUnknown** p) override {
        if(!id||!p)return E_POINTER;*p=nullptr;if(i<0||i>1)return E_INVALIDARG;
        try{*p=new CreatorObject(catalog_,i);*id=CreatorId;return S_OK;}catch(...){return E_OUTOFMEMORY;}
    }
};
}
}
extern "C" HRESULT WINAPI ttpGetSoundAddIn(ttp::lrc::AddIn** out) {
    if(!out)return E_POINTER;*out=nullptr;
    try{*out=new ttp::lrc::AddInObject;return S_OK;}catch(...){return E_OUTOFMEMORY;}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH)ttp::lrc::module=instance;
    return TRUE;
}
