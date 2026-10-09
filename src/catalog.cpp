#include "core.h"
#include <algorithm>

namespace ttp::lrc {
std::wstring Resource(unsigned id){wchar_t buffer[2048]{};int n=LoadStringW(module,id,buffer,2048);return n>0?std::wstring(buffer,n):std::wstring{};}
LPWSTR Copy(std::wstring_view text){auto p=static_cast<LPWSTR>(CoTaskMemAlloc((text.size()+1)*sizeof(wchar_t)));if(!p)throw std::bad_alloc();std::copy(text.begin(),text.end(),p);p[text.size()]=0;return p;}
std::wstring ModulePath(){std::wstring path(32768,0);auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));if(!n||n==path.size())throw Failure(32005,"Cannot read plugin path");path.resize(n);return path;}
namespace {
// Avoid MSVC's dynamic local-static TLS guard when an XP host loads the DLL late.
std::mutex catalog_mutex;
std::shared_ptr<Catalog> cached_catalog;
std::wstring Ini(){auto path=ModulePath();auto at=path.find_last_of(L'.');if(at==path.npos)path+=L".ini";else path.replace(at,path.size()-at,L".ini");return path;}
struct File{HANDLE h;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
std::vector<Service> Parse(const XmlNode& root){if(root.name!="ttp_lrcsvr")throw Failure(32005,"Invalid service root");std::vector<Service> out;
    for(const auto& n:root.children)if(n.name=="server"&&out.size()<2){Service s{Wide(n.Attr("name")),Wide(n.Attr("url"))};if(s.name.empty()||!ValidUrl(s.url))throw Failure(32005,"Invalid lyric server");out.push_back(std::move(s));}
    return out;
}
std::string Read(){File file{CreateFileW(Ini().c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};if(file.h==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.h,&size)||size.QuadPart<0||size.QuadPart>BodyLimit)return {};
    std::string bytes(static_cast<size_t>(size.QuadPart),0);DWORD n{};if(!ReadFile(file.h,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)||n!=bytes.size())return {};return bytes;
}
void Save(const std::string& bytes){
    // The original persists server entries; promotional <extra> links are
    // session-only and are intentionally not restored on the next launch.
    auto ini=Ini();auto parent=ini.substr(0,ini.find_last_of(L"\\/"));wchar_t temp[MAX_PATH]{};
    if(!GetTempFileNameW(parent.c_str(),L"lrs",0,temp)){OutputDebugStringW(L"ttp_lrcsh: cannot create catalog temporary file\n");return;}
    bool ok=false;{File file{CreateFileW(temp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};DWORD n{};
        ok=file.h!=INVALID_HANDLE_VALUE && WriteFile(file.h,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(file.h);}
    if(!ok||!MoveFileExW(temp,ini.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp);OutputDebugStringW(L"ttp_lrcsh: cannot persist catalog\n");}
}
}
Catalog::Catalog(){
    try{auto bytes=Read();if(!bytes.empty()){auto root=ParseXml(bytes,true);services=Parse(root);for(auto& n:root.children)if(n.name=="extra"){extra_title=Wide(n.Attr("title"));extra_url=Wide(n.Attr("url"));}}}catch(...){}
    if(services.empty())services={{Resource(32000),Resource(32002)},{Resource(32001),Resource(32003)}};
    while(services.size()<2)services.push_back({});
}
Service Catalog::At(int index){std::lock_guard lock(mutex);return index>=0&&index<static_cast<int>(services.size())?services[index]:Service{};}
void Catalog::Refresh(Transport& transport,const NetworkValue& net,const std::shared_ptr<Abort>& abort){
    std::wstring url;{std::lock_guard lock(mutex);if(refreshed)return;refreshed=true;url=services[0].url;}
    if(!ValidUrl(url))return;
    try{auto response=transport.Get(url+L"?svrlst&",net,abort);if(response.body.empty())return;
        auto root=ParseXml(response.body,true);auto updated=Parse(root);
        std::wstring title,link;bool extra=false;for(const auto& n:root.children)if(n.name=="extra"){title=Wide(n.Attr("title"));link=Wide(n.Attr("url"));extra=true;break;}
        {std::lock_guard lock(mutex);for(size_t i=0;i<updated.size();++i)services[i]=std::move(updated[i]);
            if(extra){extra_title=std::move(title);extra_url=std::move(link);}}
        if(!updated.empty())Save(CatalogXml(response.body,root));
    }catch(const Canceled&){throw;}catch(...){} // Failed refresh retains the usable configuration.
}
std::shared_ptr<Catalog> Services(){std::lock_guard lock(catalog_mutex);if(!cached_catalog)cached_catalog=std::make_shared<Catalog>();return cached_catalog;}
}
