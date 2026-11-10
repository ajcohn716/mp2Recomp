#include "netplay_crypto.hpp"
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <memory>
#include <stdexcept>

namespace netplay::crypto {
namespace {
void require(bool ok){if(!ok)throw std::runtime_error("Encrypted peer authentication failed");}
void address_check(const std::string& address) {
    if(address.empty()||address.size()>253||!std::all_of(address.begin(),address.end(),[](unsigned char c){return std::isalnum(c)||c=='.'||c=='-'||c==':';}))throw std::runtime_error("Enter a public IP address or DNS name without a URL or port");
}
void steam_id_check(const std::string& address) {
    uint64_t id{};
    auto [end,err]=std::from_chars(address.data(),address.data()+address.size(),id);
    // Public-universe individual Steam accounts only, with a nonzero account ID.
    if(err!=std::errc{}||end!=address.data()+address.size()||std::to_string(id)!=address||
       (id>>56)!=1||((id>>52)&15)!=1||((id>>32)&0xfffff)!=1||uint32_t(id)==0)
        throw std::runtime_error("Invalid Steam host identity");
}
Key derive(const Key& secret,const Key& client,const Key& host,const char* label) {
    std::array<unsigned char,64> salt{};std::copy(client.begin(),client.end(),salt.begin());std::copy(host.begin(),host.end(),salt.begin()+32);
    std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF,nullptr),EVP_PKEY_CTX_free);
    require(bool(ctx));require(EVP_PKEY_derive_init(ctx.get())>0);require(EVP_PKEY_CTX_set_hkdf_md(ctx.get(),EVP_sha256())>0);
    require(EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(),salt.data(),salt.size())>0);
    require(EVP_PKEY_CTX_set1_hkdf_key(ctx.get(),secret.data(),secret.size())>0);
    std::string info=std::string("mp2-netplay-v2/")+label;
    require(EVP_PKEY_CTX_add1_hkdf_info(ctx.get(),reinterpret_cast<const unsigned char*>(info.data()),info.size())>0);
    Key key{};size_t size=key.size();require(EVP_PKEY_derive(ctx.get(),key.data(),&size)>0&&size==key.size());return key;
}
void number(unsigned char* out,uint64_t sequence){for(int i=7;i>=0;--i){out[i]=sequence&255;sequence>>=8;}}
}
Key random_key(){Key key{};require(RAND_bytes(key.data(),key.size())==1);return key;}
std::string encode(std::span<const unsigned char> bytes) {
    std::string out(4*((bytes.size()+2)/3),'\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),bytes.data(),int(bytes.size()));
    for(char& c:out){if(c=='+')c='-';else if(c=='/')c='_';}
    while(!out.empty()&&out.back()=='=')out.pop_back();return out;
}
std::vector<unsigned char> decode(const std::string& value) {
    if(value.empty()||value.size()>1024||value.size()%4==1||!std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';}))throw std::runtime_error("Invalid connection code");
    std::string text=value;for(char& c:text){if(c=='-')c='+';else if(c=='_')c='/';}
    size_t original=text.size();while(text.size()%4)text+='=';
    std::vector<unsigned char> out(text.size()/4*3+1);
    int size=EVP_DecodeBlock(out.data(),reinterpret_cast<const unsigned char*>(text.data()),int(text.size()));
    if(size<0)throw std::runtime_error("Invalid connection code");
    out.resize(size-(text.size()-original));
    if(encode(out)!=value)throw std::runtime_error("Noncanonical connection code");return out;
}
std::string invitation(const Invite& invite) {
    if(invite.steam)steam_id_check(invite.address);else address_check(invite.address);if(!invite.port)throw std::runtime_error("Invalid host port");
    std::vector<unsigned char> data{'M','2',uint8_t(invite.steam?3:2),uint8_t(invite.address.size()),uint8_t(invite.port>>8),uint8_t(invite.port)};
    data.insert(data.end(),invite.address.begin(),invite.address.end());data.insert(data.end(),invite.key.begin(),invite.key.end());return std::string(invite.steam?"MP2S-":"MP2-")+encode(data);
}
Invite parse_invitation(const std::string& value) {
    bool steam=value.rfind("MP2S-",0)==0;
    if(!steam&&value.rfind("MP2-",0)!=0)throw std::runtime_error("Paste the full MP2 connection code");
    auto bytes=decode(value.substr(steam?5:4));
    if(bytes.size()<39||bytes[0]!='M'||bytes[1]!='2'||bytes[2]!=(steam?3:2)||bytes.size()!=size_t(bytes[3])+38)throw std::runtime_error("Invalid or incompatible connection code");
    Invite invite;invite.steam=steam;invite.address.assign(bytes.begin()+6,bytes.end()-32);invite.port=uint16_t(bytes[4])*256+bytes[5];
    std::copy(bytes.end()-32,bytes.end(),invite.key.begin());if(steam)steam_id_check(invite.address);else address_check(invite.address);
    if(!invite.port)throw std::runtime_error("Invalid host port");return invite;
}
Channel::~Channel(){OPENSSL_cleanse(sending.data(),sending.size());OPENSSL_cleanse(receiving.data(),receiving.size());}
void Channel::initialize(const Key& secret,const Key& client,const Key& host,bool is_host) {
    if(initialized)throw std::runtime_error("Duplicate encryption handshake");
    sending=derive(secret,client,host,is_host?"host-to-client":"client-to-host");
    receiving=derive(secret,client,host,is_host?"client-to-host":"host-to-client");initialized=true;
}
std::vector<unsigned char> Channel::seal(const std::string& message) {
    require(initialized&&send_sequence!=UINT64_MAX&&message.size()<=16384);
    std::array<unsigned char,12> nonce{};number(nonce.data()+4,send_sequence);
    std::vector<unsigned char> packet(8+message.size()+16);number(packet.data(),send_sequence);
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);require(bool(ctx));
    require(EVP_EncryptInit_ex(ctx.get(),EVP_aes_256_gcm(),nullptr,sending.data(),nonce.data())==1);
    int length=0,tail=0;require(EVP_EncryptUpdate(ctx.get(),nullptr,&length,packet.data(),8)==1);
    require(EVP_EncryptUpdate(ctx.get(),packet.data()+8,&length,reinterpret_cast<const unsigned char*>(message.data()),int(message.size()))==1);
    require(EVP_EncryptFinal_ex(ctx.get(),packet.data()+8+length,&tail)==1);
    require(EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_GET_TAG,16,packet.data()+8+message.size())==1);
    ++send_sequence;return packet;
}
std::string Channel::open(std::span<const unsigned char> packet) {
    require(initialized&&receive_sequence!=UINT64_MAX&&packet.size()>=24&&packet.size()<=16408);
    std::array<unsigned char,8> expected{};number(expected.data(),receive_sequence);
    require(CRYPTO_memcmp(expected.data(),packet.data(),8)==0);
    std::array<unsigned char,12> nonce{};std::copy(expected.begin(),expected.end(),nonce.begin()+4);
    std::string message(packet.size()-24,'\0');
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);require(bool(ctx));
    require(EVP_DecryptInit_ex(ctx.get(),EVP_aes_256_gcm(),nullptr,receiving.data(),nonce.data())==1);
    int length=0,tail=0;require(EVP_DecryptUpdate(ctx.get(),nullptr,&length,packet.data(),8)==1);
    require(EVP_DecryptUpdate(ctx.get(),reinterpret_cast<unsigned char*>(message.data()),&length,packet.data()+8,int(message.size()))==1);
    require(EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_SET_TAG,16,const_cast<unsigned char*>(packet.data()+packet.size()-16))==1);
    require(EVP_DecryptFinal_ex(ctx.get(),reinterpret_cast<unsigned char*>(message.data())+length,&tail)==1);
    ++receive_sequence;return message;
}
}
