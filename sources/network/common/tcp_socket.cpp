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

#ifdef _WIN32
#ifdef __GNUC__
#   include <Ws2tcpip.h>       // Mingw / gcc on windows
   #define _WIN32_WINNT 0x0501
   #include <winsock2.h>
   #   include <Ws2tcpip.h>
   extern "C" {
   WINSOCK_API_LINKAGE  INT WSAAPI inet_pton(INT Family, PCSTR pszAddrString, PVOID pAddrBuf);
   WINSOCK_API_LINKAGE  PCSTR WSAAPI inet_ntop(INT  Family, PVOID pAddr, PSTR pStringBuf, size_t StringBufSize);
   }

 #else
   // Windows...
   #include <winsock2.h>
   #include <In6addr.h>
   #include <Ws2tcpip.h>
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif /* _WIN32 */

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif /* SOCKET_ERROR */

#if _WIN32
#define __TACOPIE_LENGTH(size) static_cast<int>(size) // for Windows, convert buffer size to `int`
#pragma warning(disable : 4996)                // for Windows, `inet_ntoa` is deprecated as it does not support IPv6
#else
#define __TACOPIE_LENGTH(size) size // for Unix, keep buffer size as `size_t`
#endif                              /* _WIN32 */

namespace tacopie {

//!
//! ctor & dtor
//!

tcp_socket::tcp_socket(void)
: m_fd(__TACOPIE_INVALID_FD)
, m_sHost("")
, m_uPort(0)
, m_eType(type::UNKNOWN) { __TACOPIE_LOG(debug, "create tcp_socket"); }

//!
//! custom ctor
//! build socket from existing file descriptor
//!

tcp_socket::tcp_socket(fd_t fd, const std::string& sHost, std::uint32_t uPort, type eType)
: m_fd(fd)
, m_sHost(sHost)
, m_uPort(uPort)
, m_eType(eType) { __TACOPIE_LOG(debug, "create tcp_socket"); }

//!
//! Move constructor
//!

tcp_socket::tcp_socket(tcp_socket&& socket)
: m_fd(std::move(socket.m_fd))
, m_sHost(socket.m_sHost)
, m_uPort(socket.m_uPort)
, m_eType(socket.m_eType) {
  socket.m_fd       = __TACOPIE_INVALID_FD;
  socket.m_eType    = type::UNKNOWN;

  __TACOPIE_LOG(debug, "moved tcp_socket");
}

//!
//! client socket operations
//!

std::vector<char>
tcp_socket::recv(std::size_t uSizeToRead) {
  create_socket_if_necessary();
  check_or_set_type(type::CLIENT);

  std::vector<char> vctData(uSizeToRead, 0);

  ssize_t uReadSize = ::recv(m_fd, const_cast<char*>(vctData.data()), __TACOPIE_LENGTH(uSizeToRead), 0);

  if (uReadSize == SOCKET_ERROR) { __TACOPIE_THROW(error, "recv() failure"); }

  if (uReadSize == 0) { __TACOPIE_THROW(warn, "nothing to read, socket has been closed by remote host"); }

  vctData.resize(uReadSize);

  return vctData;
}

std::size_t
tcp_socket::send(const std::vector<char>& vctData, std::size_t uSizeToWrite) {
  create_socket_if_necessary();
  check_or_set_type(type::CLIENT);

  ssize_t uWriteSize = ::send(m_fd, vctData.data(), __TACOPIE_LENGTH(uSizeToWrite), 0);

  if (uWriteSize == SOCKET_ERROR) { __TACOPIE_THROW(error, "send() failure"); }

  return uWriteSize;
}

//!
//! server socket operations
//!

void
tcp_socket::listen(std::size_t uMaxConnectionQueue) {
  create_socket_if_necessary();
  check_or_set_type(type::SERVER);

  if (::listen(m_fd, __TACOPIE_LENGTH(uMaxConnectionQueue)) == SOCKET_ERROR) {
      __TACOPIE_THROW(debug, "listen() failure");
  }
}

tcp_socket
tcp_socket::accept(void) {
  create_socket_if_necessary();
  check_or_set_type(type::SERVER);

  sockaddr_storage sockAddrStorage;
  socklen_t nAddrLen = sizeof(sockAddrStorage);

  fd_t fdClient = ::accept(m_fd, reinterpret_cast<sockaddr*>(&sockAddrStorage), &nAddrLen);

  if (fdClient == __TACOPIE_INVALID_FD) { __TACOPIE_THROW(error, "accept() failure"); }

  //! now determine host and port based on socket type
  std::string       sAddr;
  std::uint32_t     uPort;

  //! ipv6
  if (sockAddrStorage.ss_family == AF_INET6) {
    sockaddr_in6* pSockAddr6 = reinterpret_cast<sockaddr_in6*>(&sockAddrStorage);
    char arrBuf[INET6_ADDRSTRLEN]   = {};
    const char* pAddr   = ::inet_ntop(sockAddrStorage.ss_family, &pSockAddr6->sin6_addr, arrBuf, INET6_ADDRSTRLEN);

    if (pAddr) {
      sAddr = std::string("[") + pAddr + "]";
    }

    uPort = ntohs(pSockAddr6->sin6_port);
  } else {//! ipv4
    sockaddr_in* pSockAddr4 = reinterpret_cast<sockaddr_in*>(&sockAddrStorage);
    char arrBuf[INET_ADDRSTRLEN] = {};
    const char* pAddr   = ::inet_ntop(sockAddrStorage.ss_family, &pSockAddr4->sin_addr, arrBuf, INET_ADDRSTRLEN);

    if (pAddr) {
      sAddr = pAddr;
    }

    uPort = ntohs(pSockAddr4->sin_port);
  }
  return {fdClient, sAddr, uPort, type::CLIENT};
}

//!
//! check whether the current socket has an appropriate type for that kind of operation
//! if current type is UNKNOWN, update internal type with given type
//!

void
tcp_socket::check_or_set_type(type eType) {
  if (m_eType != type::UNKNOWN && m_eType != eType) {
      __TACOPIE_THROW(error, "trying to perform invalid operation on socket");
  }

  m_eType = eType;
}

//!
//! get socket name information
//!

const std::string&
tcp_socket::get_host(void) const {
  return m_sHost;
}

std::uint32_t
tcp_socket::get_port(void) const {
  return m_uPort;
}

//!
//! get socket type
//!

tcp_socket::type
tcp_socket::get_type(void) const {
  return m_eType;
}

//!
//! set type, should be used if some operations determining socket type
//! have been done on the behalf of the tcp_socket instance
//!

void
tcp_socket::set_type(type eType) {
  m_eType = eType;
}

//!
//! direct access to the underlying fd
//!

fd_t
tcp_socket::get_fd(void) const {
  return m_fd;
}

//!
//! ipv6 checking
//!

bool
tcp_socket::is_ipv6(void) const {
  return m_sHost.find(':') != std::string::npos;
}

//!
//! comparison operator
//!
bool
tcp_socket::operator==(const tcp_socket& rhs) const {
  return m_fd == rhs.m_fd && m_eType == rhs.m_eType;
}

bool
tcp_socket::operator!=(const tcp_socket& rhs) const {
  return !operator==(rhs);
}

} // namespace tacopie
