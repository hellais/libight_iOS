/*-
 * This file is part of Libight <https://libight.github.io/>.
 *
 * Libight is free software. See AUTHORS and LICENSE for more
 * information on the copying conditions.
 */

#ifndef LIBIGHT_NET_SOCKS5_HPP
# define LIBIGHT_NET_SOCKS5_HPP

#include <ight/common/log.h>

#include <ight/net/buffer.hpp>
#include <ight/net/connection.hpp>
#include <ight/net/transport.hpp>

namespace ight {
namespace net {
namespace socks5 {

using namespace ight::common::pointer;
using namespace ight::common;

using namespace ight::net::connection;
using namespace ight::net::transport;

class Socks5 : public Transport {

protected:
    SharedPointer<Connection> conn;
    std::function<void()> on_connect_fn;
    std::function<void(SharedPointer<IghtBuffer>)> on_data_fn;
    std::function<void()> on_flush_fn;
    Settings settings;
    SharedPointer<IghtBuffer> buffer{
        std::make_shared<IghtBuffer>()
    };
    bool isclosed = false;
    std::string proxy_address;
    std::string proxy_port;

public:

    virtual void emit_connect() override {
        conn->emit_connect();
    }

    virtual void emit_data(SharedPointer<IghtBuffer> data) override {
        conn->emit_data(data);
    }

    virtual void emit_flush() override {
        conn->emit_flush();
    }

    virtual void emit_error(IghtError err) override {
        conn->emit_error(err);
    }

    Socks5(Settings);

    virtual void on_connect(std::function<void()> fn) override {
        on_connect_fn = fn;
    }

    virtual void on_ssl(std::function<void()> fn) override {
        conn->on_ssl(fn);
    }

    virtual void on_data(std::function<void(SharedPointer<
            IghtBuffer>)> fn) override {
        on_data_fn = fn;
    }

    virtual void on_flush(std::function<void()> fn) override {
        on_flush_fn = fn;
    }

    virtual void on_error(std::function<void(IghtError)> fn) override {
        conn->on_error(fn);
    }

    virtual void set_timeout(double timeout) override {
        conn->set_timeout(timeout);
    }

    virtual void clear_timeout() override {
        conn->clear_timeout();
    }

    virtual void send(const void* data, size_t count) override {
        conn->send(data, count);
    }

    virtual void send(std::string data) override {
        conn->send(data);
    }

    virtual void send(IghtBuffer& data) override {
        conn->send(data);
    }

    virtual void send(SharedPointer<IghtBuffer> data) override {
        conn->send(data);
    }

    virtual void close() override {
        isclosed = true;
        conn->close();
    }

    virtual std::string socks5_address() override {
        return proxy_address;
    }

    virtual std::string socks5_port() override {
        return proxy_port;
    }

};

}}}
#endif  // LIBIGHT_NET_SOCKS5_HPP
