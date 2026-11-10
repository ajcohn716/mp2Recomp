#include "netplay_transport.hpp"
#include <rtc/rtc.hpp>
#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>

namespace netplay::transport {
namespace {
struct Events {
    std::mutex mutex;
    std::deque<std::function<void()>> pending;
    bool overflow{};
    void post(std::function<void()> task) {
        std::lock_guard lock(mutex);
        if(pending.size()>=1024){overflow=true;return;}
        pending.push_back(std::move(task));
    }
    void poll() {
        std::deque<std::function<void()>> tasks;
        {std::lock_guard lock(mutex);if(overflow)throw std::runtime_error("Direct network queue overflow");tasks.swap(pending);}
        for(auto& task:tasks)task();
    }
};
struct DirectSocket final : Socket, std::enable_shared_from_this<DirectSocket> {
    std::shared_ptr<rtc::WebSocket> ws;
    std::shared_ptr<Events> events;
    DirectSocket(std::shared_ptr<rtc::WebSocket> value,std::shared_ptr<Events> queue) : ws(std::move(value)),events(std::move(queue)) {}
    static std::shared_ptr<DirectSocket> create(std::shared_ptr<rtc::WebSocket> ws,std::shared_ptr<Events> events) {
        auto socket=std::make_shared<DirectSocket>(std::move(ws),events);
        std::weak_ptr<DirectSocket> weak=socket;
        auto closed=[weak,events]{events->post([weak]{if(auto s=weak.lock())if(s->on_close)s->on_close();});};
        socket->ws->onOpen([weak,events]{events->post([weak]{if(auto s=weak.lock())if(s->on_open)s->on_open();});});
        socket->ws->onClosed(closed);
        socket->ws->onError([closed](std::string){closed();});
        socket->ws->onMessage([weak,events](rtc::message_variant message){
            events->post([weak,message=std::move(message)]() mutable {
                if(auto s=weak.lock())if(s->on_message)std::visit([&](auto& data){s->on_message(std::move(data));},message);
            });
        });
        return socket;
    }
    ~DirectSocket() override { ws->resetCallbacks(); ws->close(); }
    void open(const std::string& address,uint16_t port) override {
        auto host=address.find(':')==std::string::npos?address:"["+address+"]";
        ws->open("ws://"+host+":"+std::to_string(port)+"/mp2");
    }
    bool is_open() const override {return ws->isOpen();}
    size_t buffered() const override {return ws->bufferedAmount();}
    void send(const std::string& message) override {ws->send(message);}
    void send(const Binary& message) override {ws->send(message.data(),message.size());}
    void close() override {ws->close();}
};
struct Direct final : Transport {
    std::unique_ptr<rtc::WebSocketServer> listener;
    std::shared_ptr<Events> events=std::make_shared<Events>();
    ~Direct() override {
        if(listener){listener->onClient(nullptr);listener->stop();listener.reset();}
        std::lock_guard lock(events->mutex);events->pending.clear();
    }
    uint16_t listen(const std::string& address,uint16_t port,std::function<void(std::shared_ptr<Socket>)> accept) override {
        rtc::WebSocketServerConfiguration cfg;
        cfg.bindAddress=address.find(':')==std::string::npos?"0.0.0.0":"::";
        cfg.port=port;cfg.maxMessageSize=32768;cfg.connectionTimeout=std::chrono::seconds(5);
        listener=std::make_unique<rtc::WebSocketServer>(cfg);
        listener->onClient([events=events,accept=std::move(accept)](std::shared_ptr<rtc::WebSocket> socket){
            events->post([events,accept,socket=std::move(socket)]{accept(DirectSocket::create(socket,events));});
        });
        return listener->port();
    }
    std::shared_ptr<Socket> socket() override {
        rtc::WebSocketConfiguration cfg;cfg.connectionTimeout=std::chrono::seconds(10);cfg.maxMessageSize=32768;
        return DirectSocket::create(std::make_shared<rtc::WebSocket>(cfg),events);
    }
    void poll() override {events->poll();}
};
}
std::unique_ptr<Transport> direct(){return std::make_unique<Direct>();}
}
