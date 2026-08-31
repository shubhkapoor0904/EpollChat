#include "ThreadPool.hpp"
#include <iostream>

ThreadPool::ThreadPool(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = 1;
    }

    m_workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->m_queueMutex);
                    this->m_cv.wait(lock, [this] {
                        return this->m_stop || !this->m_tasks.empty();
                    });

                    if (this->m_stop && this->m_tasks.empty()) {
                        return;
                    }

                    task = std::move(this->m_tasks.front());
                    this->m_tasks.pop();
                }

                try {
                    task();
                } catch (const std::exception& e) {
                    std::cerr << "[ThreadPool Exception] " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[ThreadPool Exception] Unknown error occurred in worker thread." << std::endl;
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        if (m_stop) {
            throw std::runtime_error("Enqueue called on stopped ThreadPool");
        }
        m_tasks.push(std::move(task));
    }
    m_cv.notify_one();
}

void ThreadPool::stop() {
    bool expected = false;
    if (m_stop.compare_exchange_strong(expected, true)) {
        m_cv.notify_all();
        for (std::thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}
