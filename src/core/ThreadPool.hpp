#pragma once

#include <EASTL/vector.h>
#include <EASTL/functional.h>
#include <EASTL/queue.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace violet {

// Simple thread pool for async asset loading
// Uses EASTL containers with standard synchronization primitives
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a task for execution
    void submit(eastl::function<void()> task);

    // Wait for all tasks to complete
    void waitForAll();

    // Get number of worker threads
    size_t getThreadCount() const { return workers.size(); }

    // Get number of pending tasks
    size_t getPendingTaskCount() const;

private:
    void workerThread();

    eastl::vector<std::thread> workers;
    eastl::queue<eastl::function<void()>> tasks;

    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::condition_variable allTasksComplete;
    std::atomic<bool> stop{false};
    std::atomic<size_t> activeTasks{0};
};

} // namespace violet