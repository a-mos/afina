#include <afina/coroutine/Engine.h>

#include <cassert>
#include <csetjmp>
#include <cstring>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char storeBeginAddress;
    assert(ctx.Hight != nullptr && ctx.Low != nullptr);
    if (&storeBeginAddress > ctx.Low) {
        ctx.Hight = &storeBeginAddress;
    } else {
        ctx.Low = &storeBeginAddress;
    }
    size_t need_size = ctx.Hight - ctx.Low;
    if (std::get<1>(ctx.Stack) < need_size || std::get<1>(ctx.Stack) > need_size * 2) {
        delete std::get<0>(ctx.Stack);
        std::get<0>(ctx.Stack) = new char[need_size];
        std::get<1>(ctx.Stack) = need_size;
    }
    std::memcpy(std::get<0>(ctx.Stack), ctx.Low, need_size);
}

void Engine::Restore(context &ctx) {
    char restoreBeginAddress;
    while (&restoreBeginAddress <= ctx.Hight && &restoreBeginAddress >= ctx.Low) {
        Restore(ctx);
    }
    std::memcpy(ctx.Low, std::get<0>(ctx.Stack), ctx.Hight - ctx.Low);
    cur_routine = &ctx;
    longjmp(ctx.Environment, 1);
}

// Находит какую-то задачу, не являющуюся текущей и запускает ее.
// Если такой задачи не существует, то функция ничего не делает.
void Engine::yield() {
    if (alive == nullptr) {
        return;
    }
    auto routine_todo = alive;
    if (routine_todo == cur_routine) {
        if (alive->next != nullptr) {
            routine_todo = alive->next;
        } else {
            return;
        }
    }
    enter(routine_todo);
}

// Останавливает текущую задачу и запускает ту, которая заданна аргументом
void Engine::sched(void *coro) {
    auto new_coro = static_cast<context *>(coro);
    if (new_coro == nullptr) {
        yield();
    }
    if (new_coro == cur_routine || new_coro->isBlocked) {
        return;
    }
    enter(new_coro);
}

void Engine::enter(void *coro) {
    assert(cur_routine != nullptr);
    if (cur_routine != idle_ctx) {
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
        Store(*cur_routine);
    }
    Restore(*static_cast<context *>(coro));
}

void Engine::block(void *coro) {
    context *blockedCoro;
    // If argument is nullptr then block current coroutine
    if (coro == nullptr) {
        blockedCoro = cur_routine;
    } else {
        blockedCoro = static_cast<context *>(coro);
    }
    if (blockedCoro == nullptr || blockedCoro->isBlocked) {
        return;
    }
    blockedCoro->isBlocked = true;
    // alive list
    if (alive == blockedCoro) {
        alive = alive->next;
    }
    if (blockedCoro->prev) {
        blockedCoro->prev->next = blockedCoro->next;
    }
    if (blockedCoro->next) {
        blockedCoro->next->prev = blockedCoro->prev;
    }
    // blocked list
    blockedCoro->prev = nullptr;
    blockedCoro->next = blocked;
    blocked = blockedCoro;
    if (blocked->next) {
        blocked->next->prev = blockedCoro;
    }
    if (blockedCoro == cur_routine) {
        yield();
    }
}

// Put coroutine back to list of alive, so that it could be scheduled later
void Engine::unblock(void *coro) {
    auto unblockedCoro = static_cast<context *>(coro);
    if (unblockedCoro == nullptr || !unblockedCoro->isBlocked) {
        return;
    }
    unblockedCoro->isBlocked = false;
    // blocked list
    if (blocked == unblockedCoro) {
        blocked = blocked->next;
    }
    if (unblockedCoro->prev) {
        unblockedCoro->prev->next = unblockedCoro->next;
    }
    if (unblockedCoro->next) {
        unblockedCoro->next->prev = unblockedCoro->prev;
    }
    // alive list
    unblockedCoro->prev = nullptr;
    unblockedCoro->next = alive;
    alive = unblockedCoro;
    if (alive->next) {
        alive->next->prev = unblockedCoro;
    }
}

} // namespace Coroutine
} // namespace Afina