#include "ThreadPool.hpp"
#include "Log.hpp"

namespace violet {

ThreadPool::ThreadPool(size_t numThreads) {
    // If numThreads is 0, use hardware concurrency
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            numThreads = 4; // Fallback to 4 threads
        }
    }

    Log::info("ThreadPool", "Initializing with {} worker threads", numThreads);

    workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] { workerThread(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }

    condition.notify_all();

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    Log::info("ThreadPool", "Thread pool shutdown complete");
}

void ThreadPool::workerThread() {
    while (true) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return stop || !tasks.empty(); });

            if (stop && tasks.empty()) {
                return;
            }

            if (!tasks.empty()) {
                task = eastl::move(const_cast<Task&>(tasks.top()));
                tasks.pop();
                ++activeTasks;
            }
        }

        // Execute task outside the lock
        if (task.func) {
            try {
                task.func();
            } catch (const std::exception& e) {
                Log::error("ThreadPool", "Task execution failed: {}", e.what());
            } catch (...) {
                Log::error("ThreadPool", "Task execution failed with unknown exception");
            }

            --activeTasks;
            allTasksComplete.notify_all();
        }
    }
}

void ThreadPool::waitForAll() {
    std::unique_lock<std::mutex> lock(queueMutex);
    allTasksComplete.wait(lock, [this] {
        return tasks.empty() && activeTasks == 0;
    });
}

size_t ThreadPool::getPendingTaskCount() const {
    std::unique_lock<std::mutex> lock(queueMutex);
    return tasks.size() + activeTasks.load();
}

} // namespace violet