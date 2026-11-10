#include "public_ip.hpp"
#include <curl/curl.h>
#include <array>
#include <filesystem>
#include <memory>

namespace netplay {
std::string parse_public_ipv4(std::string_view response) {
    if(response.size()>64)return {};
    auto first=response.find_first_not_of(" \t\r\n");
    if(first==response.npos)return {};
    response=response.substr(first,response.find_last_not_of(" \t\r\n")-first+1);
    std::array<unsigned,4> octets{};
    size_t pos=0;
    for(int i=0;i<4;++i) {
        size_t start=pos;
        while(pos<response.size()&&response[pos]>='0'&&response[pos]<='9') {
            octets[i]=octets[i]*10+unsigned(response[pos++]-'0');
            if(pos-start>3||octets[i]>255)return {};
        }
        if(pos==start||(pos-start>1&&response[start]=='0'))return {};
        if(i<3&&(pos==response.size()||response[pos++]!='.'))return {};
    }
    if(pos!=response.size())return {};
    auto a=octets[0],b=octets[1],c=octets[2];
    if(a==0||a==10||a==127||a>=224||(a==100&&b>=64&&b<=127)||
       (a==169&&b==254)||(a==172&&b>=16&&b<=31)||(a==192&&b==168)||
       (a==192&&b==0&&c==0)||(a==192&&b==0&&c==2)||
       (a==198&&(b==18||b==19))||(a==198&&b==51&&c==100)||
       (a==203&&b==0&&c==113))return {};
    return std::string(response);
}
namespace {
size_t receive(char* bytes,size_t size,size_t count,void* data) noexcept {
    auto& body=*static_cast<std::string*>(data);
    if(size && count>(64-body.size())/size)return 0;
    try {body.append(bytes,size*count);}catch(...){return 0;}
    return size*count;
}
}
std::string lookup_public_ipv4() {
    static const auto initialized=curl_global_init(CURL_GLOBAL_DEFAULT);
    if(initialized!=CURLE_OK)return {};
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> request(curl_easy_init(),curl_easy_cleanup);
    if(!request)return {};
    std::string body;
    auto* curl=request.get();
    // A configured HTTP proxy would report the proxy's address instead of the host's.
    if(curl_easy_setopt(curl,CURLOPT_URL,"https://ipv4.icanhazip.com/")!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_IPRESOLVE,long(CURL_IPRESOLVE_V4))!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_PROXY,"")!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L)!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,3000L)!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,5000L)!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L)!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L)!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L)!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive)!=CURLE_OK||
       curl_easy_setopt(curl,CURLOPT_WRITEDATA,&body)!=CURLE_OK)return {};
#ifdef _WIN32
    // Use Windows' certificate store with OpenSSL-backed libcurl as well.
    if(curl_easy_setopt(curl,CURLOPT_SSL_OPTIONS,long(CURLSSLOPT_NATIVE_CA))!=CURLE_OK)return {};
#else
    // SDK libcurl may have a build-prefix CA path. Use the player's system trust
    // bundle so installed copies do not depend on the SDK's original location.
    for(const char* path:{"/etc/ssl/certs/ca-certificates.crt","/etc/pki/tls/certs/ca-bundle.crt","/etc/ssl/cert.pem"}) {
        std::error_code error;
        if(std::filesystem::is_regular_file(path,error)) {
            if(curl_easy_setopt(curl,CURLOPT_CAINFO,path)!=CURLE_OK)return {};
            break;
        }
    }
#endif
    if(curl_easy_perform(curl)!=CURLE_OK)return {};
    long status{};
    if(curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status)!=CURLE_OK||status!=200)return {};
    return parse_public_ipv4(body);
}
}
