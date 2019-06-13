// MIT License
//
// Copyright (c) 2016-2017 Simon Ninon <simon.ninon@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <tacopie/utils/logger.hpp>
#include <tacopie/utils/thread_pool.hpp>

namespace tacopie {

namespace utils {

//!
//! ctor & dtor
//!

thread_pool::thread_pool(std::size_t nNbThreads) {
  __TACOPIE_LOG(debug, "create thread_pool");
  set_nb_threads(nNbThreads);
}

thread_pool::~thread_pool(void) {
  __TACOPIE_LOG(debug, "destroy thread_pool");
  stop();
}

//!
//! worker main loop
//!

void
thread_pool::run(void) {
  __TACOPIE_LOG(debug, "start run() worker");

  while (true) {
    auto pairTaskInfo   = fetch_task_or_stop();
    bool bStopped       = pairTaskInfo.first;
    task_t task         = pairTaskInfo.second;

    //! if thread has been requested to stop, stop it here
    if (bStopped) {
      break;
    }

    //! execute task
    if (task) {
      __TACOPIE_LOG(debug, "execute task");

      try {
        task();
      }
      catch (const std::exception&) {
        __TACOPIE_LOG(warn, "uncatched exception propagated up to the threadpool.")
      }

      __TACOPIE_LOG(debug, "execution complete");
    }
  }

  __TACOPIE_LOG(debug, "stop run() worker");
}

//!
//! stop the thread pool and wait for workers completion
//!

void
thread_pool::stop(void) {
  if (!is_running()) { return; }

  m_bShouldStop_a = true;
  m_cvTasks.notify_all();

  for (auto& worker : m_lstWorkerThreads) { worker.join(); }

  m_lstWorkerThreads.clear();

  __TACOPIE_LOG(debug, "thread_pool stopped");
}

//!
//! whether the thread_pool is running or not
//!
bool
thread_pool::is_running(void) const {
  return !m_bShouldStop_a;
}

//!
//! whether the current thread should stop or not
//!
bool
thread_pool::should_stop(void) const {
  return m_bShouldStop_a || m_nNbRunningThreads_a > m_uMaxNbThreads_a;
}

//!
//! retrieve a new task
//!

std::pair<bool, thread_pool::task_t>
thread_pool::fetch_task_or_stop(void) {
  std::unique_lock<std::mutex> lock(m_mtxTasks);

  __TACOPIE_LOG(debug, "waiting to fetch task");

  m_cvTasks.wait(lock, [&] { return should_stop() || !m_queTasks.empty(); });

  if (should_stop()) {
    --m_nNbRunningThreads_a;
    return {true, nullptr};
  }

  task_t task = std::move(m_queTasks.front());
  m_queTasks.pop();
  return {false, task};
}

//!
//! add tasks to thread pool
//!

void
thread_pool::add_task(const task_t& task) {
  std::lock_guard<std::mutex> lock(m_mtxTasks);

  __TACOPIE_LOG(debug, "add task to thread_pool");

  m_queTasks.push(task);
  m_cvTasks.notify_one();
}

thread_pool&
thread_pool::operator<<(const task_t& task) {
  add_task(task);

  return *this;
}

//!
//! adjust number of threads
//!
void
thread_pool::set_nb_threads(std::size_t uNbThreads) {
  m_uMaxNbThreads_a = uNbThreads;

  //! if we increased the number of threads, spawn them
  while (m_nNbRunningThreads_a < m_uMaxNbThreads_a) {
    ++m_nNbRunningThreads_a;
    m_lstWorkerThreads.push_back(std::thread(std::bind(&thread_pool::run, this)));
  }

  //! otherwise, wake up threads to make them stop if necessary (until we get the right amount of threads)
  if (m_nNbRunningThreads_a > m_uMaxNbThreads_a) {
    m_cvTasks.notify_all();
  }
}

} // namespace utils

} // namespace tacopie
