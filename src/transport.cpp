#include "core.h"
#include "ttp_https.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <optional>

namespace ttp::lrc {
namespace {
struct Handle { HINTERNET value{};~Handle(){if(value)InternetCloseHandle(value);} operator HINTERNET()const{return value;} };
void Check(BOOL ok){if(!ok)throw Failure(32004,"HTTP transport error");}
std::wstring Lower(std::wstring s) {
    std::transform(s.begin(),s.end(),s.begin(),towlower); return s;
}
std::wstring Trim(std::wstring_view s) {
    const auto first=s.find_first_not_of(L" \t");
    return first==s.npos ? L"" : std::wstring(s.substr(first,s.find_last_not_of(L" \t")-first+1));
}
struct Address {
    std::wstring host, path, target, origin;
    INTERNET_PORT port{}; bool secure{};
    explicit Address(const std::wstring& url) {
        URL_COMPONENTSW c{sizeof(c)};
        c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=static_cast<DWORD>(-1);
        Check(InternetCrackUrlW(url.c_str(),static_cast<DWORD>(url.size()),0,&c));
        secure=c.nScheme==INTERNET_SCHEME_HTTPS;
        if(c.nScheme!=INTERNET_SCHEME_HTTP && !secure) throw std::runtime_error("Legacy lyric transport requires HTTP");
        if(!c.lpszHostName||!c.dwHostNameLength)throw Failure(32005,"Missing lyric host");
        host.assign(c.lpszHostName,c.dwHostNameLength); port=c.nPort;
        path=c.lpszUrlPath&&c.dwUrlPathLength ? std::wstring(c.lpszUrlPath,c.dwUrlPathLength) : L"/";
        target=path;
        if(c.lpszExtraInfo&&c.dwExtraInfoLength) target.append(c.lpszExtraInfo,c.dwExtraInfoLength);
        origin=(secure?L"https://":L"http://")+Lower(host)+L":"+std::to_wstring(port);
    }
};
std::wstring Header(HINTERNET request,DWORD query,const wchar_t* name=nullptr,DWORD* index=nullptr) {
    // Query into a bounded buffer: also handles repeated Set-Cookie fields.
    wchar_t buffer[8192]{};
    if(name) wcscpy_s(buffer,name);
    DWORD bytes=sizeof(buffer);
    if(!HttpQueryInfoW(request,query,buffer,&bytes,index)) {
        if(GetLastError()==ERROR_INSUFFICIENT_BUFFER) throw std::runtime_error("Lyric HTTP header too large");
        return {};
    }
    return std::wstring(buffer,bytes/sizeof(wchar_t));
}
bool PathMatches(std::wstring_view path,std::wstring_view cookie) {
    return path.starts_with(cookie) && (path.size()==cookie.size() || cookie.back()==L'/' || path[cookie.size()]==L'/');
}
ULONGLONG Now() { FILETIME f{}; GetSystemTimeAsFileTime(&f); return (ULONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime; }
bool CompleteRange(std::wstring_view range,size_t size) {
    if(!range.starts_with(L"bytes "))return false;
    size_t pos=6;
    auto number=[&](ULONGLONG& value){
        const auto start=pos;value=0;
        while(pos<range.size() && range[pos]>=L'0' && range[pos]<=L'9'){
            const unsigned digit=range[pos++]-L'0';
            if(value>(~ULONGLONG{}-digit)/10)return false;
            value=value*10+digit;
        }
        return pos!=start;
    };
    auto delimiter=[&](wchar_t c){return pos<range.size() && range[pos++]==c;};
    ULONGLONG first{},last{},total{};
    return number(first)&&delimiter(L'-')&&number(last)&&delimiter(L'/')&&number(total)&&
        pos==range.size()&&!first&&total&&last==total-1&&total==size;
}
struct HttpsLibrary {
    HMODULE dll{};const ttp_https_api_v6* api{};
    ~HttpsLibrary(){if(dll)FreeLibrary(dll);}
    void Load(){
        if(api)return;
        auto path=ModulePath();path.resize(path.find_last_of(L"\\/")+1);path+=L"ttp_https.dll";
        if(!dll)dll=LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!dll)throw Failure(32022,"");
        auto get=reinterpret_cast<ttp_https_get_api_fn>(GetProcAddress(dll,"ttp_https_get_api"));
        const auto* base=get?get(6):nullptr;
        if(!base || base->abi_version!=6 || base->size!=sizeof(ttp_https_api_v6))throw Failure(32022,"");
        auto next=reinterpret_cast<const ttp_https_api_v6*>(base);
        if(!next->exchange_ex || !next->release_exchange_ex)throw Failure(32022,"");
        api=next;
    }
};
}
struct Transport::Impl {
    struct Cookie { std::wstring origin,path,name,value; ULONGLONG expires{~ULONGLONG{}}; };
    std::mutex mutex;
    Handle session;
    HttpsLibrary https;
    std::vector<Cookie> cookies;
    std::wstring RequestCookies(const Address& url) {
        std::wstring result;
        for(const auto& c:cookies) if(c.origin==url.origin && c.expires>Now() && PathMatches(url.path,c.path)) {
            if(!result.empty()) result+=L"; "; result+=c.name+L"="+c.value;
        }
        if(result.size()>16384) throw std::runtime_error("Too many lyric cookies");
        return result;
    }
    void Store(const Address& url,const std::wstring& header) {
        const auto semi=header.find(L';'),eq=header.find(L'=');
        if(eq==header.npos || eq==0 || (semi!=header.npos && eq>semi)) return;
        Cookie c{url.origin,url.path.substr(0,url.path.rfind(L'/')+1),Trim(std::wstring_view(header).substr(0,eq)),
            Trim(std::wstring_view(header).substr(eq+1,semi==header.npos ? header.npos : semi-eq-1))};
        if(c.path.size()>1) c.path.pop_back();
        if(c.name.empty() || c.name.find_first_of(L"()<>@,;:\\\"/[]?={} \t")!=c.name.npos) return;
        for(auto ch:c.name+c.value) if(ch<0x21 || ch>0x7e) return;
        bool secure=false,domain_ok=true; std::optional<ULONGLONG> age;
        for(size_t start=semi;start!=header.npos;) {
            const auto end=header.find(L';',start+1);
            auto attr=Trim(std::wstring_view(header).substr(start+1,end==header.npos ? header.npos : end-start-1));
            const auto split=attr.find(L'='); const auto key=Lower(Trim(std::wstring_view(attr).substr(0,split)));
            auto value=split==attr.npos ? L"" : Trim(std::wstring_view(attr).substr(split+1));
            if(key==L"secure") secure=true;
            else if(key==L"path" && !value.empty() && value[0]==L'/') c.path=value;
            else if(key==L"domain") {
                value=Lower(value); if(!value.empty() && value[0]==L'.') value.erase(0,1);
                const auto host=Lower(url.host);
                domain_ok=!value.empty() && (host==value || (host.size()>value.size() && host.ends_with(L"."+value)));
            } else if(key==L"max-age" && !value.empty()) {
                wchar_t* endptr{}; const auto seconds=_wcstoi64(value.c_str(),&endptr,10);
                if(endptr!=value.c_str() && !*endptr) age=seconds<=0 ? 0 : Now()+static_cast<ULONGLONG>(std::min<__int64>(seconds,315360000))*10000000;
            } else if(key==L"expires") {
                SYSTEMTIME time{}; FILETIME f{};
                if(InternetTimeToSystemTimeW(value.c_str(),&time,0) && SystemTimeToFileTime(&time,&f))
                    c.expires=(ULONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime;
            }
            start=end;
        }
        if((secure && !url.secure) || !domain_ok) return; // This jar is deliberately confined to HTTP and the response origin.
        if(age) c.expires=*age;
        std::erase_if(cookies,[&](const Cookie& old) { return old.expires<=Now() ||
            (old.origin==c.origin && old.path==c.path && old.name==c.name); });
        if(c.expires>Now()) {
            if(cookies.size()>=64) throw std::runtime_error("Too many lyric cookies");
            cookies.push_back(std::move(c));
            std::stable_sort(cookies.begin(),cookies.end(),[](const Cookie& a,const Cookie& b){return a.path.size()>b.path.size();});
        }
    }
};
void Abort::Cancel() noexcept {canceled=true;Close();}
void Abort::Close() noexcept {HINTERNET h{};{std::lock_guard lock(mutex);h=std::exchange(request,nullptr);}if(h)InternetCloseHandle(h);}
void Abort::Attach(HINTERNET h){std::lock_guard lock(mutex);if(canceled){InternetCloseHandle(h);throw Canceled{};}request=h;}
bool ValidUrl(std::wstring_view value){
    if(value.empty()||value.size()>8192 || value.find_first_of(L"\\#")!=value.npos ||
        std::any_of(value.begin(),value.end(),[](wchar_t c){return c<=L' ';}))return false;
    URL_COMPONENTSW c{sizeof(c)};c.dwHostNameLength=c.dwUserNameLength=c.dwPasswordLength=static_cast<DWORD>(-1);
    // XP leaves requested lengths at DWORD(-1) for absent components. The
    // returned pointer, not that sentinel, determines whether a field exists.
    return InternetCrackUrlW(value.data(),static_cast<DWORD>(value.size()),0,&c) && c.lpszHostName && c.dwHostNameLength &&
        !(c.lpszUserName&&c.dwUserNameLength) && !(c.lpszPassword&&c.dwPasswordLength) &&
        (c.nScheme==INTERNET_SCHEME_HTTP||c.nScheme==INTERNET_SCHEME_HTTPS);
}
bool SecureUrl(const std::wstring& value){return Address(value).secure;}
std::wstring UrlOrigin(const std::wstring& value){return Address(value).origin;}
namespace {
struct Step {Response response;unsigned status{};std::wstring location,range;std::vector<std::wstring> cookies;};
std::optional<Step> Portable(HttpsLibrary& library,const std::wstring& url,const NetworkValue& net,const std::wstring& cookies,
    const std::shared_ptr<Abort>& abort,const TlsTrust* trust){
    library.Load();const auto* api=library.api;
    ttp_https_exchange_ex_request extended{};extended.size=sizeof(extended);
    auto& request=extended.exchange;request.size=sizeof(request);auto& n=request.request;n.size=sizeof(n);
    n.url=url.c_str();n.proxy_type=net.type;n.proxy_server=net.server.c_str();n.proxy_port=static_cast<int>(net.port);
    n.proxy_username=net.user.c_str();n.proxy_password=net.password.c_str();
    n.canceled=[](void* p)->int{auto* a=static_cast<Abort*>(p);return a->canceled||a->Expired()?1:0;};n.cancel_context=abort.get();
    if(trust){n.ca_pem=reinterpret_cast<const unsigned char*>(trust->pem.c_str());n.ca_pem_size=trust->pem.size()+1;}
    const auto cookie=Utf8(cookies);request.cookie=cookie.c_str();
    const auto referer=Utf8(L"https://"+Address(url).host+L"/");
    extended.user_agent="Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1)";
    extended.accept="image/gif, image/x-xbitmap, image/jpg, image/pjpeg, text/html, text/xml, */*";
    extended.referer=referer.c_str();extended.body_policy=TTP_HTTPS_BODY_SUCCESS;
    ttp_https_exchange_ex_response output{};output.size=sizeof(output);
    auto& response=output.exchange;response.size=sizeof(response);response.response.size=sizeof(response.response);
    struct Release{const ttp_https_api_v6* api;ttp_https_exchange_ex_response* p;~Release(){api->release_exchange_ex(p);}}release{api,&output};
    char error[512]{};const int code=api->exchange_ex(&extended,&output,error,sizeof(error));
    abort->Check();if(code==TTP_HTTPS_USE_WINHTTP){if(trust)throw Failure(32024,"");return {};}
    if(code!=TTP_HTTPS_OK)throw Failure(response.response.verify_flags?32023:32025,error);
    Step step;step.status=response.http_status;
    if(response.response.body_size>BodyLimit || response.cookie_count>64)throw Failure(32005,"Invalid HTTPS response size");
    if(response.response.body_size)step.response.body.assign(reinterpret_cast<const char*>(response.response.body),response.response.body_size);
    step.response.title=Wide(response.response.title_header?response.response.title_header:"");
    step.response.url=Wide(response.response.url_header?response.response.url_header:"");step.location=Wide(response.location?response.location:"");
    step.range=Wide(output.content_range?output.content_range:"");
    for(unsigned i=0;i<response.cookie_count;++i)step.cookies.push_back(Wide(response.set_cookies[i]));
    return step;
}
HINTERNET OpenSession(const NetworkValue& net){
    DWORD access=net.type==0?INTERNET_OPEN_TYPE_DIRECT:INTERNET_OPEN_TYPE_PRECONFIG;
    std::wstring proxy;if(net.type>1&&!net.server.empty()){access=INTERNET_OPEN_TYPE_PROXY;proxy=net.server;if(net.port)proxy+=L":"+std::to_wstring(net.port);}
    Handle session{InternetOpenW(L"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1)",access,proxy.empty()?nullptr:proxy.c_str(),proxy.empty()?nullptr:L"<local>",0)};
    Check(session.value!=nullptr);DWORD timeout=10000;
    for(DWORD option:{INTERNET_OPTION_CONNECT_TIMEOUT,INTERNET_OPTION_SEND_TIMEOUT,INTERNET_OPTION_RECEIVE_TIMEOUT})Check(InternetSetOptionW(session,option,&timeout,sizeof(timeout)));
    return std::exchange(session.value,nullptr);
}
Step Native(HINTERNET session,const std::wstring& url,const NetworkValue& net,const std::wstring& cookies,const std::shared_ptr<Abort>& abort){
    Address address(url);abort->Check();
    Handle connection{InternetConnectW(session,address.host.c_str(),address.port,nullptr,nullptr,INTERNET_SERVICE_HTTP,0,0)};Check(connection.value!=nullptr);
    bool auth=net.type>1&&!net.server.empty()&&!net.user.empty();
    const wchar_t* accept[]={L"image/gif",L"image/x-xbitmap",L"image/jpg",L"image/pjpeg",L"text/html",L"text/xml",L"*/*",nullptr};
    const auto referer=(address.secure?L"https://":L"http://")+address.host+L"/";
    HINTERNET request=HttpOpenRequestW(connection,L"GET",address.target.c_str(),nullptr,referer.c_str(),accept,
        INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_AUTO_REDIRECT|
        INTERNET_FLAG_KEEP_CONNECTION|INTERNET_FLAG_NO_UI|(auth?0:INTERNET_FLAG_NO_AUTH)|(address.secure?INTERNET_FLAG_SECURE:0),0);
    Check(request!=nullptr);abort->Attach(request);
    struct Close{std::shared_ptr<Abort> a;~Close(){a->Close();}}close{abort};
    InternetSetOptionW(request,INTERNET_OPTION_SUPPRESS_SERVER_AUTH,nullptr,0);
    auto credentials=[&]{
        Check(InternetSetOptionW(request,INTERNET_OPTION_PROXY_USERNAME,const_cast<wchar_t*>(net.user.c_str()),static_cast<DWORD>((net.user.size()+1)*2)));
        Check(InternetSetOptionW(request,INTERNET_OPTION_PROXY_PASSWORD,const_cast<wchar_t*>(net.password.c_str()),static_cast<DWORD>((net.password.size()+1)*2)));};
    if(auth)credentials();BOOL decoding=TRUE;InternetSetOptionW(request,INTERNET_OPTION_HTTP_DECODING,&decoding,sizeof(decoding));
    std::wstring headers=cookies.empty()?L"":L"Cookie: "+cookies+L"\r\n";Step step;
    auto guard=[&]{abort->Check();};
    for(int attempt=0;attempt<2;++attempt){guard();Check(HttpSendRequestW(request,headers.c_str(),static_cast<DWORD>(headers.size()),nullptr,0));DWORD size=sizeof(step.status);Check(HttpQueryInfoW(request,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&step.status,&size,nullptr));if(step.status!=407||attempt||!auth)break;credentials();}
    const bool redirected=step.status==301||step.status==302||step.status==303||step.status==307||step.status==308;
    if(!redirected && step.status>=200 && step.status<300 && step.status!=204){char buffer[8192];for(;;){guard();DWORD count{};Check(InternetReadFile(request,buffer,sizeof(buffer),&count));if(!count)break;if(step.response.body.size()+count>BodyLimit)throw Failure(32005,"Lyric response exceeds 2 MiB");step.response.body.append(buffer,count);}}
    if(step.status==206)step.range=Header(request,HTTP_QUERY_CONTENT_RANGE);
    for(DWORD index=0,count=0;count<64;++count){auto cookie=Header(request,HTTP_QUERY_SET_COOKIE,nullptr,&index);if(cookie.empty())break;step.cookies.push_back(std::move(cookie));}
    step.location=Header(request,HTTP_QUERY_LOCATION);
    step.response.title=Header(request,HTTP_QUERY_CUSTOM,L"tt-title");step.response.url=Header(request,HTTP_QUERY_CUSTOM,L"tt-url");
    guard();return step;
}
}
Transport::Transport():impl_(std::make_unique<Impl>()){}
Transport::~Transport()=default;
void Transport::Initialize(const NetworkValue& net){std::lock_guard lock(impl_->mutex);if(!impl_->session.value)impl_->session.value=OpenSession(net);}
Response Transport::Get(const std::wstring& initial,const NetworkValue& net,const std::shared_ptr<Abort>& abort,const TlsTrust* trust){
    if(trust && trust->invalid)throw Failure(32026,"");
    Initialize(net);
    std::lock_guard lock(impl_->mutex);std::wstring url=initial;
    for(int i=0;i<=8;++i){abort->Check();if(!ValidUrl(url))throw Failure(32005,"Invalid lyric URL");Address address(url);
        auto cookies=impl_->RequestCookies(address);
        auto portable=address.secure?Portable(impl_->https,url,net,cookies,abort,trust && trust->origin==address.origin?trust:nullptr):std::optional<Step>{};
        auto step=portable?std::move(*portable):Native(impl_->session,url,net,cookies,abort);
        for(const auto& c:step.cookies)impl_->Store(address,c);
        if(step.status==301||step.status==302||step.status==303||step.status==307||step.status==308){
            if(step.location.empty())throw Failure(32005,"Missing lyric redirect");wchar_t combined[16384]{};DWORD size=static_cast<DWORD>(std::size(combined));
            Check(InternetCombineUrlW(url.c_str(),step.location.c_str(),combined,&size,ICU_NO_ENCODE));url.assign(combined,size);
            if(!ValidUrl(url) || (address.secure && !Address(url).secure))throw Failure(32005,"Invalid or downgraded lyric redirect");continue;}
        if(step.status<200 || step.status>=300)throw Failure(step.status,"");
        if(step.status==206 && !step.range.empty() && !CompleteRange(step.range,step.response.body.size()))throw Failure(32005,"Incomplete lyric range");
        step.response.status=step.status;return std::move(step.response);
    }throw Failure(32005,"Too many lyric redirects");
}
}
