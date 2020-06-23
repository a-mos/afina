#include "Connection.h"

#include <iostream>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start() {
    std::unique_lock<std::mutex> lock(_m);
    _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
}

// See Connection.h
void Connection::OnError() {
    std::unique_lock<std::mutex> lock(_m);
    _is_Alive = false;
}

// See Connection.h
void Connection::OnClose() {
    std::unique_lock<std::mutex> lock(_m);
    _is_Alive = false;
}

// See Connection.h
void Connection::DoRead() {
    std::unique_lock<std::mutex> lock(_m);
    // Process new connection:
    // - read commands until socket alive
    // - execute each command
    // - send response
    try {
        int readed_bytes = -1;
        while (isAlive() && (readed_bytes = read(_socket, client_buffer + _read_offset,
                                                          sizeof(client_buffer) - _read_offset)) > 0) {
            _logger->debug("Got {} bytes from socket", readed_bytes);
            readed_bytes += _read_offset;
            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (readed_bytes > 0 && isAlive()) {
                _logger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                    _read_offset = readed_bytes;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");
                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    // Send response
                    result += "\r\n";

                    _to_write.push_back(result);
                    _event.events |= EPOLLOUT;

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }

        if (readed_bytes == 0) {
            _is_Alive = false;
            _logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite() {
    std::unique_lock<std::mutex> lock(_m);
    struct iovec out[MAXSIZE];
    size_t SIZE_TO_WRITE = std::min(_to_write.size(), MAXSIZE);
    for (size_t i = 1; i < SIZE_TO_WRITE; i++) {
        out[i].iov_base = &_to_write[i][0];
        out[i].iov_len = _to_write[i].size();
    }
    out[0].iov_base = &_to_write[0][0] + _write_offset;
    out[0].iov_len = _to_write[0].size() - _write_offset;

    ssize_t written = writev(_socket, out, SIZE_TO_WRITE);

    if (written < 0) {
        throw std::runtime_error("Failed to send response");
    }

    written += _write_offset;
    for (size_t i = 0; i < SIZE_TO_WRITE; i++) {
        if (written >= out[i].iov_len) {
            written -= out[i].iov_len;
            _to_write.pop_front();
        } else {
            break;
        }
    }

    _write_offset = written;

    if (_to_write.empty()) {
        _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
    } else {
        _event.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLERR;
    }
}

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
