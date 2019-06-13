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

#include <tacopie/network/tcp_client.hpp>
#include <tacopie/utils/error.hpp>
#include <tacopie/utils/logger.hpp>

namespace tacopie {

//!
//! ctor & dtor
//!

tcp_client::tcp_client(void)
: m_handlerDisconnection(nullptr) {
  m_ptrIOService = get_default_io_service();
  __TACOPIE_LOG(debug, "create tcp_client");
}

tcp_client::~tcp_client(void) {
  __TACOPIE_LOG(debug, "destroy tcp_client");
  disconnect(true);
}

//!
//! custom ctor
//! build client from existing socket
//!

tcp_client::tcp_client(tcp_socket&& socket)
: m_ptrIOService(get_default_io_service())
, m_tcpSocket(std::move(socket))
, m_handlerDisconnection(nullptr) {
  m_bIsConnected_a = true;
  __TACOPIE_LOG(debug, "create tcp_client");
  m_ptrIOService->track(m_tcpSocket);
}

//!
//! get host & port information
//!

const std::string&
tcp_client::get_host(void) const {
  return m_tcpSocket.get_host();
}

std::uint32_t
tcp_client::get_port(void) const {
  return m_tcpSocket.get_port();
}

//!
//! start & stop the tcp client
//!

void
tcp_client::connect(const std::string& sHost, std::uint32_t uPort, std::uint32_t uTimeoutMsecs) {
  if (is_connected()) { __TACOPIE_THROW(warn, "tcp_client is already connected"); }

  try {
    m_tcpSocket.connect(sHost, uPort, uTimeoutMsecs);
    m_ptrIOService->track(m_tcpSocket);
  }
  catch (const tacopie_error& e) {
    m_tcpSocket.close();
    throw e;
  }

  m_bIsConnected_a = true;

  __TACOPIE_LOG(info, "tcp_client connected");
}

void
tcp_client::disconnect(bool bWaitForRemoval) {
  if (!is_connected()) { return; }

  //! update state
  m_bIsConnected_a = false;

  //! clear all pending requests
  clear_read_requests();
  clear_write_requests();

  //! remove socket from io service and wait for removal if necessary
  m_ptrIOService->untrack(m_tcpSocket);
  if (bWaitForRemoval) { m_ptrIOService->wait_for_removal(m_tcpSocket); }

  //! close the socket
  m_tcpSocket.close();

  __TACOPIE_LOG(info, "tcp_client disconnected");
}

//!
//! Clear pending requests
//!
void
tcp_client::clear_read_requests(void) {
  std::lock_guard<std::mutex> lock(m_mtxReadRequests);

  std::queue<read_request> empty;
  std::swap(m_queReadRequests, empty);
}

void
tcp_client::clear_write_requests(void) {
  std::lock_guard<std::mutex> lock(m_mtxWriteRequests);

  std::queue<write_request> empty;
  std::swap(m_queWriteRequests, empty);
}

//!
//! Call disconnection handler
//!
void
tcp_client::call_disconnection_handler(void) {
  if (m_handlerDisconnection) {
    m_handlerDisconnection();
  }
}

//!
//! io service read callback
//!

void
tcp_client::on_read_available(fd_t) {
  __TACOPIE_LOG(info, "read available");

  read_result resultRead;
  auto callbackRead = process_read(resultRead);

  if (!resultRead.success) {
    __TACOPIE_LOG(warn, "read operation failure");
    disconnect();
  }

  if (callbackRead) { callbackRead(resultRead); }

  if (!resultRead.success) { call_disconnection_handler(); }
}

//!
//! io service write callback
//!

void
tcp_client::on_write_available(fd_t) {
  __TACOPIE_LOG(info, "write available");

  write_result resultWrite;
  auto callbackWrite = process_write(resultWrite);

  if (!resultWrite.success) {
    __TACOPIE_LOG(warn, "write operation failure");
    disconnect();
  }

  if (callbackWrite) { callbackWrite(resultWrite); }

  if (!resultWrite.success) { call_disconnection_handler(); }
}

//!
//! process read & write operations when available
//!

tcp_client::async_read_callback_t
tcp_client::process_read(read_result& resultRead) {
  std::lock_guard<std::mutex> lock(m_mtxReadRequests);

  if (m_queReadRequests.empty()) { return nullptr; }

  const auto& requestRead   = m_queReadRequests.front();
  auto callbackRead         = requestRead.callbackAsyncRead;

  try {
    resultRead.buffer  = m_tcpSocket.recv(requestRead.nSizeToRead);
    resultRead.success = true;
  }
  catch (const tacopie::tacopie_error&) {
    resultRead.success = false;
  }

  m_queReadRequests.pop();

  if (m_queReadRequests.empty()) { m_ptrIOService->set_rd_callback(m_tcpSocket, nullptr); }

  return callbackRead;
}

tcp_client::async_write_callback_t
tcp_client::process_write(write_result& resultWrite) {
  std::lock_guard<std::mutex> lock(m_mtxWriteRequests);

  if (m_queWriteRequests.empty()) { return nullptr; }

  const auto& requestWrite = m_queWriteRequests.front();
  auto callbackWrite       = requestWrite.callbackAsyncWrite;

  try {
    resultWrite.size    = m_tcpSocket.send(requestWrite.vctBuffer, requestWrite.vctBuffer.size());
    resultWrite.success = true;
  }
  catch (const tacopie::tacopie_error&) {
    resultWrite.success = false;
  }

  m_queWriteRequests.pop();

  if (m_queWriteRequests.empty()) { m_ptrIOService->set_wr_callback(m_tcpSocket, nullptr); }

  return callbackWrite;
}

//!
//! async read & write operations
//!

void
tcp_client::async_read(const read_request& requestRead) {
  std::lock_guard<std::mutex> lock(m_mtxReadRequests);

  if (is_connected()) {
    m_ptrIOService->set_rd_callback(m_tcpSocket,
        std::bind(&tcp_client::on_read_available, this, std::placeholders::_1));
    m_queReadRequests.push(requestRead);
  } else {
    __TACOPIE_THROW(warn, "tcp_client is disconnected");
  }
}

void
tcp_client::async_write(const write_request& requestWrite) {
  std::lock_guard<std::mutex> lock(m_mtxWriteRequests);

  if (is_connected()) {
    m_ptrIOService->set_wr_callback(m_tcpSocket,
        std::bind(&tcp_client::on_write_available, this, std::placeholders::_1));
    m_queWriteRequests.push(requestWrite);
  } else {
    __TACOPIE_THROW(warn, "tcp_client is disconnected");
  }
}

//!
//! socket getter
//!

tacopie::tcp_socket&
tcp_client::get_socket(void) {
  return m_tcpSocket;
}

const tacopie::tcp_socket&
tcp_client::get_socket(void) const {
  return m_tcpSocket;
}

//!
//! io_service getter
//!
const std::shared_ptr<tacopie::io_service>&
tcp_client::get_io_service(void) const {
  return m_ptrIOService;
}

//!
//! set on disconnection handler
//!

void
tcp_client::set_on_disconnection_handler(const disconnection_handler_t& handlerDisconnection) {
  m_handlerDisconnection = handlerDisconnection;
}

//!
//! returns whether the client is currently running or not
//!

bool
tcp_client::is_connected(void) const {
  return m_bIsConnected_a;
}

//!
//! comparison operator
//!
bool
tcp_client::operator==(const tcp_client& rhs) const {
  return m_tcpSocket == rhs.m_tcpSocket;
}

bool
tcp_client::operator!=(const tcp_client& rhs) const {
  return !operator==(rhs);
}

} // namespace tacopie
