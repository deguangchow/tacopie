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

//! guard for bulk content integration depending on how user integrates the library
#ifdef _WIN32

//! force link with ws2_32.lib
//! some user of the lib forgot to link with it #34
#pragma comment(lib, "ws2_32.lib")

#include <tacopie/network/tcp_server.hpp>
#include <tacopie/utils/error.hpp>
#include <tacopie/utils/logger.hpp>
#include <tacopie/utils/typedefs.hpp>

#include <cstring>

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

namespace tacopie {

void
tcp_socket::connect(const std::string& sHost, std::uint32_t uPort, std::uint32_t uTimeoutMsecs) {
  //! Reset host and port
  m_sHost = sHost;
  m_uPort = uPort;

  create_socket_if_necessary();
  check_or_set_type(type::CLIENT);

  sockaddr_storage  sockAddrStorage;
  socklen_t         nAddrLen;

  //! 0-init addr info struct
  std::memset(&sockAddrStorage, 0, sizeof(sockAddrStorage));

  if (is_ipv6()) {
    //! init sockaddr_in6 struct
    sockaddr_in6* pSockAddr6 = reinterpret_cast<sockaddr_in6*>(&sockAddrStorage);
    //! convert addr
    if (::inet_pton(AF_INET6, sHost.data(), &pSockAddr6->sin6_addr) < 0) {
      __TACOPIE_THROW(error, "inet_pton() failure");
    }
    //! remaining fields
    sockAddrStorage.ss_family   = AF_INET6;
    pSockAddr6->sin6_port       = htons(uPort);
    nAddrLen                    = sizeof(*pSockAddr6);
  } else {
    addrinfo*   pAddrInfoResult = nullptr;
    addrinfo    addrInfo;

    memset(&addrInfo, 0, sizeof(addrInfo));
    addrInfo.ai_socktype = SOCK_STREAM;
    addrInfo.ai_family   = AF_INET;

    //! resolve DNS
    if (getaddrinfo(sHost.c_str(), nullptr, &addrInfo, &pAddrInfoResult) != 0) {
        __TACOPIE_THROW(error, "getaddrinfo() failure");
    }

    //! init sockaddr_in struct
    sockaddr_in* pSockAddr4 = reinterpret_cast<sockaddr_in*>(&sockAddrStorage);
    //! host
    pSockAddr4->sin_addr = (reinterpret_cast<sockaddr_in*>(pAddrInfoResult->ai_addr))->sin_addr;
    //! Remaining fields
    pSockAddr4->sin_port        = htons(uPort);
    sockAddrStorage.ss_family   = AF_INET;
    nAddrLen                    = sizeof(*pSockAddr4);

    freeaddrinfo(pAddrInfoResult);
  }

  if (uTimeoutMsecs > 0) {
    //! for timeout connection handling:
    //!  1. set socket to non blocking
    //!  2. connect
    //!  3. poll select
    //!  4. check connection status
    u_long uMode = 1;
    if (ioctlsocket(m_fd, FIONBIO, &uMode) != 0) {
      close();
      __TACOPIE_THROW(error, "connect() set non-blocking failure");
    }
  } else {
    //! For no timeout case, still make sure that the socket is in blocking mode
    //! As reported in #32, this might not be the case on some OS
    u_long uMode = 0;
    if (ioctlsocket(m_fd, FIONBIO, &uMode) != 0) {
      close();
      __TACOPIE_THROW(error, "connect() set blocking failure");
    }
  }

  int nReturn = ::connect(m_fd, reinterpret_cast<const sockaddr*>(&sockAddrStorage), nAddrLen);
  if (nReturn == -1 && WSAGetLastError() != WSAEWOULDBLOCK) {
    close();
    __TACOPIE_THROW(error, "connect() failure");
  }

  if (uTimeoutMsecs > 0) {
    timeval timeVal;
    timeVal.tv_sec  = (uTimeoutMsecs / 1000);
    timeVal.tv_usec = ((uTimeoutMsecs - (timeVal.tv_sec * 1000)) * 1000);

    FD_SET fdSet;
    FD_ZERO(&fdSet);
    FD_SET(m_fd, &fdSet);

    //! 1 means we are connected.
    //! 0 means a timeout.
    if (select(static_cast<int>(m_fd) + 1, NULL, &fdSet, NULL, &timeVal) == 1) {
      //! Make sure there are no async connection errors
      int nErr = 0;
      int nLen = sizeof(nLen);
      if (getsockopt(m_fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&nErr), &nLen) == -1 || nErr != 0) {
        close();
        __TACOPIE_THROW(error, "connect() failure");
      }

      //! Set back to blocking mode as the user of this class is expecting
      u_long uMode = 0;
      if (ioctlsocket(m_fd, FIONBIO, &uMode) != 0) {
        close();
        __TACOPIE_THROW(error, "connect() set blocking failure");
      }
    } else {
      close();
      __TACOPIE_THROW(error, "connect() timed out");
    }
  }
}

//!
//! server socket operations
//!

void
tcp_socket::bind(const std::string& sHost, std::uint32_t uPort) {
  //! Reset host and port
  m_sHost = sHost;
  m_uPort = uPort;

  create_socket_if_necessary();
  check_or_set_type(type::SERVER);

  sockaddr_storage  sockAddrStorage;
  socklen_t         nAddrLen;

  //! 0-init addr info struct
  std::memset(&sockAddrStorage, 0, sizeof(sockAddrStorage));

  if (is_ipv6()) {
    //! init sockaddr_in6 struct
    sockaddr_in6*   pSockAddr6 = reinterpret_cast<sockaddr_in6*>(&sockAddrStorage);
    //! convert addr
    if (::inet_pton(AF_INET6, sHost.data(), &pSockAddr6->sin6_addr) < 0) {
      __TACOPIE_THROW(error, "inet_pton() failure");
    }
    //! remaining fields
    pSockAddr6->sin6_port       = htons(uPort);
    sockAddrStorage.ss_family   = AF_INET6;
    nAddrLen                    = sizeof(*pSockAddr6);
  } else {
    addrinfo*       pAddInfoResult = nullptr;

    //! dns resolution
    if (getaddrinfo(sHost.c_str(), nullptr, nullptr, &pAddInfoResult) != 0) {
      __TACOPIE_THROW(error, "getaddrinfo() failure");
    }

    //! init sockaddr_in struct
    sockaddr_in*    pSockAddr4 = reinterpret_cast<sockaddr_in*>(&sockAddrStorage);
    //! addr
    pSockAddr4->sin_addr = (reinterpret_cast<sockaddr_in*>(pAddInfoResult->ai_addr))->sin_addr;
    //! remaining fields
    pSockAddr4->sin_port        = htons(uPort);
    sockAddrStorage.ss_family   = AF_INET;
    nAddrLen                    = sizeof(*pSockAddr4);

    freeaddrinfo(pAddInfoResult);
  }

  if (::bind(m_fd, reinterpret_cast<const sockaddr*>(&sockAddrStorage), nAddrLen) == SOCKET_ERROR) {
      __TACOPIE_THROW(error, "bind() failure");
  }
}

//!
//! general socket operations
//!

void
tcp_socket::close(void) {
  if (m_fd != __TACOPIE_INVALID_FD) {
    __TACOPIE_LOG(debug, "close socket");
    closesocket(m_fd);
  }

  m_fd      = __TACOPIE_INVALID_FD;
  m_eType   = type::UNKNOWN;
}
//!
//! create a new socket if no socket has been initialized yet
//!

void
tcp_socket::create_socket_if_necessary(void) {
  if (m_fd != __TACOPIE_INVALID_FD) { return; }

  //! new TCP socket
  //! handle ipv6 addr
  short family;
  if (is_ipv6()) {
    family = AF_INET6;
  } else {
    family = AF_INET;
  }
  m_fd   = socket(family, SOCK_STREAM, 0);
  m_eType = type::UNKNOWN;

  if (m_fd == __TACOPIE_INVALID_FD) {
      __TACOPIE_THROW(error, "tcp_socket::create_socket_if_necessary: socket() failure");
  }
}

} // namespace tacopie

#endif /* _WIN32 */
