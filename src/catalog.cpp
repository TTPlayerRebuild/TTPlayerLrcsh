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
std::wstring CatalogPath(const wchar_t* extension){auto path=ModulePath();auto at=path.find_last_of(L'.');if(at==path.npos)path+=extension;else path.replace(at,path.size()-at,extension);return path;}
struct File{HANDLE h;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
std::shared_ptr<const TlsTrust> LocalTrust(const Service& service){
    if(service.ca_file.empty())return {};
    auto trust=std::make_shared<TlsTrust>();trust->invalid=true;
    // Only an explicit local catalog can select a sibling PEM file. No path
    // traversal, UNC lookup, or remote-catalog trust configuration is allowed.
    auto name=Wide(service.ca_file);
    if(!SecureUrl(service.url) || name==L"." || name==L".." ||
       name.find_first_of(L"\\/:*?\"<>|")!=name.npos || name.empty() ||
       name.back()==L'.' || name.back()==L' ' ||
       std::any_of(name.begin(),name.end(),[](wchar_t c){return c<L' ';}))return trust;
    auto path=ModulePath();path.resize(path.find_last_of(L"\\/")+1);path+=name;
    File file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    LARGE_INTEGER size{};
    if(file.h==INVALID_HANDLE_VALUE || GetFileType(file.h)!=FILE_TYPE_DISK ||
       !GetFileSizeEx(file.h,&size) || size.QuadPart<=0 || size.QuadPart>BodyLimit-1)return trust;
    trust->pem.resize(static_cast<size_t>(size.QuadPart));DWORD count{};
    if(!ReadFile(file.h,trust->pem.data(),static_cast<DWORD>(trust->pem.size()),&count,nullptr) || count!=trust->pem.size() ||
       trust->pem.find('\0')!=trust->pem.npos || trust->pem.find("-----BEGIN CERTIFICATE-----")==trust->pem.npos)return trust;
    trust->origin=UrlOrigin(service.url);trust->invalid=false;return trust;
}
std::vector<Service> Parse(const XmlNode& root,std::string_view bytes,bool local,bool extended=false){
    if(root.name!="ttp_lrcsvr")throw Failure(32005,"Invalid service root");std::vector<Service> out;
    for(const auto& n:root.children)if(n.name=="server"){
        // Match the rebuilt editor's bound. Never silently truncate a user XML;
        // INI and remote legacy catalogs still use the original two slots.
        if(out.size()==(extended?128u:2u)){
            if(extended)throw Failure(32005,"Too many lyric servers");
            break;
        }
        Service s{Wide(n.Attr("name")),Wide(n.Attr("url"))};
        if(s.name.empty() || !ValidUrl(s.url)){out.push_back({});continue;}
        s.ca_file=local?n.Attr("ca_file"):std::string{};
        s.source_xml=ServerXml(bytes,n,s.ca_file);
        if(local)s.trust=LocalTrust(s);
        out.push_back(std::move(s));
    }
    return out;
}
std::string Read(bool& explicit_xml){
    auto path=CatalogPath(L".xml");
    const auto attributes=GetFileAttributesW(path.c_str()),error=GetLastError();
    // Only an absent XML permits legacy fallback. Invalid, empty, inaccessible
    // XML or a directory must not reactivate the remote INI refresh path.
    explicit_xml=attributes!=INVALID_FILE_ATTRIBUTES || (error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND);
    if(!explicit_xml)path=CatalogPath(L".ini");
    File file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};if(file.h==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.h,&size)||size.QuadPart<0||size.QuadPart>BodyLimit)return {};
    std::string bytes(static_cast<size_t>(size.QuadPart),0);DWORD n{};if(!ReadFile(file.h,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)||n!=bytes.size())return {};return bytes;
}
void Save(const std::string& bytes){
    // The original persists server entries; promotional <extra> links are
    // session-only and are intentionally not restored on the next launch.
    auto ini=CatalogPath(L".ini");auto parent=ini.substr(0,ini.find_last_of(L"\\/"));wchar_t temp[MAX_PATH]{};
    if(!GetTempFileNameW(parent.c_str(),L"lrs",0,temp)){OutputDebugStringW(L"ttp_lrcsh: cannot create catalog temporary file\n");return;}
    bool ok=false;{File file{CreateFileW(temp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};DWORD n{};
        ok=file.h!=INVALID_HANDLE_VALUE && WriteFile(file.h,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(file.h);}
    if(!ok||!MoveFileExW(temp,ini.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp);OutputDebugStringW(L"ttp_lrcsh: cannot persist catalog\n");}
}
}
Catalog::Catalog(){
    bool explicit_xml=false;
    try{auto bytes=Read(explicit_xml);if(!bytes.empty()){auto root=ParseXml(bytes,true);services=Parse(root,bytes,true,explicit_xml);for(auto& n:root.children)if(n.name=="extra"){extra_title=Wide(n.Attr("title"));extra_url=Wide(n.Attr("url"));break;}}}catch(...){}
    // XML is user-managed by the rebuilt player. The original player can
    // read it through this DLL, but must never replace it via ?svrlst.
    refreshed=explicit_xml;
    if(services.empty()&&!explicit_xml)services={{Resource(32000),Resource(32002)},{Resource(32001),Resource(32003)}};
    // Keep discoverable error factories for a broken/empty XML so the rebuilt
    // host can still offer its catalog editor. Valid XML uses its actual count.
    if(!explicit_xml||services.empty())while(services.size()<2)services.push_back({});
}
Service Catalog::At(int index){std::lock_guard lock(mutex);return index>=0&&index<static_cast<int>(services.size())?services[index]:Service{};}
void Catalog::Refresh(Transport& transport,const NetworkValue& net,const std::shared_ptr<Abort>& abort){
    Service first;{std::lock_guard lock(mutex);if(refreshed)return;refreshed=true;first=services[0];}
    if(!ValidUrl(first.url))return;
    try{auto response=transport.Get(first.url+L"?svrlst&",net,abort,first.trust.get());if(response.body.empty())return;
        auto root=ParseXml(response.body,true);auto updated=Parse(root,response.body,false);
        std::wstring title,link;bool extra=false;for(const auto& n:root.children)if(n.name=="extra"){title=Wide(n.Attr("title"));link=Wide(n.Attr("url"));extra=true;break;}
        std::vector<std::string> persisted;
        {std::lock_guard lock(mutex);for(size_t i=0;i<updated.size();++i){
            auto& old=services[i];auto& next=updated[i];
            if(!next.url.empty() && !(ValidUrl(old.url)&&SecureUrl(old.url)&&!SecureUrl(next.url))){
                if(old.trust && UrlOrigin(old.url)==UrlOrigin(next.url)){
                    next.trust=old.trust;next.ca_file=old.ca_file;
                    const auto node=ParseXml(next.source_xml,true);next.source_xml=ServerXml(next.source_xml,node,next.ca_file);
                }
                old=std::move(next);
            }else OutputDebugStringW(L"ttp_lrcsh: kept catalog slot after invalid or downgraded update\n");
            persisted.push_back(old.source_xml.empty()?"<server name=\""+EscapeXml(Utf8(old.name))+"\" url=\""+EscapeXml(Utf8(old.url))+"\"/>":old.source_xml);
        }
            if(extra){extra_title=std::move(title);extra_url=std::move(link);}}
        if(!updated.empty())Save(CatalogXml(response.body,root,persisted));
    }catch(const Canceled&){throw;}catch(...){} // Failed refresh retains the usable configuration.
}
std::shared_ptr<Catalog> Services(){std::lock_guard lock(catalog_mutex);if(!cached_catalog)cached_catalog=std::make_shared<Catalog>();return cached_catalog;}
}
