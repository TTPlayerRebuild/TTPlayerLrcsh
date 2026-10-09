#include "core.h"

namespace ttp::lrc {
HMODULE module{};
namespace {
class CreatorObject final : public Creator {
    std::atomic<ULONG> refs_{1};std::shared_ptr<Catalog> catalog_;int index_;
public:
    CreatorObject(std::shared_ptr<Catalog> c,int i):catalog_(std::move(c)),index_(i){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=CreatorId)return E_NOINTERFACE;*p=static_cast<Creator*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Create(Search** p) override {if(!p)return E_POINTER;*p=nullptr;try{*p=CreateSearch(catalog_,index_);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
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
