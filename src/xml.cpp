#include "core.h"
#include <charconv>

namespace ttp::lrc {
std::wstring Wide(std::string_view bytes) {
    std::wstring out;
    for(size_t i=0;i<bytes.size();) {
        unsigned c=static_cast<unsigned char>(bytes[i++]), n=0, minimum=0;
        if(c>=0xf0 && c<=0xf4){c&=7;n=3;minimum=0x10000;}
        else if(c>=0xe0 && c<=0xef){c&=15;n=2;minimum=0x800;}
        else if(c>=0xc2 && c<=0xdf){c&=31;n=1;minimum=0x80;}
        else if(c>=0x80) throw Failure(32005,"Invalid UTF-8");
        if(i+n>bytes.size())throw Failure(32005,"Truncated UTF-8");
        for(unsigned j=0;j<n;++j){auto b=static_cast<unsigned char>(bytes[i++]);if((b&0xc0)!=0x80)throw Failure(32005,"Invalid UTF-8");c=(c<<6)|(b&63);}
        if(c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff) || !c)throw Failure(32005,"Invalid Unicode");
        if(c>=0x10000){c-=0x10000;out+=wchar_t(0xd800+(c>>10));out+=wchar_t(0xdc00+(c&1023));}
        else out+=wchar_t(c);
    }
    return out;
}
std::string Utf8(std::wstring_view text) {
    std::string out;
    for(size_t i=0;i<text.size();++i){unsigned c=text[i];
        if(c>=0xd800 && c<=0xdbff){if(i+1>=text.size() || text[i+1]<0xdc00 || text[i+1]>0xdfff)throw Failure(32005,"Invalid UTF-16");c=0x10000+((c-0xd800)<<10)+(text[++i]-0xdc00);}
        else if(c>=0xdc00 && c<=0xdfff)throw Failure(32005,"Invalid UTF-16");
        if(c<128)out+=char(c);
        else if(c<2048){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
        else if(c<65536){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
        else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
    }return out;
}
namespace {
struct Parser {
    std::string_view s;size_t p{},nodes{};bool tolerant;
    [[noreturn]] void Bad(){throw Failure(32005,"Invalid lyric XML");}
    bool At(std::string_view v){return s.substr(p).starts_with(v);}
    void Space(){while(p<s.size() && (s[p]==' '||s[p]=='\t'||s[p]=='\r'||s[p]=='\n'))++p;}
    bool Special(){
        std::string_view close;
        if(At("<!--"))close="-->";else if(At("<?"))close="?>";else return false;
        auto end=s.find(close,p+2);if(end==s.npos)Bad();p=end+close.size();return true;
    }
    std::string Name(){size_t start=p;while(p<s.size() && ((s[p]>='a'&&s[p]<='z')||(s[p]>='A'&&s[p]<='Z')||(s[p]>='0'&&s[p]<='9')||s[p]=='_'||s[p]=='-'||s[p]==':'||s[p]=='.'))++p;if(p==start)Bad();return std::string(s.substr(start,p-start));}
    std::string Value(){
        if(p>=s.size() || (s[p]!='\''&&s[p]!='"'))Bad();char quote=s[p++];std::string out;
        while(p<s.size() && s[p]!=quote){
            if(s[p]=='<')Bad();if(s[p]!='&'){out+=s[p++];continue;}
            const auto end=s.find(';',p+1);bool used=false;
            if(end!=s.npos && end-p<=16){auto token=s.substr(p+1,end-p-1);
                for(auto [key,c]:{std::pair{"amp",'&'}, {"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''}})
                    if(token==key){out+=c;used=true;break;}
                if(!used && token.starts_with('#')){
                    token.remove_prefix(1);int base=10;if(token.starts_with('x')){base=16;token.remove_prefix(1);}
                    unsigned cp{};auto v=std::from_chars(token.data(),token.data()+token.size(),cp,base);
                    if(v.ec!=std::errc{} || v.ptr!=token.data()+token.size() || !cp || cp>0x10ffff || (cp>=0xd800&&cp<=0xdfff))Bad();
                    std::wstring text;if(cp>=65536){cp-=65536;text+=wchar_t(0xd800+(cp>>10));text+=wchar_t(0xdc00+(cp&1023));}else text+=wchar_t(cp);
                    out+=Utf8(text);used=true;
                }
                if(used){p=end+1;continue;}
            }
            if(!tolerant)Bad();out+=s[p++];
        }
        if(p==s.size())Bad();++p;return out;
    }
    XmlNode Node(unsigned depth){
        if(depth>16 || ++nodes>20000 || !At("<"))Bad();++p;XmlNode n;n.name=Name();
        for(;;){Space();if(At("/>")){p+=2;return n;}if(At(">")){++p;break;}
            auto key=Name();Space();if(!At("="))Bad();++p;Space();auto value=Value();
            if(n.attrs.size()>=128 || !n.attrs.emplace(std::move(key),std::move(value)).second)Bad();}
        for(;;){Space();if(Special())continue;
            if(At("</")){p+=2;auto name=Name();Space();if(name!=n.name||!At(">"))Bad();++p;return n;}
            if(At("<![CDATA[")){auto end=s.find("]]>",p+9);if(end==s.npos)Bad();p=end+3;continue;}
            if(At("<")){n.children.push_back(Node(depth+1));continue;}
            if(p==s.size())Bad();while(p<s.size() && s[p]!='<')++p;
        }
    }
};
}
XmlNode ParseXml(std::string_view bytes,bool tolerant){
    if(bytes.size()>BodyLimit || bytes.find('\0')!=bytes.npos)throw Failure(32005,"Invalid XML size");
    if(bytes.starts_with("\xef\xbb\xbf"))bytes.remove_prefix(3);
    Wide(bytes);Parser p{bytes,0,0,tolerant};p.Space();while(p.Special())p.Space();auto n=p.Node(0);p.Space();while(p.Special())p.Space();if(p.p!=bytes.size())p.Bad();return n;
}
std::string EscapeXml(std::string_view value){std::string out;for(char c:value){switch(c){case '&':out+="&amp;";break;case '"':out+="&quot;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;default:out+=c;}}return out;}
}
