#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace aicoder
{

  // 简单固定大小线程池，执行队列中的任务。
  // 任务按提交顺序串行执行（适合 I/O 为主的工作）。
  class ThreadPool
  {
  public:
    // nthreads 默认 1，适合当前单对话串行模型。
    explicit ThreadPool(size_t nthreads = 1) : stop_(false)
    {
      threads_.reserve(nthreads);
      for (size_t i = 0; i < nthreads; ++i)
      {
        threads_.emplace_back([this]
                              {
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(queueMutex_);
            cv_.wait(lock, [this] { return stop_.load() || !tasks_.empty(); });
            if (stop_.load() && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
          }
          task();
        } });
      }
    }

    ~ThreadPool()
    {
      stop_.store(true);
      cv_.notify_all();
      for (auto &t : threads_)
        t.join();
    }

    // 提交一个任务。所有任务按提交顺序串行执行。
    // 返回任务是否成功入队。
    bool submit(std::function<void()> task)
    {
      {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (stop_.load())
          return false;
        tasks_.push(std::move(task));
      }
      cv_.notify_one();
      return true;
    }

  private:
    std::atomic<bool> stop_;
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable cv_;
  };

} // namespace aicoder