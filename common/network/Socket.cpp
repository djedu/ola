/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Socket.cpp
 * Implementation of the Socket classes
 * Copyright (C) 2005-2009 Simon Newton
 */

#include <arpa/inet.h>
#include <errno.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <lla/Logging.h>
#include <lla/network/Socket.h>

namespace lla {
namespace network {

// ConnectedSocket
// ------------------------------------------------


/*
 * Turn on non-blocking reads.
 * @param fd the descriptor to enable non-blocking on
 * @return true if it worked, false otherwise
 */
bool ConnectedSocket::SetNonBlocking(int fd) {
  if (fd == INVALID_SOCKET)
    return false;

  int val = fcntl(fd, F_GETFL, 0);
  int ret = fcntl(fd, F_SETFL, val | O_NONBLOCK);
  if (ret) {
    LLA_WARN << "failed to set " << fd << " non-blocking: " << strerror(errno);
    return false;
  }
  return true;
}


/*
 * Find out how much data is left to read
 * @return the amount of unread data for the socket
 */
int ConnectedSocket::DataRemaining(int fd) const {
  int unread;
  if (fd == INVALID_SOCKET)
    return 0;

  if (ioctl(fd, FIONREAD, &unread) < 0) {
    LLA_WARN << "ioctl error for " << fd << ", " << strerror(errno);
    return 0;
  }
  return unread;
}


/*
 * Write data to this socket.
 * @param fd the descriptor to write to
 * @param buffer the data to write
 * @param size the length of the data
 * @return the number of bytes sent
 */
ssize_t ConnectedSocket::FDSend(int fd,
                                const uint8_t *buffer,
                                unsigned int size) {
  if (fd == INVALID_SOCKET)
    return 0;

  ssize_t bytes_sent = write(fd, buffer, size);
  if (bytes_sent != size)
    LLA_WARN << "Failed to send, " << strerror(errno);
  return bytes_sent;
}


/*
 * Read data from this socket.
 * @param fd the descriptor to read from
 * @param buffer a pointer to the buffer to store new data in
 * @param size the size of the buffer
 * @param data_read a value result argument which returns the amount of data
 * copied into the buffer
 * @returns -1 on error, 0 on success.
 */
int ConnectedSocket::FDReceive(int fd,
                               uint8_t *buffer,
                               unsigned int size,
                               unsigned int &data_read) {
  int ret;
  uint8_t *data = buffer;
  data_read = 0;
  if (fd == INVALID_SOCKET)
    return -1;

  while (data_read < size) {
    if ((ret = read(fd, data, size - data_read)) < 0) {
      if (errno == EAGAIN)
        return 0;
      if (errno != EINTR) {
        LLA_WARN << "read failed, " << strerror(errno);
        return -1;
      }
    } else if (ret == 0) {
      return 0;
    }
    data_read += ret;
    data += data_read;
  }
  return 0;
}



/*
 * Close this socket
 * @return true if close succeeded, false otherwise
bool ReceivingSocket::Close() {
  bool ret = true;
  if (m_read_fd != INVALID_SOCKET) {
    if (close(m_read_fd)) {
      LLA_WARN << "close() failed, " << strerror(errno);
      ret = false;
    }
    m_read_fd = INVALID_SOCKET;
  }
  return ret;
}


/*
 * Check if this socket has been closed (this is a bit of a lie)
 * @return true if the socket is closed, false otherwise
bool ReceivingSocket::IsClosed() const {
  if (m_read_fd == INVALID_SOCKET)
    return true;

  return UnreadData() == 0;
}
 */


// LoopbackSocket
// ------------------------------------------------


/*
 * Setup this loopback socket
 */
bool LoopbackSocket::Init() {
  if (m_fd_pair[0] != INVALID_SOCKET || m_fd_pair[1] != INVALID_SOCKET)
    return false;

  if (pipe(m_fd_pair) < 0) {
    LLA_WARN << "pipe() failed, " << strerror(errno);
    return false;
  }

  SetReadNonBlocking();
  return true;
}


/*
 * Close the loopback socket
 * @return true if close succeeded, false otherwise
 */
bool LoopbackSocket::Close() {
  if (m_fd_pair[0] != INVALID_SOCKET)
    close(m_fd_pair[0]);

  if (m_fd_pair[1] != INVALID_SOCKET)
    close(m_fd_pair[1]);

  m_fd_pair[0] = INVALID_SOCKET;
  m_fd_pair[1] = INVALID_SOCKET;
  return true;
}


// PipeSocket
// ------------------------------------------------

/*
 * Create a new pipe socket
 */
bool PipeSocket::Init() {
  if (m_in_pair[0] != INVALID_SOCKET || m_out_pair[1] != INVALID_SOCKET)
    return false;

  if (pipe(m_in_pair) < 0) {
    LLA_WARN << "pipe() failed, " << strerror(errno);
    return false;
  }

  if (pipe(m_out_pair) < 0) {
    LLA_WARN << "pipe() failed, " << strerror(errno);
    close(m_in_pair[0]);
    close(m_in_pair[1]);
    m_in_pair[0] = m_in_pair[1] = INVALID_SOCKET;
    return false;
  }

  SetReadNonBlocking();
  return true;
}


/*
 * Fetch the other end of the pipe socket. The caller now owns the new
 * PipeSocket.
 * @returns NULL if the socket wasn't initialized correctly.
 */
PipeSocket *PipeSocket::OppositeEnd() {
  if (m_in_pair[0] == INVALID_SOCKET || m_out_pair[1] == INVALID_SOCKET)
    return NULL;

  if (!m_other_end) {
    m_other_end = new PipeSocket(m_out_pair, m_in_pair, this);
    m_other_end->SetReadNonBlocking();
  }
  return m_other_end;
}


/*
 * Close this PipeSocket
 */
bool PipeSocket::Close() {
  if (m_in_pair[0] != INVALID_SOCKET)
    close(m_in_pair[0]);

  if (m_out_pair[1] != INVALID_SOCKET)
    close(m_out_pair[1]);

  m_in_pair[0] = INVALID_SOCKET;
  m_out_pair[1] = INVALID_SOCKET;
  return true;
}


// TcpSocket
// ------------------------------------------------

/*
 * Connect
 * @param ip_address the IP to connect to
 * @param port the port to connect to
 */
TcpSocket* TcpSocket::Connect(const std::string &ip_address,
                              unsigned short port) {

  struct sockaddr_in server_address;
  socklen_t length = sizeof(server_address);

  int sd = socket(AF_INET, SOCK_STREAM, 0);
  if (sd < 0) {
    LLA_WARN << "socket() failed, " << strerror(errno);
    return NULL;
  }

  // setup
  memset(&server_address, 0x00, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  if (!inet_aton(ip_address.data(), &server_address.sin_addr)) {
    LLA_WARN << "Failed to convert ip " << ip_address << " " <<
      strerror(errno);
    close(sd);
    return NULL;
  }

  if (connect(sd, (struct sockaddr*) &server_address, length)) {
    LLA_WARN << "connect to " << ip_address << ":" << port << " failed, "
      << strerror(errno);
    return false;
  }
  TcpSocket *socket = new TcpSocket(sd);
  socket->SetReadNonBlocking();
  return socket;
}


/*
 * Close this TcpSocket
 */
bool TcpSocket::Close() {
  if (m_sd != INVALID_SOCKET) {
    close(m_sd);
    m_sd = INVALID_SOCKET;
  }
  return true;
}


// DeviceSocket
// ------------------------------------------------

bool DeviceSocket::Close() {
  int ret = close(m_fd);
  m_fd = INVALID_SOCKET;
  return ret == 0;
}


// UdpSocket
// ------------------------------------------------

/*
 * Start listening
 * @return true if it succeeded, false otherwise
 */
bool UdpSocket::Init() {
  if (m_fd != INVALID_SOCKET)
    return false;

  int sd = socket(PF_INET, SOCK_DGRAM, 0);

  if (sd < 0) {
    LLA_WARN << "Could not create socket " << strerror(errno);
    return false;
  }

  m_fd = sd;
  return true;
}


/*
 * Bind this socket to an external address/port
 */
bool UdpSocket::Bind(unsigned short port) {
  if (m_fd == INVALID_SOCKET)
    return false;

  struct sockaddr_in servAddr;
  memset(&servAddr, 0x00, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port = htons(port);
  servAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  LLA_DEBUG << "Binding to " << inet_ntoa(servAddr.sin_addr) << ":" << port;

  if (bind(m_fd, (struct sockaddr*) &servAddr, sizeof(servAddr)) == -1) {
    LLA_INFO << "Failed to bind socket " << strerror(errno);
    return false;
  }
  m_bound_to_port = true;
  return true;
}


/*
 * Close this socket
 */
bool UdpSocket::Close() {
  if (m_fd == INVALID_SOCKET)
    return false;

  m_bound_to_port = false;
  int ret = true;
  if (close(m_fd)) {
    LLA_WARN << "close() failed, " << strerror(errno);
    ret = false;
  }
  m_fd = INVALID_SOCKET;
  return true;
}


/*
 * Send data.
 * @param buffer the data to send
 * @param size the length of the data
 * @param destination to destination to sent to
 * @return the number of bytes sent
 */
ssize_t UdpSocket::SendTo(const uint8_t *buffer,
                          unsigned int size,
                          const struct sockaddr_in &destination) const {

  ssize_t bytes_sent = sendto(m_fd, buffer, size, 0,
                              (struct sockaddr*) &destination,
                              sizeof(struct sockaddr));
  if (bytes_sent != size)
    LLA_WARN << "Failed to send, " << strerror(errno);
  return bytes_sent;
}


/*
 * Send data
 * @param buffer the data to send
 * @param size the length of the data
 * @param ip_address the IP to send to
 * @param port the port to send to
 * @return the number of bytes sent
 */
ssize_t UdpSocket::SendTo(const uint8_t *buffer,
                          unsigned int size,
                          const std::string &ip_address,
                          unsigned short port) const {

  struct sockaddr_in destination;
  memset(&destination, 0x00, sizeof(destination));
  destination.sin_family = AF_INET;
  destination.sin_port = htons(port);

  if (!inet_aton(ip_address.data(), &destination.sin_addr)) {
    LLA_WARN << "Failed to convert ip " << ip_address << " " <<
      strerror(errno);
    return 0;
  }
  return SendTo(buffer, size, destination);
}


/*
 * Receive data
 * @param buffer the buffer to store the data
 * @param data_read the size of the buffer, updated with the number of bytes
 * read
 * @return true or false
 */
bool UdpSocket::RecvFrom(uint8_t *buffer,
                         ssize_t &data_read,
                         struct sockaddr_in &source,
                         socklen_t &src_size) const {

  data_read = recvfrom(m_fd, buffer, data_read, 0,
                       (struct sockaddr*) &source,
                       &src_size);
  return data_read >= 0;
}


/*
 * Enable broadcasting for this socket.
 * @return true if it worked, false otherwise
 */
bool UdpSocket::EnableBroadcast() {
  if (m_fd == INVALID_SOCKET)
    return false;

  int broadcast_flag = 1;
  int ret = setsockopt(m_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_flag,
                       sizeof(int));

  if (setsockopt(m_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_flag, sizeof(int))
      == -1) {
    LLA_WARN << "Failed to enabled broadcasting: " << strerror(errno);
    return false;
  }
  return true;
}


// TcpAcceptingSocket
// ------------------------------------------------

/*
 * Create a new TcpListeningSocket
 * @param address the address to listen on
 * @param port the port to listen on
 * @param backlog the backlog
 */
TcpAcceptingSocket::TcpAcceptingSocket(const std::string &address,
                                       unsigned short port,
                                       int backlog):
    AcceptingSocket(),
    m_address(address),
    m_port(port),
    m_sd(INVALID_SOCKET),
    m_backlog(backlog) {
}


/*
 * Start listening
 * @return true if it succeeded, false otherwise
 */
bool TcpAcceptingSocket::Listen() {
  struct sockaddr_in server_address;
  int reuse_flag = 1;

  if (m_sd != INVALID_SOCKET)
    return false;

  // setup
  memset(&server_address, 0x00, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(m_port);

  if (!inet_aton(m_address.data(), &server_address.sin_addr)) {
    LLA_WARN << "Failed to convert ip " << m_address << " " <<
      strerror(errno);
    return false;
  }

  int sd = socket(AF_INET, SOCK_STREAM, 0);
  if (sd < 0) {
    LLA_WARN << "socket() failed: " << strerror(errno);
    return false;
  }

  if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &reuse_flag,
                 sizeof(reuse_flag))) {
    LLA_WARN << "can't set reuse for " << sd << ", " << strerror(errno);
    close(sd);
    return false;
  }

  if (bind(sd, (struct sockaddr *) &server_address,
           sizeof(server_address)) == -1) {
    LLA_WARN << "bind to " << m_address << ":" << m_port << " failed, "
      << strerror(errno);
    close(sd);
    return false;
  }

  if (listen(sd, m_backlog)) {
    LLA_WARN << "listen on " << m_address << ":" << m_port << " failed, "
      << strerror(errno);
    return false;
  }
  m_sd = sd;
  return true;
}


/*
 * Stop listening & close this socket
 * @return true if close succeeded, false otherwise
 */
bool TcpAcceptingSocket::Close() {
  bool ret = true;
  if (m_sd != INVALID_SOCKET)
    if (close(m_sd)) {
      LLA_WARN << "close() failed " << strerror(errno);
      ret = false;
    }
  m_sd = INVALID_SOCKET;
  return ret;
}


/*
 * Check if this socket has been closed (this is a bit of a lie)
 * @return true if the socket is closed, false otherwise
 */
bool TcpAcceptingSocket::IsClosed() const {
  return m_sd == INVALID_SOCKET;
}


/*
 * Accept new connections
 * @return a new connected socket
 */
ConnectedSocket *TcpAcceptingSocket::Accept() {
  struct sockaddr_in cli_address;
  socklen_t length = sizeof(cli_address);

  if (m_sd == INVALID_SOCKET)
    return NULL;

  int sd = accept(m_sd, (struct sockaddr*) &cli_address, &length);
  if (sd < 0) {
    LLA_WARN << "accept() failed, " << strerror(errno);
    return NULL;
  }

  TcpSocket *socket = new TcpSocket(sd);
  socket->SetReadNonBlocking();
  return socket;
}

} // network
} //lla
