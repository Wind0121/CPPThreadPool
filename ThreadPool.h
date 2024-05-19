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

  ThreadPool() : ThreadPool(std::thread::hardware_concurrency()) {}

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lg(mutex_);
      quit_ = true;
    }
    cv_.notify_all();
    for (auto &iter : threads_) {
      assert(iter.second.joinable());
      iter.second.join();
    }
  }

  // 使用std::result_of来推断函数返回值类型,并构造std::future
  template <typename Func, typename... T>
  auto Submit(Func &&func, T &&...args)
      -> std::future<typename std::result_of<Func(T...)>::type> {

    // 将函数与参数捆绑在一起,这里使用std::forward是为了按左值或右值原样转发
    auto execute =
        std::bind(std::forward<Func>(func), std::forward<T>(args)...);

    using return_type = typename std::result_of<Func(T...)>::type;
    // 这里要用std::make_shared而不是std::shared_ptr
    auto task =
        std::make_shared<std::packaged_task<return_type()>>(std::move(execute));
    // 获取返回值future
    auto result = task->get_future();

    assert(!quit_);

    // 进入上锁区域
    std::lock_guard<std::mutex> lg(mutex_);
    // 这里用lambda函数来传入任务
    queue_.emplace([task]() { (*task)(); });

    if (idle_threads_ > 0) {
      // 有空闲线程就直接唤醒
      cv_.notify_one();
    } else if (current_threads_ < max_threads_) {
      // 没有空闲线程且线程数未达到上限,就创建一个线程,这里要给线程传入Worker函数,保持运行
      std::thread thread(&ThreadPool::Worker, this);
      assert(threads_.find(thread.get_id()) == threads_.end());
      // 加入到线程池中,一定要用std::move来移动,thread不支持拷贝
      threads_[thread.get_id()] = std::move(thread);
      ++current_threads_;
    } else {
      // do nothing
      // 线程数已经达到上限
    }

    return result;
  }

  size_t CurrentThreadsNum() const {
    std::lock_guard<std::mutex> lg(mutex_);
    return current_threads_;
  }

private:
  void Worker() {
    // 循环运行,直到退出
    while (true) {
      std::function<void()> task;
      {
        // 上锁区间
        std::unique_lock<std::mutex> ul(mutex_);
        ++idle_threads_;
        // 注: wait_for的返回值实际上是predicate_func的返回值
        bool has_timeout =
            !cv_.wait_for(ul, std::chrono::seconds(kWaitSeconds),
                          [this]() { return quit_ || !queue_.empty(); });
        --idle_threads_;

        if (queue_.empty()) {
          if (quit_) {
            // 线程池调用析构函数
            --current_threads_;
            return;
          }
          if (has_timeout) {
            // 如果是超时,就将其加入到可以finish的队列
            --current_threads_;
            JoinFinishedThreads();
            finished_threads_.emplace(std::this_thread::get_id());
            return;
          }
        }
        // 这里要用move来获取函数,避免多次拷贝
        task = std::move(queue_.front());
        queue_.pop();
      }
      task();
    }
  }

  void JoinFinishedThreads() {
    while (!finished_threads_.empty()) {
      auto id = std::move(finished_threads_.front());
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
  std::condition_variable cv_;
  std::unordered_map<std::thread::id, std::thread> threads_;
  std::queue<std::function<void()>> queue_;
  std::queue<std::thread::id> finished_threads_;
};

constexpr size_t ThreadPool::kWaitSeconds;
} // namespace mpool