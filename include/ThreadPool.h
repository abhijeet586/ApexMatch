/**
 * @file ThreadPool.h
 * @brief A fixed-size Thread Pool for concurrent connection handling.
 * @author Abhijeet Senapati
 *
 * Implements a classic bounded thread pool pattern:
 *   - A fixed number of worker threads are pre-allocated at construction.
 *   - Tasks are submitted via enqueue() and placed on a shared FIFO queue.
 *   - Workers wait on a condition variable and wake to consume tasks.
 *   - enqueue() returns a std::future for the caller to retrieve results.
 *
 * Concurrency guarantees:
 *   - The task queue is protected by a mutex.
 *   - Workers are signaled via std::condition_variable (no busy-wait).
 *   - Graceful shutdown drains remaining tasks before joining threads.
 */

#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>
#include <type_traits>

namespace apex {

class ThreadPool {
public:
    /**
     * @brief Constructs a thread pool and spawns worker threads.
     * @param numThreads Number of persistent worker threads.
     * @throws std::invalid_argument if numThreads is 0.
     */
    explicit ThreadPool(size_t numThreads);

    /**
     * @brief Destructor. Signals shutdown and joins all workers.
     */
    ~ThreadPool();

    // Non-copyable, non-movable.
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    /**
     * @brief Enqueues a callable task and returns a future for its result.
     *
     * The task will be picked up by the next available worker thread.
     * This method is fully thread-safe and may be called concurrently
     * from multiple threads.
     *
     * @tparam F   Callable type.
     * @tparam Args Argument types forwarded to the callable.
     * @param f    The callable to execute.
     * @param args Arguments forwarded to the callable.
     * @return std::future<ReturnType> for the task's eventual result.
     * @throws std::runtime_error if the pool has been shut down.
     */
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief Gracefully shuts down the pool.
     *
     * Signals all workers to stop, then joins them. Any tasks remaining
     * in the queue will be completed before workers exit.
     * Safe to call multiple times (idempotent).
     */
    void shutdown();

    /**
     * @brief Returns the number of worker threads in the pool.
     */
    size_t size() const noexcept { return workers_.size(); }

private:
    std::vector<std::thread>          workers_;      ///< Pre-allocated worker threads.
    std::queue<std::function<void()>> taskQueue_;     ///< FIFO task queue.
    mutable std::mutex                queueMutex_;   ///< Protects taskQueue_ and stop_.
    std::condition_variable           condition_;     ///< Signals workers on new tasks / shutdown.
    bool                              stop_;          ///< Shutdown flag.
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Template Implementation (must remain in header)
// ═══════════════════════════════════════════════════════════════════════════════

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using ReturnType = typename std::invoke_result<F, Args...>::type;

    // Wrap the callable + args into a packaged_task for future retrieval.
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<ReturnType> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (stop_) {
            throw std::runtime_error(
                "[ThreadPool] Cannot enqueue: pool has been shut down.");
        }
        taskQueue_.emplace([task]() { (*task)(); });
    }

    condition_.notify_one();  // Wake one waiting worker.
    return result;
}

} // namespace apex
