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

#include <tacopie/network/io_service.hpp>
#include <tacopie/utils/error.hpp>
#include <tacopie/utils/logger.hpp>

#include <fcntl.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif /* _WIN32 */

namespace tacopie {

//!
//! default io_service getter & setter
//!

static std::shared_ptr<io_service> g_ptrIOServiceDefaultInstance = nullptr;

const std::shared_ptr<io_service>&
get_default_io_service(void) {
  if (g_ptrIOServiceDefaultInstance == nullptr) {
    g_ptrIOServiceDefaultInstance = std::make_shared<io_service>();
  }

  return g_ptrIOServiceDefaultInstance;
}

void
set_default_io_service(const std::shared_ptr<io_service>& service) {
  __TACOPIE_LOG(debug, "setting new default_io_service");
  g_ptrIOServiceDefaultInstance = service;
}

//!
//! ctor & dtor
//!

io_service::io_service(void)
#ifdef _WIN32
: m_bShouldStop_a(ATOMIC_VAR_INIT(false))
#else
: m_bShouldStop_a(false)
#endif /* _WIN32 */
, m_threadPoolCallbackWorkers(__TACOPIE_IO_SERVICE_NB_WORKERS) {
  __TACOPIE_LOG(debug, "create io_service");

  //! Start worker after everything has been initialized
  m_threadPollWorker = std::thread(std::bind(&io_service::poll, this));
}

io_service::~io_service(void) {
  __TACOPIE_LOG(debug, "destroy io_service");

  m_bShouldStop_a = true;

  m_selfPipeNotifier.notify();
  if (m_threadPollWorker.joinable()) {
    m_threadPollWorker.join();
  }
  m_threadPoolCallbackWorkers.stop();
}

//!
//! io service workers
//!
void
io_service::set_nb_workers(std::size_t nNbThreads) {
  m_threadPoolCallbackWorkers.set_nb_threads(nNbThreads);
}


//!
//! poll worker function
//!

void
io_service::poll(void) {
  __TACOPIE_LOG(debug, "starting poll() worker");

  while (!m_bShouldStop_a) {
    int nFds = init_poll_fds_info();

    //! setup timeout
    timeval* pTimeout = NULL;
#ifdef __TACOPIE_TIMEOUT
    timeval timeout;
    timeout.tv_usec = __TACOPIE_TIMEOUT;
    pTimeout        = &timeout;
#endif /* __TACOPIE_TIMEOUT */

    __TACOPIE_LOG(debug, "polling fds");
    if (select(nFds, &m_fdsetRead, &m_fdsetWrite, NULL, pTimeout) > 0) {
      process_events();
    } else {
      __TACOPIE_LOG(debug, "poll woke up, but nothing to process");
    }
  }

  __TACOPIE_LOG(debug, "stop poll() worker");
}

//!
//! process poll detected events
//!

void
io_service::process_events(void) {
  std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);

  __TACOPIE_LOG(debug, "processing events");

  for (const auto& fd : m_vctPolledFds) {
    if (fd == m_selfPipeNotifier.get_read_fd() && FD_ISSET(fd, &m_fdsetRead)) {
      m_selfPipeNotifier.clr_buffer();
      continue;
    }

    auto pos = m_mapTrackedSockets.find(fd);

    if (pos == m_mapTrackedSockets.end()) { continue; }

    auto& socket = pos->second;

    if (FD_ISSET(fd, &m_fdsetRead) && socket.callbackRead && !socket.bIsExecutingCallbackRead_a) {
      process_rd_event(fd, socket);
    }
    if (FD_ISSET(fd, &m_fdsetWrite) && socket.callbackWrite && !socket.bIsExecutingCallbackWrite_a) {
      process_wr_event(fd, socket);
    }

    if (socket.bMarkedForUntrack_a && !socket.bIsExecutingCallbackRead_a && !socket.bIsExecutingCallbackWrite_a) {
      __TACOPIE_LOG(debug, "untrack socket");
      m_mapTrackedSockets.erase(pos);
      m_cvWaitForRemoval.notify_all();
    }
  }
}

void
io_service::process_rd_event(const fd_t& fd, tracked_socket& socket) {
  __TACOPIE_LOG(debug, "processing read event");

  auto callbackRead = socket.callbackRead;

  socket.bIsExecutingCallbackRead_a = true;

  m_threadPoolCallbackWorkers << [=] {
    __TACOPIE_LOG(debug, "execute read callback");
    callbackRead(fd);

    std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);
    auto pos = m_mapTrackedSockets.find(fd);

    if (pos == m_mapTrackedSockets.end()) { return; }

    auto& socket                    = pos->second;
    socket.bIsExecutingCallbackRead_a = false;

    if (socket.bMarkedForUntrack_a && !socket.bIsExecutingCallbackWrite_a) {
      __TACOPIE_LOG(debug, "untrack socket");
      m_mapTrackedSockets.erase(pos);
      m_cvWaitForRemoval.notify_all();
    }

    m_selfPipeNotifier.notify();
  };
}

void
io_service::process_wr_event(const fd_t& fd, tracked_socket& socket) {
  __TACOPIE_LOG(debug, "processing write event");

  auto callbackWrite = socket.callbackWrite;

  socket.bIsExecutingCallbackWrite_a = true;

  m_threadPoolCallbackWorkers << [=] {
    __TACOPIE_LOG(debug, "execute write callback");
    callbackWrite(fd);

    std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);
    auto it = m_mapTrackedSockets.find(fd);

    if (it == m_mapTrackedSockets.end()) { return; }

    auto& socket                    = it->second;
    socket.bIsExecutingCallbackWrite_a = false;

    if (socket.bMarkedForUntrack_a && !socket.bIsExecutingCallbackRead_a) {
      __TACOPIE_LOG(debug, "untrack socket");
      m_mapTrackedSockets.erase(it);
      m_cvWaitForRemoval.notify_all();
    }

    m_selfPipeNotifier.notify();
  };
}

//!
//! init m_poll_fds_info
//!

int
io_service::init_poll_fds_info(void) {
  std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);

  m_vctPolledFds.clear();
  FD_ZERO(&m_fdsetRead);
  FD_ZERO(&m_fdsetWrite);

  int nFds = static_cast<int>(m_selfPipeNotifier.get_read_fd());
  FD_SET(m_selfPipeNotifier.get_read_fd(), &m_fdsetRead);
  m_vctPolledFds.push_back(m_selfPipeNotifier.get_read_fd());

  for (const auto& socket : m_mapTrackedSockets) {
    const auto& fd          = socket.first;
    const auto& socket_info = socket.second;

    bool bShouldRead = socket_info.callbackRead && !socket_info.bIsExecutingCallbackRead_a;
    if (bShouldRead) {
      FD_SET(fd, &m_fdsetRead);
    }

    bool bShouldWrite = socket_info.callbackWrite && !socket_info.bIsExecutingCallbackWrite_a;
    if (bShouldWrite) {
      FD_SET(fd, &m_fdsetWrite);
    }

    if (bShouldRead || bShouldWrite || socket_info.bMarkedForUntrack_a) {
      m_vctPolledFds.push_back(fd);
    }

    if ((bShouldRead || bShouldWrite) && static_cast<int>(fd) > nFds) {
      nFds = static_cast<int>(fd);
    }
  }

  return nFds + 1;
}

//!
//! track & untrack socket
//!

void
io_service::track(const tcp_socket& socket, const event_callback_t& callbackRead,
    const event_callback_t& callbackWrite) {
  std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);

  __TACOPIE_LOG(debug, "track new socket");

  auto& track_info                          = m_mapTrackedSockets[socket.get_fd()];
  track_info.callbackRead                   = callbackRead;
  track_info.callbackWrite                  = callbackWrite;
  track_info.bMarkedForUntrack_a            = false;
  track_info.bIsExecutingCallbackRead_a     = false;
  track_info.bIsExecutingCallbackWrite_a    = false;

  m_selfPipeNotifier.notify();
}

void
io_service::set_rd_callback(const tcp_socket& socket, const event_callback_t& callbackEvent) {
  std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);

  __TACOPIE_LOG(debug, "update read socket tracking callback");

  auto& track_info       = m_mapTrackedSockets[socket.get_fd()];
  track_info.callbackRead = callbackEvent;

  m_selfPipeNotifier.notify();
}

void
io_service::set_wr_callback(const tcp_socket& socket, const event_callback_t& callbackEvent) {
  std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);

  __TACOPIE_LOG(debug, "update write socket tracking callback");

  auto& track_info       = m_mapTrackedSockets[socket.get_fd()];
  track_info.callbackWrite = callbackEvent;

  m_selfPipeNotifier.notify();
}

void
io_service::untrack(const tcp_socket& socket) {
  std::lock_guard<std::mutex> lock(m_mtxTrackedSockets);

  auto pos = m_mapTrackedSockets.find(socket.get_fd());

  if (pos == m_mapTrackedSockets.end()) { return; }

  if (pos->second.bIsExecutingCallbackRead_a || pos->second.bIsExecutingCallbackWrite_a) {
    __TACOPIE_LOG(debug, "mark socket for untracking");
    pos->second.bMarkedForUntrack_a = true;
  } else {
    __TACOPIE_LOG(debug, "untrack socket");
    m_mapTrackedSockets.erase(pos);
    m_cvWaitForRemoval.notify_all();
  }

  m_selfPipeNotifier.notify();
}

//!
//! wait until the socket has been effectively removed
//! basically wait until all pending callbacks are executed
//!

void
io_service::wait_for_removal(const tcp_socket& socket) {
  std::unique_lock<std::mutex> lock(m_mtxTrackedSockets);

  __TACOPIE_LOG(debug, "waiting for socket removal");

  m_cvWaitForRemoval.wait(lock, [&]() {
    __TACOPIE_LOG(debug, "socket has been removed");

    return m_mapTrackedSockets.find(socket.get_fd()) == m_mapTrackedSockets.end();
  });
}

} // namespace tacopie
