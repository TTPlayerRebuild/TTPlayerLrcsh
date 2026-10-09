#include "core.h"
#include <algorithm>
#include <bit>
#include <cwctype>
#include <cstdlib>

namespace ttp::lrc {
namespace {
void Replace(std::wstring& text,std::wstring_view from,std::wstring_view to){for(size_t i=0;(i=text.find(from,i))!=text.npos;i+=to.size())text.replace(i,from.size(),to);}
std::wstring Hex(std::wstring_view value){
    std::wstring text(value);Replace(text,L"'",L"’ ");
    constexpr std::wstring_view pairs=L"()[]{}<>（）［］｛｝《》【】“”";
    auto separator=[](wchar_t c){return !iswalnum(c) && !isleadbyte(static_cast<unsigned>(c));};
    size_t start=0;while(start<std::min<size_t>(text.size(),4) && text[start]>=L'0'&&text[start]<=L'9')++start;
    if(!start || start>=4 || start>=text.size()-1 || !separator(text[start]))start=0;
    std::wstring clean;
    for(size_t i=start;i<text.size();++i){if(!separator(text[i])){clean+=text[i];continue;}
        auto at=pairs.find(text[i]);if(at!=pairs.npos && at%2==0){auto end=text.find(pairs[at+1],i+1);if(end!=text.npos)i=end;}}
    if(!clean.empty()){
        auto count=LCMapStringW(GetThreadLocale(),LCMAP_SIMPLIFIED_CHINESE|LCMAP_LOWERCASE,clean.data(),static_cast<int>(clean.size()),nullptr,0);
        if(count>0){std::wstring mapped(count,0);if(LCMapStringW(GetThreadLocale(),LCMAP_SIMPLIFIED_CHINESE|LCMAP_LOWERCASE,clean.data(),static_cast<int>(clean.size()),mapped.data(),count))clean=std::move(mapped);}}
    std::wstring out;constexpr wchar_t digits[]=L"0123456789ABCDEF";
    for(auto c:clean)for(unsigned shift:{0U,8U}){unsigned b=(c>>shift)&255;out+=digits[b>>4];out+=digits[b&15];}return out;
}
std::wstring Base(std::wstring_view base){std::wstring out(base);auto i=out.find(L"://");if(i!=out.npos){auto path=out.find_first_of(L"/?",i+3);if(path==out.npos)out+=L'/';else if(out[path]==L'?')out.insert(path,1,L'/');}return out;}
int LegacyInt(const std::string& s){
    // The original CRT wraps its decimal accumulator at 32 bits. strtol
    // saturates instead, changing both the selected ID and download Code.
    size_t i=0;while(i<s.size() && (s[i]==' ' || (s[i]>='\t' && s[i]<='\r')))++i;
    bool negative=false;if(i<s.size() && (s[i]=='+'||s[i]=='-'))negative=s[i++]=='-';
    unsigned value=0;for(;i<s.size() && s[i]>='0' && s[i]<='9';++i)value=value*10+unsigned(s[i]-'0');
    return std::bit_cast<int>(negative?0U-value:value);
}
void ServerError(const XmlNode& root,unsigned fallback=32010){auto message=root.Attr("errmsg");auto code=root.Attr("errcode");throw Failure(code.empty()?fallback:static_cast<unsigned>(LegacyInt(code)),message.c_str(),true);}
std::wstring HeaderHex(std::wstring_view s){std::wstring out;unsigned byte=0,low=0;size_t count=0;
    for(size_t i=0;i+1<s.size();i+=2){auto digit=[](wchar_t c){return c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:-1;};int a=digit(s[i]),b=digit(s[i+1]);if(a<0||b<0)break;byte=(a<<4)|b;if(count++%2==0)low=byte;else out+=wchar_t(low|(byte<<8));}Replace(out,L"\\n",L"\r\n");return out;}
}
std::wstring SearchUrl(std::wstring_view base,std::wstring_view artist,std::wstring_view title){return Base(base)+L"?sh?Artist="+Hex(artist)+L"&Title="+Hex(title)+L"&Flags=0&";}
int DownloadCode(unsigned id,std::string_view bytes){
    unsigned high=id>>24,middle=(id>>16)&255;if(!middle)middle=(~(id>>8))&255;if(!high)high=(~id)&255;
    const unsigned mixed=((id&255)<<24)|(middle<<16)|(((id>>8)&255)<<8)|high;
    unsigned reverse=0,forward=0;
    for(size_t i=bytes.size();i-->0;)reverse=static_cast<signed char>(bytes[i])+reverse+(reverse<<(i%2+4));
    for(size_t i=0;i<bytes.size();++i)forward=static_cast<signed char>(bytes[i])+forward+(forward<<(i%2+3));
    return std::bit_cast<int>(((reverse^mixed)+(forward|id))*(forward|mixed)*(reverse^id));
}
std::vector<Candidate> FindLyrics(Transport& transport,const Service& service,const NetworkValue& net,
    const std::shared_ptr<Abort>& abort,std::wstring_view artist,std::wstring_view title){
    auto response=transport.Get(SearchUrl(service.url,artist,title),net,abort,service.trust.get());
    if(response.body.empty())throw Failure(response.status?response.status:32004,"");
    auto root=ParseXml(response.body,true);std::vector<Candidate> rows;
    for(auto& node:root.children)if(node.name=="lrc"){
        if(rows.size()>=10000)throw Failure(32005,"Too many lyric results");
        rows.push_back({LegacyInt(node.Attr("id")),node.Attr("artist"),node.Attr("title")});}
    if(rows.empty())ServerError(root);return rows;
}
Downloaded DownloadLyric(Transport& transport,const Service& service,const NetworkValue& net,
    const std::shared_ptr<Abort>& abort,const Candidate& row){
    auto url=Base(service.url)+L"?dl?Id="+std::to_wstring(row.id)+L"&Code="+std::to_wstring(DownloadCode(static_cast<unsigned>(row.id),row.artist+row.title))+L"&";
    auto response=transport.Get(url,net,abort,service.trust.get());if(response.body.empty())throw Failure(response.status?response.status:32004,"");
    if(response.body.starts_with("<result "))ServerError(ParseXml(response.body,true),0);
    auto text=Wide(response.body);Replace(text,L"\n\r",L"\r\n");Replace(text,L"\r\n\r",L"\r\n");
    return {std::move(text),HeaderHex(response.title),HeaderHex(response.url)};
}
}
