#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <ratio>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace mpool {

class ThreadPool {
public:
  explicit ThreadPool(size_t thread_num)
      : max_threads_(thread_num), current_threads_(0), idle_threads_(0),
        quit_(false) {}

  ThreadPool() : mpool::ThreadPool(std::thread::hardware_concurrency()) {}

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lg(mutex_);
      quit_ = false;
    }
    cv.notify_all();
    for (auto &iter : threads_) {
      assert(iter.second.joinable());
      iter.second.join();
    }
  }

  template <typename Func, typename... T>
  auto Submit(Func &&func, T &&...args)
      -> std::future<typename std::result_of<Func(T...)>::type> {
    auto execute =
        std::bind(std::forward<Func>(func), std::forward<T>(args)...);

    using return_type = typename std::result_of<Func(T...)>::type;

    auto task =
        std::shared_ptr<std::packaged_task<return_type()>>(std::move(execute));
    auto result = task->get_future();

    assert(!quit_);

    // 进入上锁区域
    std::lock_guard<std::mutex> lg(mutex_);

    queue_.emplace([task]() { (*task)(); });

    if (idle_threads_ > 0) {
      cv.notify_one();
    } else if (current_threads_ < max_threads_) {
      std::thread thread(&ThreadPool::Worker, this);
      assert(threads_.find(thread.get_id()) == threads_.end());
      threads_[thread.get_id()] = std::move(thread);
      ++current_threads_;
    } else {
      // do nothing
    }

    return result;
  }

  size_t CurrentThreadsNum() const {
    std::lock_guard<std::mutex> lg(mutex_);
    return current_threads_;
  }

private:
  void Worker() {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> ul(mutex_);
      ++idle_threads_;
      bool has_timeout =
          cv.wait_for(ul, std::chrono::seconds(kWaitSeconds),
                      [this]() { return !queue_.empty() || quit_; });

      if (queue_.empty()) {
        if (quit_) {
          --current_threads_;
          return;
        }
        if (has_timeout) {
          --current_threads_;
          JoinFinishedThreads();
          finished_threads_.emplace(std::this_thread::get_id());
          return;
        }
      }

      task = std::move(queue_.front());
      queue_.pop();
    }
    task();
  }

  void JoinFinishedThreads() {
    while (!finished_threads_.empty()) {
      auto id = finished_threads_.front();
      finished_threads_.pop();
      auto iter = threads_.find(id);
      assert(iter != threads_.end());
      assert(iter->second.joinable());
      iter->second.join();
      threads_.erase(iter);
    }
  }

  static constexpr size_t kWaitSeconds = 2;
  mutable std::mutex mutex_;
  size_t max_threads_;
  size_t current_threads_;
  size_t idle_threads_;
  bool quit_;
  std::condition_variable cv;
  std::unordered_map<std::thread::id, std::thread> threads_;
  std::queue<std::function<void()>> queue_;
  std::queue<std::thread::id> finished_threads_;
};

constexpr size_t ThreadPool::kWaitSeconds;
} // namespace mpool