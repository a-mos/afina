#ifndef AFINA_NETWORK_ST_COROUTINE_CONNECTION_H
#define AFINA_NETWORK_ST_COROUTINE_CONNECTION_H

#include <afina/execute/Command.h>
#include <cstring>

#include <afina/Storage.h>
#include <afina/coroutine/Engine.h>
#include <protocol/Parser.h>
#include <spdlog/logger.h>
#include <sys/epoll.h>
#include <deque>

namespace Afina {
namespace Network {
namespace STcoroutine {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> pl):
            _socket(s), pStorage(ps), _logger(pl), _is_Alive(true) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }
    inline bool isAlive() const {
        return _is_Alive;
    }
    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();
    void Work(Afina::Coroutine::Engine& engine);

private:
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    bool _is_Alive;

    std::shared_ptr<spdlog::logger> _logger;
    std::shared_ptr<Afina::Storage> pStorage;

    char client_buffer[4096] = {0};
    Protocol::Parser parser;
    std::unique_ptr<Execute::Command> command_to_execute;
    std::size_t arg_remains = 0;
    std::string argument_for_command;

    size_t _read_offset = 0;
    size_t _write_offset = 0;
    size_t MAXSIZE = 64;
    std::deque<std::string> _to_write;

    Afina::Coroutine::Engine::context* _ctx;
    Afina::Coroutine::Engine::context* server_courutine;
};
} // namespace STcoroutine
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_COROUTINE_CONNECTION_H
