/**
 * @file threadpool.h
 * @brief a fixed-size thread pool for concurrent connection handling.
 * @author Abhijeet Senapati
 *
 * implements a classic bounded thread pool pattern:
 *   - a fixed number of worker threads are pre-allocated at construction.
 *   - tasks are submitted via enqueue() and placed on a shared fifo queue.
 *   - workers wait on a condition variable and wake to consume tasks.
 *   - enqueue() returns a future for the caller to retrieve results.
 *
 * concurrency guarantees:
 *   - the task queue is protected by a mutex.
 *   - workers are signaled via condition_variable (no busy-wait).
 *   - graceful shutdown drains remaining tasks before joining threads.
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

using namespace std;

class ThreadPool {
public:
    /**
     * @brief constructs a thread pool and spawns worker threads.
     * @param numthreads number of persistent worker threads.
     * @throws invalid_argument if numthreads is 0.
     */
    explicit ThreadPool(size_t numThreads);

    /**
     * @brief destructor. signals shutdown and joins all workers.
     */
    ~ThreadPool();

    //non-copyable,non-movable.
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    /**
     * @brief enqueues a callable task and returns a future for its result.
     *
     * the task will be picked up by the next available worker thread.
     * this method is fully thread-safe and may be called concurrently
     * from multiple threads.
     *
     * @tparam f   callable type.
     * @tparam args argument types forwarded to the callable.
     * @param f    the callable to execute.
     * @param args arguments forwarded to the callable.
     * @return future<returntype> for the task's eventual result.
     * @throws runtime_error if the pool has been shut down.
     */
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> future<typename invoke_result<F, Args...>::type>;

    /**
     * @brief gracefully shuts down the pool.
     *
     * signals all workers to stop, then joins them. any tasks remaining
     * in the queue will be completed before workers exit.
     * safe to call multiple times (idempotent).
     */
    void shutdown();
    /**
     * @brief returns the number of worker threads in the pool.
     */
    size_t size() const noexcept { return workers_.size(); }

private:
    vector<thread>          workers_;      ///< pre-allocated worker threads.
    queue<function<void()>> taskQueue_;     ///< fifo task queue.
    mutable mutex           queueMutex_;   ///< protects taskqueue_ and stop_.
    condition_variable      condition_;     ///< signals workers on new tasks / shutdown.
    bool                    stop_;          ///< shutdown flag.
};

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> future<typename invoke_result<F, Args...>::type>
{
    using ReturnType = typename invoke_result<F, Args...>::type;

    //wrap the callable + args into a packaged_task for future retrieval.
    auto task = make_shared<packaged_task<ReturnType()>>(
        bind(forward<F>(f), forward<Args>(args)...)
    );

    future<ReturnType> result = task->get_future();

    {
        unique_lock<mutex> lock(queueMutex_);
        if (stop_) {
            throw runtime_error(
                "[ThreadPool] Cannot enqueue: pool has been shut down.");
        }
        taskQueue_.emplace([task]() { (*task)(); });
    }

    condition_.notify_one();//wake one waiting worker.
    return result;
}

}//namespace apex
