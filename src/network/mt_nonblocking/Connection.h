#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <spdlog/logger.h>
#include <protocol/Parser.h>
#include <afina/execute/Command.h>
#include <sys/epoll.h>
#include <deque>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> pl):
            _socket(s), pStorage(ps), _logger(pl), _is_Alive(true) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }

    inline bool isAlive() const { return _is_Alive; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    // Logger instance
    std::shared_ptr<spdlog::logger> _logger;

    std::shared_ptr<Afina::Storage> pStorage;

    std::atomic<bool> _is_Alive;

    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    char client_buffer[4096];
    size_t _read_offset = 0;
    size_t _write_offset = 0;
    std::deque<std::string> _to_write;
    std::mutex _m;
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
