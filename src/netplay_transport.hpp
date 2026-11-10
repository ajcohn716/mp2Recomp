// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Cohn
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace netplay::transport {
using Binary = std::vector<std::byte>;
using Message = std::variant<std::string, Binary>;
// Callbacks are detached before a socket is closed. Session queues callbacks
// onto its worker; each transport is polled and destroyed on that same worker.
struct Socket {
    std::function<void()> on_open, on_close;
    std::function<void(Message)> on_message;
    virtual ~Socket() = default;
    void reset_callbacks() { on_open={}; on_close={}; on_message={}; }
    virtual void open(const std::string& address, uint16_t port) = 0;
    virtual bool is_open() const = 0;
    virtual size_t buffered() const = 0;
    virtual void send(const std::string&) = 0;
    virtual void send(const Binary&) = 0;
    virtual void close() = 0;
};
struct Transport {
    virtual ~Transport() = default;
    virtual uint16_t listen(const std::string& address, uint16_t port,
                            std::function<void(std::shared_ptr<Socket>)> accept) = 0;
    virtual std::string host_address(const std::string& configured) const { return configured; }
    virtual std::shared_ptr<Socket> socket() = 0;
    virtual void poll() = 0;
};
std::unique_ptr<Transport> direct();
std::unique_ptr<Transport> steam();
inline constexpr uint32_t steam_app_id = 480;
}
