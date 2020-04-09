#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Afina {
namespace Concurrency {

/**
 * # Thread pool
 */
class Executor {
    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };;

public:
    Executor(size_t low_watermark, size_t hight_watermark, size_t max_queue_size, size_t idle_time)
        : _low_watermark(low_watermark), _hight_watermark(hight_watermark), _max_queue_size(max_queue_size),
          _idle_time(idle_time) {

        std::unique_lock<std::mutex> lock(mutex);
        state = State::kRun;
        for (int i = 0; i < _low_watermark; ++i) {
            std::thread t = std::thread([this]() { perform(this); });
            t.detach();
            threads.push_back(std::move(t));
        }
        _current_working = 0;
    }

/**
 * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
 * free. All enqueued jobs will be complete.
 *
 * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
 */
void Stop(bool await = false) {
    std::unique_lock<std::mutex> lock(mutex);
    state = State::kStopping;
    empty_condition.notify_all();
    if (await) {
        while (state != State::kStopped) {
            stop.wait(lock);
        }
    }
}

/**
 * Add function to be executed on the threadpool. Method returns true in case if task has been placed
 * onto execution queue, i.e scheduled for execution and false otherwise.
 *
 * That function doesn't wait for function result. Function could always be written in a way to notify caller about
 * execution finished by itself
 */
template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
    // Prepare "task"
    auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

    std::unique_lock<std::mutex> lock(this->mutex);
    if (state != State::kRun) {
        return false;
    }

    // Enqueue new task
    // Have not busy threads
    if (threads.size() - _current_working > 0) {
        tasks.push_back(exec);
        empty_condition.notify_one();
        return true;
    }

    // All busy, can create new
    if (_current_working < _hight_watermark) {
        std::thread t = std::thread([this]() { perform(this); });
        t.detach();
        threads.push_back(std::move(t));
        tasks.push_back(exec);
        return true;
    }

    // All busy, cant create new
    if (tasks.size() < _max_queue_size) {
        tasks.push_back(exec);
        return true;
    }

    return false;
}

    ~Executor() { Stop(true); }

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    void kill() {
        auto pid = std::this_thread::get_id();
        auto it = threads.begin();
        while (it != threads.end()) {
            if (it->get_id() == pid) {
                break;
            }
            it++;
        }
        threads.erase(it);
    }

    /**
    * Main function that all pool threads are running. It polls internal task queue and execute tasks
    */

    void perform(Executor *executor) {
        while (executor->state != State::kStopped) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(executor->mutex);
                while (true) {
                    while (executor->tasks.empty() && executor->state == State::kRun) {
                        if (executor->empty_condition.wait_until(
                                lock, std::chrono::high_resolution_clock::now() +
                                          std::chrono::microseconds(executor->_idle_time)) == std::cv_status::timeout) {
                            if (executor->threads.size() == executor->_low_watermark) {
                                continue;
                            }
                            executor->kill();
                            return;
                        }
                    }

                    if (!executor->tasks.empty()) {
                        break;
                    }

                    if (executor->state != Executor::State::kRun) {
                        executor->kill();
                        if (executor->threads.empty()) {
                            executor->state = Executor::State::kStopped;
                            lock.unlock();
                            executor->stop.notify_one();
                        }
                        return;
                    }
                }
                task = executor->tasks.front();
                executor->tasks.pop_front();
            }
            _current_working++;
            task();
            _current_working--;
        }
    }

    /**
     * Mutex to protect state below from concurrent modification
     */
    std::mutex mutex;

    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable empty_condition;

    /**
     * Vector of actual threads that perorm execution
     */
    std::vector<std::thread> threads;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> tasks;

    /**
     * Flag to stop bg threads
     */
    State state;

    size_t _low_watermark, _hight_watermark, _max_queue_size, _idle_time;
    std::atomic<size_t> _current_working;
    std::condition_variable stop;
};

} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
