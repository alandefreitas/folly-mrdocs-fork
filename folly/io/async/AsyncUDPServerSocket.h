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

#include <folly/Memory.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/io/async/EventBase.h>

/// Core Folly library types and utilities.
namespace folly {

/**
 * UDP server socket
 *
 * It wraps a UDP socket waiting for packets and distributes them among
 * a set of event loops in round robin fashion.
 *
 * NOTE: At the moment it is designed to work with single packet protocols
 *       in mind. We distribute incoming packets among all the listeners in
 *       round-robin fashion. So, any protocol that expects to send/recv
 *       more than 1 packet will not work because they will end up with
 *       different event base to process.
 */
class AsyncUDPServerSocket
    : private AsyncUDPSocket::ReadCallback,
      public AsyncSocketBase {
 public:
  /// Receives lifecycle and data events from the server socket.
  class Callback {
   public:
    /// Extra parameters delivered alongside each received datagram.
    using OnDataAvailableParams =
        AsyncUDPSocket::ReadCallback::OnDataAvailableParams;
    /**
     * Invoked when we start reading data from socket. It is invoked in
     * each acceptors/listeners event base thread.
     */
    virtual void onListenStarted() noexcept = 0;

    /**
     * Invoked when the server socket is closed. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onListenStopped() noexcept = 0;

    /**
     * Invoked when the server socket is paused. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onListenPaused() noexcept {}

    /**
     * Invoked when the server socket is resumed. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onListenResumed() noexcept {}

    /**
     * Invoked when the server socket can still read but need to inform the
     * callback object that it should not process read from new client address.
     * It is invoked in each acceptors/listeners event base thread.
     */
    virtual void onAcceptNewPeerPaused() noexcept {}

    /**
     * Invoked when need to inform the callback object that it can resume
     * process read from new client address. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onAcceptNewPeerResumed() noexcept {}

    /**
     * Invoked when a new packet is received
     *
     * \param socket The socket the packet arrived on.
     * \param addr The address of the peer that sent the packet.
     * \param buf The received packet payload.
     * \param truncated Whether the packet was larger than the read buffer.
     * \param params Extra parameters delivered with the packet.
     */
    virtual void onDataAvailable(
        std::shared_ptr<AsyncUDPSocket> socket,
        const folly::SocketAddress& addr,
        std::unique_ptr<folly::IOBuf> buf,
        bool truncated,
        OnDataAvailableParams params) noexcept = 0;

    /// Destroys the callback.
    virtual ~Callback() = default;
  };

  /// Strategy used to pick which listener handles each incoming packet.
  enum class DispatchMechanism {
    RoundRobin, ///< Cycle through listeners in order.
    ClientAddressHash ///< Pick a listener by hashing the client address.
  };

  /**
   * Create a new UDP server socket
   *
   * Note about packet size - We allocate buffer of packetSize_ size to read.
   * If packet are larger than this value, as per UDP protocol, remaining data
   * is dropped and you get `truncated = true` in onDataAvailable callback
   *
   * \param evb The event base that drives the server socket.
   * \param sz The read buffer size in bytes.
   * \param dm The mechanism used to dispatch packets to listeners.
   */
  explicit AsyncUDPServerSocket(
      EventBase* evb,
      size_t sz = 1500,
      DispatchMechanism dm = DispatchMechanism::RoundRobin)
      : evb_(evb), packetSize_(sz), dispatchMechanism_(dm), nextListener_(0) {}

  /// Destroys the server socket, closing it if still open.
  ~AsyncUDPServerSocket() override {
    if (socket_) {
      close();
    }
  }

  /// Binds the underlying socket to a local address.
  ///
  /// \param addy The local address to bind to.
  /// \param options Socket options applied before and after binding.
  /// \param ifName The network interface to bind to, or empty for any.
  void bind(
      const folly::SocketAddress& addy,
      const SocketOptionMap& options = emptySocketOptionMap,
      const std::string& ifName = "") {
    CHECK(!socket_);

    socket_ = std::make_shared<AsyncUDPSocket>(evb_);
    socket_->setReusePort(reusePort_);
    socket_->setReuseAddr(reuseAddr_);
    socket_->setRecvTos(recvTos_);
    socket_->applyOptions(
        validateSocketOptions(
            options, addy.getFamily(), SocketOptionKey::ApplyPos::PRE_BIND),
        SocketOptionKey::ApplyPos::PRE_BIND);
    AsyncUDPSocket::BindOptions bindOptions;
    bindOptions.ifName = ifName;
    socket_->bind(addy, bindOptions);
    socket_->applyOptions(
        validateSocketOptions(
            options, addy.getFamily(), SocketOptionKey::ApplyPos::POST_BIND),
        SocketOptionKey::ApplyPos::POST_BIND);
  }

  /// Enables or disables port reuse before binding.
  ///
  /// \param reusePort Whether to set SO_REUSEPORT.
  void setReusePort(bool reusePort) { reusePort_ = reusePort; }

  /// Enables or disables address reuse before binding.
  ///
  /// \param reuseAddr Whether to set SO_REUSEADDR.
  void setReuseAddr(bool reuseAddr) { reuseAddr_ = reuseAddr; }

  /// Enables or disables receiving the type-of-service field.
  ///
  /// \param recvTos Whether to request TOS on received packets.
  void setRecvTos(bool recvTos) { recvTos_ = recvTos; }

  /// Sets the type-of-service or traffic class on the socket.
  ///
  /// \param tosOrTclass The TOS (IPv4) or traffic class (IPv6) value.
  void setTosOrTrafficClass(uint8_t tosOrTclass) {
    CHECK(socket_);
    socket_->setTosOrTrafficClass(tosOrTclass);
  }

  /// Returns the local address the socket is bound to.
  ///
  /// \returns The bound local address.
  folly::SocketAddress address() const {
    CHECK(socket_);
    return socket_->address();
  }

  /// Writes the local address into the given output parameter.
  ///
  /// \param a Receives the bound local address.
  void getAddress(SocketAddress* a) const override { *a = address(); }

  /**
   * Add a listener to the round robin list
   *
   * \param evb The event base the listener runs on.
   * \param callback The callback that handles packets for this listener.
   */
  void addListener(EventBase* evb, Callback* callback) {
    listeners_.emplace_back(evb, callback);
  }

  /// Starts reading packets and notifies all registered listeners.
  void listen() {
    CHECK(socket_) << "Need to bind before listening";

    for (auto& listener : listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback]() mutable {
        callback->onListenStarted();
      });
    }

    socket_->resumeRead(this);
  }

  /// Returns the underlying network socket handle.
  ///
  /// \returns The bound network socket handle.
  NetworkSocket getNetworkSocket() const {
    CHECK(socket_) << "Need to bind before getting Network Socket";
    return socket_->getNetworkSocket();
  }

  /// Returns the underlying UDP socket.
  ///
  /// \returns A shared pointer to the wrapped UDP socket.
  const std::shared_ptr<AsyncUDPSocket>& getSocket() const { return socket_; }

  /// Closes the underlying socket and releases it.
  void close() {
    CHECK(socket_) << "Need to bind before closing";
    socket_->close();
    socket_.reset();
  }

  /// Returns the event base that drives this server socket.
  ///
  /// \returns The owning event base.
  EventBase* getEventBase() const override { return evb_; }

  /**
   * Indicates if the current socket is accepting.
   *
   * \returns True if the socket is currently reading packets.
   */
  bool isAccepting() const { return socket_->isReading(); }

  /**
   * Pauses accepting datagrams on the underlying socket.
   */
  void pauseAccepting() {
    socket_->pauseRead();
    for (auto& listener : listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback]() mutable {
        callback->onListenPaused();
      });
    }
  }

  /**
   * Inform the callback object that it should not process read from new client
   * address.
   */
  void pauseAcceptingNewPeer() {
    for (auto& listener : listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback]() mutable {
        callback->onAcceptNewPeerPaused();
      });
    }
  }

  /**
   * Starts accepting datagrams once again.
   */
  void resumeAccepting() {
    socket_->resumeRead(this);
    for (auto& listener : listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback]() mutable {
        callback->onListenResumed();
      });
    }
  }

  /**
   * Inform the callback object that it can process read from new client address
   * now.
   */
  void resumeAcceptingNewPeer() {
    for (auto& listener : listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback]() mutable {
        callback->onAcceptNewPeerResumed();
      });
    }
  }

  /// Enables socket timestamping options.
  ///
  /// \param val A bitmask of timestamping flags to apply.
  /// \returns True if the option was set successfully.
  bool setTimestamping(int val) { return socket_->setTimestamping(val); }

 private:
  // AsyncUDPSocket::ReadCallback
  void getReadBuffer(void** buf, size_t* len) noexcept override {
    std::tie(*buf, *len) = buf_.preallocate(packetSize_, packetSize_);
  }

  void onDataAvailable(
      const folly::SocketAddress& clientAddress,
      size_t len,
      bool truncated,
      OnDataAvailableParams params) noexcept override {
    buf_.postallocate(len);
    auto data = buf_.split(len);

    if (listeners_.empty()) {
      LOG(WARNING) << "UDP server socket dropping packet, "
                   << "no listener registered";
      return;
    }

    uint32_t listenerId = 0;
    uint64_t client_hash_lo = 0;
    switch (dispatchMechanism_) {
      case DispatchMechanism::ClientAddressHash:
        // Hash base on clientAddress.
        // 1. This logic is samilar to: clientAddress.hash() % listeners_.size()
        //    But runs faster as it use multiply and shift instead of division.
        // 2. Only use the lower 32 bit from the address hash result for faster
        //    computation.
        client_hash_lo = static_cast<uint32_t>(clientAddress.hash());
        listenerId = (client_hash_lo * listeners_.size()) >> 32;
        break;
      case DispatchMechanism::RoundRobin: // round robin is default.
      default:
        if (nextListener_ >= listeners_.size()) {
          nextListener_ = 0;
        }
        listenerId = nextListener_;
        ++nextListener_;
        break;
    }

    auto callback = listeners_[listenerId].second;

    // Schedule it in the listener's eventbase
    // XXX: Speed this up
    auto f =
        [socket = socket_,
         client = clientAddress,
         callback,
         data_2 = std::move(data),
         truncated,
         params]() mutable {
          callback->onDataAvailable(
              socket, client, std::move(data_2), truncated, params);
        };

    listeners_[listenerId].first->runInEventBaseThread(std::move(f));
  }

  void onReadError(const AsyncSocketException& ex) noexcept override {
    LOG(ERROR) << ex.what();

    // Lets register to continue listening for packets
    socket_->resumeRead(this);
  }

  void onReadClosed() noexcept override {
    for (auto& listener : listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback]() mutable {
        callback->onListenStopped();
      });
    }
  }

  EventBase* const evb_;
  const size_t packetSize_;

  std::shared_ptr<AsyncUDPSocket> socket_;

  // List of listener to distribute packets among
  using Listener = std::pair<EventBase*, Callback*>;
  std::vector<Listener> listeners_;

  DispatchMechanism dispatchMechanism_;

  // Next listener to send packet to
  uint32_t nextListener_;

  // Temporary buffer for data
  folly::IOBufQueue buf_;

  bool reusePort_{false};
  bool reuseAddr_{false};
  bool recvTos_{false};
};

} // namespace folly
