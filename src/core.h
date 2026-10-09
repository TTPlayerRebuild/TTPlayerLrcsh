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
    std::mutex mutex;
    HINTERNET request{};
    void Cancel() noexcept;
    void Attach(HINTERNET);
    void Close() noexcept;
    void Check() const {if(canceled)throw Canceled{};}
    ~Abort(){Close();}
};
struct Response {std::string body; std::wstring title,url;};
class Transport {
public:
    Transport(); ~Transport();
    Response Get(const std::wstring&,const NetworkValue&,const std::shared_ptr<Abort>&);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
struct XmlNode {
    std::string name; std::map<std::string,std::string> attrs; std::vector<XmlNode> children;
    std::string Attr(const char* key) const {auto i=attrs.find(key);return i==attrs.end()?std::string{}:i->second;}
};
XmlNode ParseXml(std::string_view,bool tolerant=false);
std::string EscapeXml(std::string_view);
struct Service {std::wstring name,url;};
struct Catalog {
    std::mutex mutex; bool refreshed{}; std::vector<Service> services;
    std::wstring extra_title,extra_url;
    Catalog();
    Service At(int);
    void Refresh(Transport&,const NetworkValue&,const std::shared_ptr<Abort>&);
};
std::shared_ptr<Catalog> Services();
bool ValidUrl(std::wstring_view);
struct Candidate {int id{}; std::string artist,title;};
std::wstring SearchUrl(std::wstring_view,std::wstring_view,std::wstring_view);
int DownloadCode(unsigned,std::string_view);
std::vector<Candidate> FindLyrics(Transport&,const Service&,const NetworkValue&,
    const std::shared_ptr<Abort>&,std::wstring_view,std::wstring_view);
struct Downloaded {std::wstring text,title,url;};
Downloaded DownloadLyric(Transport&,const Service&,const NetworkValue&,
    const std::shared_ptr<Abort>&,const Candidate&);
}
