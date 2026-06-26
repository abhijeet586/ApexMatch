/**
 * @file ThreadPool.cpp
 * @brief Implementation of the fixed-size ThreadPool.
 * @author Abhijeet Senapati
 *
 * Worker Thread Lifecycle:
 *   1. Block on condition_variable until a task is available or shutdown is signaled.
 *   2. Dequeue the front task under the mutex.
 *   3. Execute the task outside the critical section (no lock held).
 *   4. Loop back to step 1.
 *   5. On shutdown: drain remaining tasks, then exit.
 */

#include "ThreadPool.h"
#include <iostream>

namespace apex {

ThreadPool::ThreadPool(size_t numThreads) : stop_(false) {
    if (numThreads == 0) {
        throw std::invalid_argument("[ThreadPool] numThreads must be > 0.");
    }

    workers_.reserve(numThreads);

    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this, i] {
            /*
             * Worker loop: each thread blocks on the condition variable
             * until either a new task arrives or the pool is shutting down.
             *
             * On shutdown, workers finish all remaining queued tasks
             * before terminating — ensuring no work is silently dropped.
             */
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(queueMutex_);

                    // Wait until there is a task or we are told to stop.
                    condition_.wait(lock, [this] {
                        return stop_ || !taskQueue_.empty();
                    });

                    // Exit only if stopped AND no remaining work.
                    if (stop_ && taskQueue_.empty()) {
                        return;
                    }

                    task = std::move(taskQueue_.front());
                    taskQueue_.pop();
                }

                // Execute the task outside the lock to maximize concurrency.
                task();
            }
        });
    }

    std::cout << "[ThreadPool] Initialized with "
              << numThreads << " worker threads.\n";
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (stop_) return;  // Idempotent: already shut down.
        stop_ = true;
    }

    // Wake all workers so they can observe the stop flag.
    condition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    std::cout << "[ThreadPool] All workers joined. Shutdown complete.\n";
}

} // namespace apex
