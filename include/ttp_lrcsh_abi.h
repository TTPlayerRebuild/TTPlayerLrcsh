#pragma once
#include <windows.h>
#include <unknwn.h>

namespace ttp::lrc {
inline constexpr GUID AddInId{0xeec6c534,0xfeba,0x421e,{0xaa,0x5d,0x10,0xac,0x66,0xf9,0x87,0x84}};
inline constexpr GUID CreatorId{0x9d5ae963,0x7df6,0x4323,{0xb4,0xcf,0xfb,0xda,0x15,0x9b,0xff,0x15}};
inline constexpr GUID SearchId{0xbf17b3e8,0x7e33,0x45e7,{0x93,0x42,0xcd,0xd3,0x4b,0xa5,0x5a,0xf9}};
inline constexpr GUID CallbackId{0xca540428,0x2528,0x4bbc,{0x87,0xb0,0x66,0x05,0xf2,0x18,0x10,0xc9}};
inline constexpr GUID HostId{0x6f1f38a8,0x3021,0x4e2e,{0xbb,0xfb,0x7f,0x61,0x6a,0x5c,0x3d,0x62}};
// Optional interface. Never append fields or methods to a legacy interface.
inline constexpr GUID ControlId{0xb25e8a91,0x0a73,0x46bb,{0x9c,0x25,0xa8,0xe7,0x3c,0x51,0x9d,0x64}};
struct Network {
    DWORD size; int type; LPCWSTR server; DWORD port; LPCWSTR username; LPCWSTR password;
};
static_assert(sizeof(Network)==24,"The original plugin ABI is x86 only");
struct Callback : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE OnResults(int,LPCWSTR*,LPCWSTR*)=0;
    virtual HRESULT STDMETHODCALLTYPE OnDownload(int,LPCWSTR,LPCWSTR,LPCWSTR)=0;
    virtual HRESULT STDMETHODCALLTYPE OnError(LPCWSTR)=0;
    virtual HRESULT STDMETHODCALLTYPE OnServer(LPCWSTR)=0;
};
struct Search : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(IUnknown*,const Network*)=0;
    virtual HRESULT STDMETHODCALLTYPE Find(LPCWSTR artist,LPCWSTR title)=0;
    virtual HRESULT STDMETHODCALLTYPE Download(int index)=0;
    virtual HRESULT STDMETHODCALLTYPE GetExtra(LPWSTR*,LPWSTR*)=0;
};
struct Creator : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Create(Search**)=0;
    virtual HRESULT STDMETHODCALLTYPE Name(LPWSTR*)=0;
};
struct AddIn : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Enumerate(int,GUID*,IUnknown**)=0;
};
struct Control : IUnknown {
    // Before Initialize only. Explicit services never run remote svrlst refresh.
    // flags must be zero in version 1. Strings are copied before return.
    virtual HRESULT STDMETHODCALLTYPE Configure(LPCWSTR name,LPCWSTR url,DWORD flags)=0;
    // Terminal cancellation; no new operation accepted. Thread-safe, no wait.
    virtual HRESULT STDMETHODCALLTYPE Cancel()=0;
};
using GetAddIn=HRESULT (WINAPI*)(AddIn**);
}
