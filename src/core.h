#pragma once
#include "ttp_lrcsh_abi.h"
#include <wininet.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace ttp::lrc {
extern HMODULE module;
constexpr size_t BodyLimit=2*1024*1024;
struct Failure : std::runtime_error {
    unsigned code;
    bool server_message;
    explicit Failure(unsigned n,const char* text,bool server=false):runtime_error(text),code(n),server_message(server){}
};
struct Canceled {};
std::wstring Wide(std::string_view);
std::string Utf8(std::wstring_view);
std::wstring Resource(unsigned);
LPWSTR Copy(std::wstring_view);
std::wstring ModulePath();
struct NetworkValue {
    int type{1}; DWORD port{}; std::wstring server,user,password;
    Network View() const {return {sizeof(Network),type,server.c_str(),port,user.c_str(),password.c_str()};}
};
struct Abort {
    std::atomic<bool> canceled{};
    const DWORD started{GetTickCount()};
    std::mutex mutex;
    HINTERNET request{};
    void Cancel() noexcept;
    void Attach(HINTERNET);
    void Close() noexcept;
    bool Expired() const {return static_cast<DWORD>(GetTickCount()-started)>=65000;}
    void Check() const {if(canceled)throw Canceled{};if(Expired())throw Failure(408,"");}
    ~Abort(){Close();}
};
struct Response {std::string body; std::wstring title,url; unsigned status{200};};
// A private CA is an explicit local, per-origin setting. Remote catalogs may
// neither introduce it nor widen it to another origin.
struct TlsTrust {std::wstring origin;std::string pem;bool invalid{};};
class Transport {
public:
    Transport(); ~Transport();
    void Initialize(const NetworkValue&);
    Response Get(const std::wstring&,const NetworkValue&,const std::shared_ptr<Abort>&,const TlsTrust* trust=nullptr);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
struct XmlNode {
    std::string name; std::map<std::string,std::string> attrs; std::vector<XmlNode> children;
    // Source spans permit lossless catalog persistence without reconstructing
    // unknown attributes, text, comments, or extension elements.
    size_t begin{},end{};
    std::vector<std::pair<std::string,std::pair<size_t,size_t>>> attribute_spans;
    std::string Attr(const char* key) const {auto i=attrs.find(key);return i==attrs.end()?std::string{}:i->second;}
};
XmlNode ParseXml(std::string_view,bool tolerant=false);
std::string CatalogXml(std::string_view,const XmlNode&,const std::vector<std::string>& servers={});
std::string ServerXml(std::string_view,const XmlNode&,std::string_view ca_file={});
std::string EscapeXml(std::string_view);
struct Service {std::wstring name,url;std::shared_ptr<const TlsTrust> trust;std::string ca_file,source_xml;};
struct Catalog {
    std::mutex mutex; bool refreshed{}; std::vector<Service> services;
    std::wstring extra_title,extra_url;
    Catalog();
    Service At(int);
    void Refresh(Transport&,const NetworkValue&,const std::shared_ptr<Abort>&);
};
std::shared_ptr<Catalog> Services();
Search* CreateSearch(std::shared_ptr<Catalog>,int);
bool ValidUrl(std::wstring_view);
bool SecureUrl(const std::wstring&);
std::wstring UrlOrigin(const std::wstring&);
struct Candidate {int id{}; std::string artist,title;};
std::wstring SearchUrl(std::wstring_view,std::wstring_view,std::wstring_view);
int DownloadCode(unsigned,std::string_view);
std::vector<Candidate> FindLyrics(Transport&,const Service&,const NetworkValue&,
    const std::shared_ptr<Abort>&,std::wstring_view,std::wstring_view);
struct Downloaded {std::wstring text,title,url;};
Downloaded DownloadLyric(Transport&,const Service&,const NetworkValue&,
    const std::shared_ptr<Abort>&,const Candidate&);
}
