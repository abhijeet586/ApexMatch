/**
 * @file threadpool.cpp
 * @brief implementation of the fixed-size threadpool.
 * @author Abhijeet Senapati
 *
 * worker thread lifecycle:
 *   1. block on condition_variable until a task is available or shutdown is signaled.
 *   2. dequeue the front task under the mutex.
 *   3. execute the task outside the critical section (no lock held).
 *   4. loop back to step 1.
 *   5. on shutdown: drain remaining tasks, then exit.
 */

#include "ThreadPool.h"
#include <iostream>

using namespace std;

namespace apex {

ThreadPool::ThreadPool(size_t numThreads) : stop_(false) {
    if (numThreads == 0) {
        throw invalid_argument("[ThreadPool] numThreads must be > 0.");
    }

    workers_.reserve(numThreads);

    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this, i] {
            /*
             * worker loop: each thread blocks on the condition variable
             * until either a new task arrives or the pool is shutting down.
             *
             * on shutdown, workers finish all remaining queued tasks
             * before terminating — ensuring no work is silently dropped.
             */
            while (true) {
                function<void()> task;
                {
                    unique_lock<mutex> lock(queueMutex_);
                    //wait until there is a task or we are told to stop
                    condition_.wait(lock, [this] {
                        return stop_ || !taskQueue_.empty();
                    });
                    //exit only if stopped and no remaining work
                    if (stop_ && taskQueue_.empty()) {
                        return;
                    }
                    task = move(taskQueue_.front());
                    taskQueue_.pop();
                }
                //execute the task outside the lock to maximize concurrency
                task();
            }
        });
    }
    cout << "[ThreadPool] Initialized with "<< numThreads << " worker threads.\n";
}

ThreadPool::~ThreadPool(){
    shutdown();
}

void ThreadPool::shutdown(){
    {
        unique_lock<mutex> lock(queueMutex_);
        if (stop_) return;  //idempotent: already shut down.
        stop_ = true;
    }
    //wake all workers so they can observe the stop flag.
    condition_.notify_all();
    for (auto& worker:workers_) {
        if (worker.joinable()){
            worker.join();
        }
    }
    cout<<"[ThreadPool] All workers joined. Shutdown complete."<<endl;
}

}//namespace apex
