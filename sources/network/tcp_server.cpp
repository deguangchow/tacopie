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

#include <tacopie/network/tcp_server.hpp>
#include <tacopie/utils/error.hpp>
#include <tacopie/utils/logger.hpp>

#include <algorithm>

namespace tacopie {

//!
//! ctor & dtor
//!

tcp_server::tcp_server(void)
: m_ptrIOService(get_default_io_service())
, m_callbackOnNewConnection(nullptr) { __TACOPIE_LOG(debug, "create tcp_server"); }

tcp_server::~tcp_server(void) {
  __TACOPIE_LOG(debug, "destroy tcp_server");
  stop();
}

//!
//! start & stop the tcp server
//!

void
tcp_server::start(const std::string& sHost, std::uint32_t uPort, const on_new_connection_callback_t& callback) {
  if (is_running()) { __TACOPIE_THROW(warn, "tcp_server is already running"); }

  m_tcpSocket.bind(sHost, uPort);
  m_tcpSocket.listen(__TACOPIE_CONNECTION_QUEUE_SIZE);

  m_ptrIOService->track(m_tcpSocket);
  m_ptrIOService->set_rd_callback(m_tcpSocket, std::bind(&tcp_server::on_read_available, this, std::placeholders::_1));
  m_callbackOnNewConnection = callback;

  m_bIsRunning_a = true;

  __TACOPIE_LOG(info, "tcp_server running");
}

void
tcp_server::stop(bool bWaitForRemoval, bool bRecursiveWaitForRemoval) {
  if (!is_running()) { return; }

  m_bIsRunning_a = false;

  m_ptrIOService->untrack(m_tcpSocket);
  if (bWaitForRemoval) { m_ptrIOService->wait_for_removal(m_tcpSocket); }
  m_tcpSocket.close();

  std::lock_guard<std::mutex> lock(m_mtxClients);
  for (auto& client : m_lstClients) {
    client->disconnect(bRecursiveWaitForRemoval && bWaitForRemoval);
  }
  m_lstClients.clear();

  __TACOPIE_LOG(info, "tcp_server stopped");
}

//!
//! io service read callback
//!

void
tcp_server::on_read_available(fd_t) {
  try {
    __TACOPIE_LOG(info, "tcp_server received new connection");

    auto ptrTcpClient = std::make_shared<tcp_client>(m_tcpSocket.accept());

    if (!m_callbackOnNewConnection || !m_callbackOnNewConnection(ptrTcpClient)) {
      __TACOPIE_LOG(info, "connection handling delegated to tcp_server");

      ptrTcpClient->set_on_disconnection_handler(std::bind(&tcp_server::on_client_disconnected, this, ptrTcpClient));
      m_lstClients.push_back(ptrTcpClient);
    } else {
      __TACOPIE_LOG(info, "connection handled by tcp_server wrapper");
    }
  }
  catch (const tacopie::tacopie_error&) {
    __TACOPIE_LOG(warn, "accept operation failure");
    stop();
  }
}

//!
//! client disconnected
//!

void
tcp_server::on_client_disconnected(const std::shared_ptr<tcp_client>& client) {
  //! If we are not running the server
  //! Then it means that this function is called by tcp_client::disconnect() at the destruction of all clients
  if (!is_running()) { return; }

  __TACOPIE_LOG(debug, "handle server's client disconnection");

  std::lock_guard<std::mutex> lock(m_mtxClients);
  auto pos = std::find(m_lstClients.begin(), m_lstClients.end(), client);

  if (pos != m_lstClients.end()) { m_lstClients.erase(pos); }
}

//!
//! returns whether the server is currently running or not
//!

bool
tcp_server::is_running(void) const {
  return m_bIsRunning_a;
}

//!
//! get socket
//!

tcp_socket&
tcp_server::get_socket(void) {
  return m_tcpSocket;
}

const tcp_socket&
tcp_server::get_socket(void) const {
  return m_tcpSocket;
}

//!
//! io_service getter
//!
const std::shared_ptr<tacopie::io_service>&
tcp_server::get_io_service(void) const {
  return m_ptrIOService;
}

//!
//! get client sockets
//!

const std::list<std::shared_ptr<tacopie::tcp_client>>&
tcp_server::get_clients(void) const {
  return m_lstClients;
}

//!
//! comparison operator
//!
bool
tcp_server::operator==(const tcp_server& rhs) const {
  return m_tcpSocket == rhs.m_tcpSocket;
}

bool
tcp_server::operator!=(const tcp_server& rhs) const {
  return !operator==(rhs);
}

} // namespace tacopie
