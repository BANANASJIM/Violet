#pragma once

#include <EASTL/vector.h>
#include <EASTL/functional.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/queue.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>

namespace violet {

enum class TaskPriority {
    Low = 0,
    Normal = 1,
    High = 2
};

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<typename Func, typename... Args>
    auto submit(TaskPriority priority, Func&& func, Args&&... args)
        -> std::future<typename std::invoke_result_t<Func, Args...>>;

    template<typename Func, typename... Args>
    auto submit(Func&& func, Args&&... args)
        -> std::future<typename std::invoke_result_t<Func, Args...>> {
        return submit(TaskPriority::Normal, std::forward<Func>(func), std::forward<Args>(args)...);
    }

    void waitForAll();

    size_t getThreadCount() const { return workers.size(); }

    size_t getPendingTaskCount() const;

private:
    struct Task {
        eastl::function<void()> func;
        TaskPriority priority;

        bool operator<(const Task& other) const {
            return static_cast<int>(priority) < static_cast<int>(other.priority);
        }
    };

    void workerThread();

    eastl::vector<std::thread> workers;
    std::priority_queue<Task, eastl::vector<Task>> tasks;

    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop{false};
    std::atomic<size_t> activeTasks{0};
    std::condition_variable allTasksComplete;
};

template<typename Func, typename... Args>
auto ThreadPool::submit(TaskPriority priority, Func&& func, Args&&... args)
    -> std::future<typename std::invoke_result_t<Func, Args...>> {

    using ReturnType = typename std::invoke_result_t<Func, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
    );

    std::future<ReturnType> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (stop) {
            throw std::runtime_error("Cannot submit task to stopped ThreadPool");
        }

        tasks.push(Task{
            .func = [task]() { (*task)(); },
            .priority = priority
        });
    }

    condition.notify_one();
    return result;
}

} // namespace violet