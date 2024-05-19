#include "ThreadPool.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
using namespace mpool;
constexpr size_t kThreadsNum = 100;

std::mutex cnt_mutex;

void task(int task_id){
    {
        std::lock_guard<std::mutex> lg(cnt_mutex);
        std::cout << "task-" << task_id << " start\n";
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    {
        std::lock_guard<std::mutex> lg(cnt_mutex);
        std::cout << "task-" << task_id << " end\n";
    }
}

void monitor(const ThreadPool& pool,int seconds){
    for(int i = 1;i < seconds * 10;i++){
        {
            std::lock_guard<std::mutex> lg(cnt_mutex);
            std::cout << "current threads num: " << pool.CurrentThreadsNum() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(){
    ThreadPool pool(kThreadsNum);
    pool.Submit(monitor, std::ref(pool),13);
    for(int i = 1;i < 100;i++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pool.Submit(task, i);
    }
    return 0;
}