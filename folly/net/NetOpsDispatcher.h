/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>

#include <folly/net/NetOps.h>

namespace folly {
namespace netops {

/**
 * Dispatcher for netops:: calls.
 *
 * Using a Dispatcher instead of calling netops:: directly enables tests to
 * mock netops:: calls.
 */
class Dispatcher {
 public:
  /// Returns the default Dispatcher instance.
  ///
  /// \returns The process-wide default Dispatcher.
  static Dispatcher* getDefaultInstance();

  /// Accepts a connection on a listening socket.
  ///
  /// \param s The listening socket.
  /// \param addr Buffer that receives the peer address.
  /// \param addrlen In/out length of the address buffer.
  /// \returns The accepted socket, or an invalid socket on error.
  virtual NetworkSocket accept(
      NetworkSocket s, sockaddr* addr, socklen_t* addrlen);
  /// Binds a socket to a local address.
  ///
  /// \param s The socket to bind.
  /// \param name The local address to bind to.
  /// \param namelen The length of the address.
  /// \returns 0 on success, or -1 on error.
  virtual int bind(NetworkSocket s, const sockaddr* name, socklen_t namelen);
  /// Closes a socket.
  ///
  /// \param s The socket to close.
  /// \returns 0 on success, or -1 on error.
  virtual int close(NetworkSocket s);
  /// Connects a socket to a remote address.
  ///
  /// \param s The socket to connect.
  /// \param name The remote address to connect to.
  /// \param namelen The length of the address.
  /// \returns 0 on success, or -1 on error.
  virtual int connect(NetworkSocket s, const sockaddr* name, socklen_t namelen);
  /// Retrieves the address of the connected peer.
  ///
  /// \param s The socket to query.
  /// \param name Buffer that receives the peer address.
  /// \param namelen In/out length of the address buffer.
  /// \returns 0 on success, or -1 on error.
  virtual int getpeername(NetworkSocket s, sockaddr* name, socklen_t* namelen);
  /// Retrieves the local address bound to a socket.
  ///
  /// \param s The socket to query.
  /// \param name Buffer that receives the local address.
  /// \param namelen In/out length of the address buffer.
  /// \returns 0 on success, or -1 on error.
  virtual int getsockname(NetworkSocket s, sockaddr* name, socklen_t* namelen);
  /// Retrieves a socket option.
  ///
  /// \param s The socket to query.
  /// \param level The protocol level of the option.
  /// \param optname The option name.
  /// \param optval Buffer that receives the option value.
  /// \param optlen In/out length of the value buffer.
  /// \returns 0 on success, or -1 on error.
  virtual int getsockopt(
      NetworkSocket s, int level, int optname, void* optval, socklen_t* optlen);
  /// Converts an IPv4 dotted-decimal string to a network address.
  ///
  /// \param cp The address string.
  /// \param inp Buffer that receives the converted address.
  /// \returns Nonzero on success, or 0 on error.
  virtual int inet_aton(const char* cp, in_addr* inp);
  /// Marks a socket as accepting connections.
  ///
  /// \param s The socket to listen on.
  /// \param backlog The maximum length of the pending connection queue.
  /// \returns 0 on success, or -1 on error.
  virtual int listen(NetworkSocket s, int backlog);
  /// Waits for events on a set of poll descriptors.
  ///
  /// \param fds The array of poll descriptors.
  /// \param nfds The number of descriptors in the array.
  /// \param timeout The timeout in milliseconds.
  /// \returns The number of ready descriptors, 0 on timeout, or -1 on error.
  virtual int poll(PollDescriptor fds[], nfds_t nfds, int timeout);
  /// Receives data from a connected socket.
  ///
  /// \param s The socket to receive from.
  /// \param buf The buffer that receives the data.
  /// \param len The size of the buffer.
  /// \param flags Receive flags.
  /// \returns The number of bytes received, or -1 on error.
  virtual ssize_t recv(NetworkSocket s, void* buf, size_t len, int flags);
  /// Receives data and records the source address.
  ///
  /// \param s The socket to receive from.
  /// \param buf The buffer that receives the data.
  /// \param len The size of the buffer.
  /// \param flags Receive flags.
  /// \param from Buffer that receives the source address.
  /// \param fromlen In/out length of the source address buffer.
  /// \returns The number of bytes received, or -1 on error.
  virtual ssize_t recvfrom(
      NetworkSocket s,
      void* buf,
      size_t len,
      int flags,
      sockaddr* from,
      socklen_t* fromlen);
  /// Receives a message, including ancillary data.
  ///
  /// \param s The socket to receive from.
  /// \param message The message header to fill.
  /// \param flags Receive flags.
  /// \returns The number of bytes received, or -1 on error.
  virtual ssize_t recvmsg(NetworkSocket s, msghdr* message, int flags);
  /// Receives multiple messages in a single call.
  ///
  /// \param s The socket to receive from.
  /// \param msgvec The array of message headers to fill.
  /// \param vlen The number of entries in the array.
  /// \param flags Receive flags.
  /// \param timeout The timeout, or null to block.
  /// \returns The number of messages received, or -1 on error.
  virtual int recvmmsg(
      NetworkSocket s,
      mmsghdr* msgvec,
      unsigned int vlen,
      unsigned int flags,
      timespec* timeout);
  /// Sends data on a connected socket.
  ///
  /// \param s The socket to send on.
  /// \param buf The data to send.
  /// \param len The number of bytes to send.
  /// \param flags Send flags.
  /// \returns The number of bytes sent, or -1 on error.
  virtual ssize_t send(NetworkSocket s, const void* buf, size_t len, int flags);
  /// Sends data to a specific destination address.
  ///
  /// \param s The socket to send on.
  /// \param buf The data to send.
  /// \param len The number of bytes to send.
  /// \param flags Send flags.
  /// \param to The destination address.
  /// \param tolen The length of the destination address.
  /// \returns The number of bytes sent, or -1 on error.
  virtual ssize_t sendto(
      NetworkSocket s,
      const void* buf,
      size_t len,
      int flags,
      const sockaddr* to,
      socklen_t tolen);
  /// Sends a message, including ancillary data.
  ///
  /// \param socket The socket to send on.
  /// \param message The message to send.
  /// \param flags Send flags.
  /// \returns The number of bytes sent, or -1 on error.
  virtual ssize_t sendmsg(
      NetworkSocket socket, const msghdr* message, int flags);
  /// Sends multiple messages in a single call.
  ///
  /// \param socket The socket to send on.
  /// \param msgvec The array of messages to send.
  /// \param vlen The number of entries in the array.
  /// \param flags Send flags.
  /// \returns The number of messages sent, or -1 on error.
  virtual int sendmmsg(
      NetworkSocket socket, mmsghdr* msgvec, unsigned int vlen, int flags);
  /// Sets a socket option.
  ///
  /// \param s The socket to modify.
  /// \param level The protocol level of the option.
  /// \param optname The option name.
  /// \param optval The option value to set.
  /// \param optlen The length of the value.
  /// \returns 0 on success, or -1 on error.
  virtual int setsockopt(
      NetworkSocket s,
      int level,
      int optname,
      const void* optval,
      socklen_t optlen);
  /// Shuts down part or all of a full-duplex connection.
  ///
  /// \param s The socket to shut down.
  /// \param how Which directions to shut down.
  /// \returns 0 on success, or -1 on error.
  virtual int shutdown(NetworkSocket s, int how);
  /// Creates a socket.
  ///
  /// \param af The address family.
  /// \param type The socket type.
  /// \param protocol The protocol.
  /// \returns The new socket, or an invalid socket on error.
  virtual NetworkSocket socket(int af, int type, int protocol);
  /// Creates a connected pair of sockets.
  ///
  /// \param domain The communication domain.
  /// \param type The socket type.
  /// \param protocol The protocol.
  /// \param sv Array that receives the two connected sockets.
  /// \returns 0 on success, or -1 on error.
  virtual int socketpair(
      int domain, int type, int protocol, NetworkSocket sv[2]);

  /// Puts a socket into non-blocking mode.
  ///
  /// \param s The socket to modify.
  /// \returns 0 on success, or -1 on error.
  virtual int set_socket_non_blocking(NetworkSocket s);
  /// Puts a socket into blocking mode.
  ///
  /// \param s The socket to modify.
  /// \returns 0 on success, or -1 on error.
  virtual int set_socket_blocking(NetworkSocket s);
  /// Sets the close-on-exec flag on a socket.
  ///
  /// \param s The socket to modify.
  /// \returns 0 on success, or -1 on error.
  virtual int set_socket_close_on_exec(NetworkSocket s);

 protected:
  /// Default-constructs a Dispatcher.
  Dispatcher() = default;
  /// Destroys the Dispatcher.
  virtual ~Dispatcher() = default;
};

/**
 * Container for netops::Dispatcher.
 *
 * Enables override Dispatcher to be installed for tests and special cases.
 * If no override installed, returns default Dispatcher instance.
 */
class DispatcherContainer {
 public:
  /**
   * Returns Dispatcher.
   *
   * If no override installed, returns default Dispatcher instance.
   *
   * @return The active Dispatcher.
   */
  netops::Dispatcher* getDispatcher() const {
    return overrideDispatcher_
        ? overrideDispatcher_.get()
        : Dispatcher::getDefaultInstance();
  }

  /**
   * Returns Dispatcher.
   *
   * If no override installed, returns default Dispatcher instance.
   *
   * @return The active Dispatcher.
   */
  netops::Dispatcher* operator->() const { return getDispatcher(); }

  /**
   * Sets override Dispatcher. To remove override, pass empty shared_ptr.
   *
   * @param dispatcher The override dispatcher, or an empty ptr to clear it.
   */
  void setOverride(std::shared_ptr<netops::Dispatcher> dispatcher) {
    overrideDispatcher_ = std::move(dispatcher);
  }

  /**
   * If installed, returns shared_ptr to override Dispatcher, else empty ptr.
   *
   * @return The override Dispatcher, or an empty ptr if none is set.
   */
  std::shared_ptr<netops::Dispatcher> getOverride() const {
    return overrideDispatcher_;
  }

 private:
  std::shared_ptr<netops::Dispatcher> overrideDispatcher_;
};

} // namespace netops
} // namespace folly
