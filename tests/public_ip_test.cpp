#include "public_ip.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc,char**) {
    using netplay::parse_public_ipv4;
    if(parse_public_ipv4(" 93.184.216.34\r\n")!="93.184.216.34")throw std::runtime_error("IPv4 whitespace handling");
    for(const auto* value:{"", "\n", "<html>error</html>", "93.184.216.34 extra", "93.184.216.34\n1.1.1.1",
        "256.1.1.1", "1.2.3", "1.2.3.4.5", "1.2.3.-1", "01.2.3.4", "::1", "0.0.0.0",
        "10.1.2.3", "127.0.0.1", "172.16.1.1", "192.168.1.1", "169.254.1.1", "100.64.1.1",
        "224.1.1.1", "255.255.255.255", "192.0.2.1", "198.51.100.1", "203.0.113.1"})
        if(!parse_public_ipv4(value).empty())throw std::runtime_error("Invalid/public-IP response accepted");
    if(!parse_public_ipv4(std::string(1024,'1')).empty())throw std::runtime_error("Oversized response accepted");
    std::cout<<"PASS: public IPv4 response validation\n";
    // Explicit opt-in smoke test; ordinary checks never need the public service.
    if(argc>1) {
        if(netplay::lookup_public_ipv4().empty())throw std::runtime_error("Live HTTPS lookup failed");
        std::cout<<"PASS: live HTTPS IPv4 lookup\n";
    }
}
