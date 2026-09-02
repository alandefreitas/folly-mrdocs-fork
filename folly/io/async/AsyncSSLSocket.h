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

#include <iomanip>

#include <folly/Optional.h>
#include <folly/String.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncPipe.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/CertificateIdentityVerifier.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/TimeoutManager.h>
#include <folly/io/async/ssl/OpenSSLUtils.h>
#include <folly/io/async/ssl/SSLErrors.h>
#include <folly/io/async/ssl/TLSDefinitions.h>
#include <folly/lang/Bits.h>
#include <folly/portability/OpenSSL.h>
#include <folly/portability/Sockets.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <folly/ssl/SSLSession.h>
#include <folly/ssl/SSLSessionManager.h>

/// Facebook Folly library namespace.
namespace folly {

/// Connector that drives the SSL handshake for an AsyncSSLSocket.
class AsyncSSLSocketConnector;

/**
 * A class for performing asynchronous I/O on an SSL connection.
 *
 * AsyncSSLSocket allows users to asynchronously wait for data on an
 * SSL connection, and to asynchronously send data.
 *
 * The APIs for reading and writing are intentionally asymmetric.
 * Waiting for data to read is a persistent API: a callback is
 * installed, and is notified whenever new data is available.  It
 * continues to be notified of new events until it is uninstalled.
 *
 * AsyncSSLSocket does not provide read timeout functionality,
 * because it typically cannot determine when the timeout should be
 * active.  Generally, a timeout should only be enabled when
 * processing is blocked waiting on data from the remote endpoint.
 * For server connections, the timeout should not be active if the
 * server is currently processing one or more outstanding requests for
 * this connection.  For client connections, the timeout should not be
 * active if there are no requests pending on the connection.
 * Additionally, if a client has multiple pending requests, it will
 * usually want a separate timeout for each request, rather than a
 * single read timeout.
 *
 * The write API is fairly intuitive: a user can request to send a
 * block of data, and a callback will be informed once the entire
 * block has been transferred to the kernel, or on error.
 * AsyncSSLSocket does provide a send timeout, since most callers
 * want to give up if the remote end stops responding and no further
 * progress can be made sending the data.
 */
class AsyncSSLSocket : public AsyncSocket {
 public:
  /// Owning pointer type that respects the socket's deferred destructor.
  using UniquePtr = std::unique_ptr<AsyncSSLSocket, Destructor>;
  /// Deleter that frees an X509 certificate.
  using X509_deleter = folly::static_function_deleter<X509, &X509_free>;

  /// Callback invoked on the outcome of an SSL handshake.
  class HandshakeCB {
   public:
    /// Destroys the callback.
    virtual ~HandshakeCB() = default;

    /**
     * handshakeVer() is invoked during handshaking to give the
     * application chance to validate it's peer's certificate.
     *
     * Note that OpenSSL performs only rudimentary internal
     * consistency verification checks by itself. Any other validation
     * like whether or not the certificate was issued by a trusted CA.
     * The default implementation of this callback mimics what what
     * OpenSSL does internally if SSL_VERIFY_PEER is set with no
     * verification callback.
     *
     * See the passages on verify_callback in SSL_CTX_set_verify(3)
     * for more details.
     *
     * \param sock SSL socket on which the handshake is being verified.
     * \param preverifyOk Whether OpenSSL's internal checks passed.
     * \param ctx The certificate store context being verified.
     * \returns true if the peer certificate is accepted.
     */
    virtual bool handshakeVer(
        AsyncSSLSocket* sock,
        bool preverifyOk,
        X509_STORE_CTX* ctx) noexcept {
      return preverifyOk;
    }

    /**
     * handshakeSuc() is called when a new SSL connection is
     * established, i.e., after SSL_accept/connect() returns successfully.
     *
     * The HandshakeCB will be uninstalled before handshakeSuc()
     * is called.
     *
     * @param sock  SSL socket on which the handshake was initiated
     */
    virtual void handshakeSuc(AsyncSSLSocket* sock) noexcept = 0;

    /**
     * handshakeErr() is called if an error occurs while
     * establishing the SSL connection.
     *
     * The HandshakeCB will be uninstalled before handshakeErr()
     * is called.
     *
     * @param sock  SSL socket on which the handshake was initiated
     * @param ex  An exception representing the error.
     */
    virtual void handshakeErr(
        AsyncSSLSocket* sock, const AsyncSocketException& ex) noexcept = 0;
  };

  /// Timeout helper that forwards expirations to the owning socket.
  class Timeout : public AsyncTimeout {
   public:
    /// Constructs a timeout bound to a socket and event base.
    /// \param sslSocket Socket that owns this timeout.
    /// \param eventBase Event base that drives the timeout.
    Timeout(AsyncSSLSocket* sslSocket, EventBase* eventBase)
        : AsyncTimeout(eventBase), sslSocket_(sslSocket) {}

    /// Schedules the timeout and remembers its duration.
    /// \param timeout Duration after which the timeout fires.
    /// \returns True if the timeout was scheduled successfully.
    bool scheduleTimeout(TimeoutManager::timeout_type timeout) {
      timeout_ = timeout;
      return AsyncTimeout::scheduleTimeout(timeout);
    }

    /// Schedules the timeout given a duration in milliseconds.
    /// \param timeoutMs Duration in milliseconds after which the timeout fires.
    /// \returns True if the timeout was scheduled successfully.
    bool scheduleTimeout(uint32_t timeoutMs) {
      return scheduleTimeout(std::chrono::milliseconds{timeoutMs});
    }

    /// Returns the currently scheduled timeout duration.
    /// \returns The scheduled timeout duration.
    TimeoutManager::timeout_type getTimeout() { return timeout_; }

    /// Forwards the expiration to the owning socket.
    void timeoutExpired() noexcept override {
      sslSocket_->timeoutExpired(timeout_);
    }

   private:
    AsyncSSLSocket* sslSocket_;
    TimeoutManager::timeout_type timeout_;
  };

  /**
   * A class to wait for asynchronous operations with OpenSSL 1.1.0
   */
  class DefaultOpenSSLAsyncFinishCallback : public ReadCallback {
   public:
    /// Constructs the callback from a pipe reader, socket, and guard.
    /// \param reader Pipe reader that delivers the wakeup signal.
    /// \param sslSocket Socket whose accept is resumed on completion.
    /// \param dg Destructor guard keeping the socket alive.
    DefaultOpenSSLAsyncFinishCallback(
        AsyncPipeReader::UniquePtr reader,
        AsyncSSLSocket* sslSocket,
        DestructorGuard dg)
        : pipeReader_(std::move(reader)),
          sslSocket_(sslSocket),
          dg_(std::move(dg)) {}

    /// Tears down the callback and detaches it from the socket.
    ~DefaultOpenSSLAsyncFinishCallback() override {
      pipeReader_->setReadCB(nullptr);
      sslSocket_->setAsyncOperationFinishCallback(nullptr);
    }

    /// Resumes the SSL accept once the async operation signals completion.
    /// \param len Number of signal bytes read; expected to be one.
    void readDataAvailable(size_t len) noexcept override {
      CHECK_EQ(len, 1u);
      sslSocket_->restartSSLAccept();
      pipeReader_->setReadCB(nullptr);
      sslSocket_->setAsyncOperationFinishCallback(nullptr);
    }

    /// Provides the single-byte buffer used to receive the wakeup signal.
    /// \param bufReturn Set to the buffer that receives the signal byte.
    /// \param lenReturn Set to the buffer length in bytes.
    void getReadBuffer(void** bufReturn, size_t* lenReturn) noexcept override {
      *bufReturn = &byte_;
      *lenReturn = 1;
    }

    /// Ignores end-of-file on the wakeup pipe.
    void readEOF() noexcept override {}

    /// Ignores read errors on the wakeup pipe.
    /// \param ex The read error, which is ignored.
    void readErr(const folly::AsyncSocketException& ex) noexcept override {}

   private:
    uint8_t byte_{0};
    AsyncPipeReader::UniquePtr pipeReader_;
    AsyncSSLSocket* sslSocket_{nullptr};
    DestructorGuard dg_;
  };

  /**
   * Struct to consolidate constructor arguments.
   */
  struct Options {
    // If this verifier is set, it's used during the TLS handshake. First,
    // verifyContext() is called during OpenSSL's certificate verification
    // callback for each certificate in the chain, after HandshakeCB's
    // handshakeVer() if set. Then, verifyLeaf() is invoked to verify the
    // peer's end-entity leaf certificate, but only if OpenSSL's chain
    // validation, handshakeVer(), and verifyContext() all succeeded.
    std::shared_ptr<const CertificateIdentityVerifier> verifier; ///< Optional peer identity verifier applied during the handshake.
    bool deferSecurityNegotiation{}; ///< Whether to defer the TLS handshake so unencrypted data may be sent first.
    bool isServer{}; ///< Whether the socket operates in server mode.
    std::string serverName; ///< SNI hostname advertised to the server.
  };

  /**
   * Initialize this AsyncSSLSocket object with the given Options.
   *
   * @param ctx SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param options optional arguments for this AsyncSSLSocket instance
   */
  AsyncSSLSocket(
      std::shared_ptr<const folly::SSLContext> ctx,
      EventBase* evb,
      Options&& options);

  /**
   * Initialize this AsyncSSLSocket object with the given Options from an
   * already connected AsyncSocket.
   *
   * @param ctx SSL context for this connection.
   * @param oldAsyncSocket already connected socket to take over.
   * @param options optional arguments for this AsyncSSLSocket instance
   */
  AsyncSSLSocket(
      std::shared_ptr<const folly::SSLContext> ctx,
      AsyncSocket::UniquePtr oldAsyncSocket,
      Options&& options);

  /**
   * Create a client AsyncSSLSocket
   *
   * @param ctx SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param deferSecurityNegotiation if true, unencrypted data can be sent
   *          before sslConn/Accept
   */
  AsyncSSLSocket(
      std::shared_ptr<const folly::SSLContext> ctx,
      EventBase* evb,
      bool deferSecurityNegotiation = false);

  /**
   * Create a server/client AsyncSSLSocket from an already connected
   * socket file descriptor.
   *
   * Note that while AsyncSSLSocket enables TCP_NODELAY for sockets it creates
   * when connecting, it does not change the socket options when given an
   * existing file descriptor.  If callers want TCP_NODELAY enabled when using
   * this version of the constructor, they need to explicitly call
   * setNoDelay(true) after the constructor returns.
   *
   * @param ctx             SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param fd  File descriptor to take over (should be a connected socket).
   * @param server Is socket in server mode?
   * @param deferSecurityNegotiation
   *          unencrypted data can be sent before sslConn/Accept
   * @param peerAddress optional peer address (eg: returned from accept).  If
   *          nullptr, AsyncSocket will lazily attempt to determine it from fd
   *          via a system call
   */
  AsyncSSLSocket(
      std::shared_ptr<const folly::SSLContext> ctx,
      EventBase* evb,
      NetworkSocket fd,
      bool server = true,
      bool deferSecurityNegotiation = false,
      const SocketAddress* peerAddress = nullptr);

  /**
   * Create a server/client AsyncSSLSocket from an already connected
   * AsyncSocket.
   *
   * @param ctx SSL context for this connection.
   * @param oldAsyncSocket already connected socket to take over.
   * @param server Is socket in server mode?
   * @param deferSecurityNegotiation if true, unencrypted data can be sent
   *          before sslConn/Accept
   */
  AsyncSSLSocket(
      std::shared_ptr<const folly::SSLContext> ctx,
      AsyncSocket* oldAsyncSocket,
      bool server = true,
      bool deferSecurityNegotiation = false);

  /**
   * Create a server/client AsyncSSLSocket from an already connected
   * AsyncSocket.
   *
   * @param ctx SSL context for this connection.
   * @param oldAsyncSocket already connected socket to take over.
   * @param server Is socket in server mode?
   * @param deferSecurityNegotiation if true, unencrypted data can be sent
   *          before sslConn/Accept
   */
  AsyncSSLSocket(
      std::shared_ptr<const folly::SSLContext> ctx,
      AsyncSocket::UniquePtr oldAsyncSocket,
      bool server = true,
      bool deferSecurityNegotiation = false);

  /**
   * Helper function to create a server/client shared_ptr<AsyncSSLSocket>.
   *
   * @param ctx SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param options optional arguments for this AsyncSSLSocket instance
   * @return a new AsyncSSLSocket owning pointer.
   */
  static UniquePtr newSocket(
      std::shared_ptr<const folly::SSLContext> ctx,
      EventBase* evb,
      Options&& options) {
    return AsyncSSLSocket::UniquePtr(
        new AsyncSSLSocket(std::move(ctx), evb, std::move(options)));
  }

  /**
   * Helper function to create a server/client shared_ptr<AsyncSSLSocket>.
   *
   * @param ctx SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param fd File descriptor to take over (should be a connected socket).
   * @param server Is socket in server mode?
   * @param deferSecurityNegotiation if true, unencrypted data can be sent
   *          before sslConn/Accept
   * @param peerAddress optional peer address (eg: returned from accept).
   * @return a new AsyncSSLSocket owning pointer.
   */
  static UniquePtr newSocket(
      const std::shared_ptr<const folly::SSLContext>& ctx,
      EventBase* evb,
      NetworkSocket fd,
      bool server = true,
      bool deferSecurityNegotiation = false,
      const folly::SocketAddress* peerAddress = nullptr) {
    return AsyncSSLSocket::UniquePtr(new AsyncSSLSocket(
        ctx, evb, fd, server, deferSecurityNegotiation, peerAddress));
  }

  /**
   * Helper function to create a client shared_ptr<AsyncSSLSocket>.
   *
   * @param ctx SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param deferSecurityNegotiation if true, unencrypted data can be sent
   *          before sslConn/Accept
   * @return a new AsyncSSLSocket owning pointer.
   */
  static UniquePtr newSocket(
      const std::shared_ptr<const folly::SSLContext>& ctx,
      EventBase* evb,
      bool deferSecurityNegotiation = false) {
    return AsyncSSLSocket::UniquePtr(
        new AsyncSSLSocket(ctx, evb, deferSecurityNegotiation));
  }

  /**
   * Create a client AsyncSSLSocket with tlsext_servername in
   * the Client Hello message.
   *
   * @param ctx SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param serverName tlsext_hostname that will be sent in ClientHello.
   * @param deferSecurityNegotiation if true, unencrypted data can be sent
   *          before sslConn/Accept
   */
  AsyncSSLSocket(
      const std::shared_ptr<const folly::SSLContext>& ctx,
      EventBase* evb,
      const std::string& serverName,
      bool deferSecurityNegotiation = false);

  /**
   * Create a client AsyncSSLSocket from an already connected
   * socket file descriptor.
   *
   * Note that while AsyncSSLSocket enables TCP_NODELAY for sockets it creates
   * when connecting, it does not change the socket options when given an
   * existing file descriptor.  If callers want TCP_NODELAY enabled when using
   * this version of the constructor, they need to explicitly call
   * setNoDelay(true) after the constructor returns.
   *
   * @param ctx  SSL context for this connection.
   * @param evb  EventBase that will manage this socket.
   * @param fd   File descriptor to take over (should be a connected socket).
   * @param serverName tlsext_hostname that will be sent in ClientHello.
   * @param deferSecurityNegotiation
   *          unencrypted data can be sent before sslConn/Accept
   * @param peerAddr optional peer address (eg: returned from accept).  If
   *          nullptr, AsyncSocket will lazily attempt to determine it from fd
   *          via a system call
   */
  AsyncSSLSocket(
      const std::shared_ptr<const folly::SSLContext>& ctx,
      EventBase* evb,
      NetworkSocket fd,
      const std::string& serverName,
      bool deferSecurityNegotiation = false,
      const SocketAddress* peerAddr = nullptr);

  /// Creates a client AsyncSSLSocket with the given SNI hostname.
  /// \param ctx SSL context for this connection.
  /// \param evb EventBase that will manage this socket.
  /// \param serverName tlsext_hostname that will be sent in ClientHello.
  /// \param deferSecurityNegotiation If true, unencrypted data can be sent
  ///          before sslConn/Accept.
  /// \returns A new AsyncSSLSocket owning pointer.
  static UniquePtr newSocket(
      const std::shared_ptr<const folly::SSLContext>& ctx,
      EventBase* evb,
      const std::string& serverName,
      bool deferSecurityNegotiation = false) {
    return AsyncSSLSocket::UniquePtr(
        new AsyncSSLSocket(ctx, evb, serverName, deferSecurityNegotiation));
  }

  /**
   * TODO: implement support for SSL renegotiation.
   *
   * This involves proper handling of the SSL_ERROR_WANT_READ/WRITE
   * code as a result of SSL_write/read(), instead of returning an
   * error. In that case, the READ/WRITE event should be registered,
   * and a flag (e.g., writeBlockedOnRead) should be set to indiciate
   * the condition. In the next invocation of read/write callback, if
   * the flag is on, performWrite()/performReadMsg() should be called in
   * addition to the normal call to performReadMsg()/performWrite(), and
   * the flag should be reset.
   */

  // Inherit AsyncTransport methods from AsyncSocket except the
  // following.
  // See the documentation in AsyncTransport.h
  // TODO: implement graceful shutdown in close()
  // TODO: implement detachSSL() that returns the SSL connection
  void closeNow() override;
  /// Half-closes the write side of the connection after pending writes.
  void shutdownWrite() override;
  /// Half-closes the write side of the connection immediately.
  void shutdownWriteNow() override;
  /// Returns whether the socket has data available to read.
  /// \returns True if the socket has readable data.
  bool readable() const override;
  /// Returns whether the socket is in a usable state.
  /// \returns True if the socket is usable.
  bool good() const override;
  /// Returns whether the socket is still establishing a connection.
  /// \returns True if the socket is still connecting.
  bool connecting() const override;
  /// Returns the application protocol negotiated via ALPN.
  /// \returns The negotiated protocol name, or empty if none.
  std::string getApplicationProtocol() const noexcept override;
  /// Sets the application protocols offered during ALPN negotiation.
  /// \param supportedProtocols Protocol names to offer, in preference order.
  void setSupportedApplicationProtocols(
      const std::vector<std::string>& supportedProtocols);

  /// Returns the security protocol name, or empty when unencrypted.
  /// \returns "TLS" when encrypted, or empty when unencrypted.
  std::string getSecurityProtocol() const override {
    if (sslState_ == SSLStateEnum::STATE_UNENCRYPTED) {
      return "";
    }
    return "TLS";
  }

  /// Derives exported keying material for the given label and context.
  /// \param label Label identifying the keying material to export.
  /// \param context Optional context value bound into the derivation.
  /// \param length Number of bytes of keying material to derive.
  /// \returns The derived keying material.
  std::unique_ptr<folly::IOBuf> getExportedKeyingMaterial(
      folly::StringPiece label,
      std::unique_ptr<IOBuf> context,
      uint16_t length) const override;

  /// Enables or disables end-of-record tracking on writes.
  /// \param track True to enable end-of-record tracking.
  void setEorTracking(bool track) override;
  /// Returns the number of raw bytes written to the underlying socket.
  /// \returns The number of raw bytes written.
  size_t getRawBytesWritten() const override;
  /// Returns the number of raw bytes received from the underlying socket.
  /// \returns The number of raw bytes received.
  size_t getRawBytesReceived() const override;

  // End of methods inherited from AsyncTransport

  /**
   * Enable ByteEvents for this socket.
   *
   * ByteEvents cannot be enabled if TLS 1.0 or earlier is in use, as these
   * client implementations often have trouble handling cases where a TLS
   * record is split across multiple packets.
   */
  void enableByteEvents() override;

  /// Enables capturing of the peer's ClientHello for later inspection.
  void enableClientHelloParsing();

  /**
   * Accept an SSL connection on the socket.
   *
   * The callback will be invoked and uninstalled when an SSL
   * connection has been established on the underlying socket.
   * The value of verifyPeer determines the client verification method.
   * By default, its set to use the value in the underlying context
   *
   * @param callback callback object to invoke on success/failure
   * @param timeout timeout for this function in milliseconds, or 0 for no
   *                timeout
   * @param verifyPeer  SSLVerifyPeerEnum uses the options specified in the
   *                context by default, can be set explcitly to override the
   *                method in the context
   */
  virtual void sslAccept(
      HandshakeCB* callback,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
      const folly::SSLContext::SSLVerifyPeerEnum& verifyPeer =
          folly::SSLContext::SSLVerifyPeerEnum::USE_CTX);

  /**
   * Invoke SSL accept following an asynchronous session cache lookup
   */
  void restartSSLAccept();

  /**
   * Connect to the given address, invoking callback when complete or on error
   *
   * Note timeout applies to TCP + SSL connection time
   *
   * @param callback callback object to invoke on success/failure.
   * @param address address to connect to.
   * @param timeout timeout in milliseconds for the combined TCP + SSL connect.
   * @param options socket options to apply to the connection.
   * @param bindOptions local bind options to apply before connecting.
   * @param ifName interface name to bind to, or empty for none.
   */
  void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      int timeout = 0,
      const SocketOptionMap& options = emptySocketOptionMap,
      const BindOptions& bindOptions = anyAddress(),
      const std::string& ifName = "") noexcept override;

  /**
   * A variant of connect that allows the caller to specify
   * the timeout for the regular connect and the ssl connect
   * separately.
   * connectTimeout is specified as the time to establish a TCP
   * connection.
   * totalConnectTimeout defines the
   * time it takes from starting the TCP connection to the time
   * the ssl connection is established. The reason the timeout is
   * defined this way is because user's rarely need to specify the SSL
   * timeout independently of the connect timeout. It allows us to
   * bound the time for a connect and SSL connection in
   * a finer grained manner than if timeout was just defined
   * independently for SSL.
   *
   * @param callback callback object to invoke on success/failure.
   * @param address address to connect to.
   * @param connectTimeout time allowed to establish the TCP connection.
   * @param totalConnectTimeout time allowed for the TCP + SSL connection.
   * @param options socket options to apply to the connection.
   * @param bindOptions local bind options to apply before connecting.
   * @param ifName interface name to bind to, or empty for none.
   */
  virtual void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      std::chrono::milliseconds connectTimeout,
      std::chrono::milliseconds totalConnectTimeout,
      const SocketOptionMap& options = emptySocketOptionMap,
      const BindOptions& bindOptions = anyAddress(),
      const std::string& ifName = "") noexcept;

  /// Inherits the base class connect overloads.
  using AsyncSocket::connect;

  /**
   * If a connect request is in-flight, cancels it and closes the socket
   * immediately. Otherwise, this is a no-op.
   *
   * This does not invoke any connection related callbacks. Call this to
   * prevent any connect callback while cleaning up, etc.
   */
  void cancelConnect() override;

  /**
   * Initiate an SSL connection on the socket
   * The callback will be invoked and uninstalled when an SSL connection
   * has been establshed on the underlying socket.
   * The verification option verifyPeer is applied if it's passed explicitly.
   * If it's not, the options in SSLContext set on the underlying SSLContext
   * are applied.
   *
   * @param callback callback object to invoke on success/failure
   * @param timeout timeout for this function in milliseconds, or 0 for no
   *                timeout
   * @param verifyPeer  SSLVerifyPeerEnum uses the options specified in the
   *                context by default, can be set explcitly to override the
   *                method in the context. If verification is turned on sets
   *                SSL_VERIFY_PEER and invokes
   *                HandshakeCB::handshakeVer().
   */
  virtual void sslConn(
      HandshakeCB* callback,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
      const folly::SSLContext::SSLVerifyPeerEnum& verifyPeer =
          folly::SSLContext::SSLVerifyPeerEnum::USE_CTX);

  /// Lifecycle states of the SSL connection.
  enum SSLStateEnum {
    STATE_UNINIT, ///< Not yet initialized.
    STATE_UNENCRYPTED, ///< Open but not yet encrypted.
    STATE_ACCEPTING, ///< Performing the server-side handshake.
    STATE_ASYNC_PENDING, ///< Waiting on an asynchronous handshake operation.
    STATE_CONNECTING, ///< Performing the client-side handshake.
    STATE_ESTABLISHED, ///< Handshake complete and connection encrypted.
    STATE_REMOTE_CLOSED, ///< remote end closed; we can still write
    STATE_CLOSING, ///< close() called, but waiting on writes to complete
    /// close() called with pending writes, before connect() has completed
    STATE_CONNECTING_CLOSING,
    STATE_CLOSED, ///< Connection fully closed.
    STATE_ERROR ///< The connection is in an error state.
  };

  /// Returns the current SSL connection state.
  /// \returns The current SSL connection state.
  SSLStateEnum getSSLState() const { return sslState_; }

  /**
   * Retrieve the SSL session associated with this established connection.
   *
   * The SSL Session object is a copyable, opaque token that can be set on other
   * unconnected AsyncSSLSockets. If AsyncSSLSocket::connect() is called with a
   * previous session set, TLS resumption will be attempted.
   *
   * @return the SSL session associated with this connection.
   */
  std::shared_ptr<ssl::SSLSession> getSSLSession();

  /**
   * Get a handle to the SSL struct.
   *
   * @return the underlying OpenSSL SSL object.
   */
  const SSL* getSSL() const;

  /**
   * Sets the SSL session that will be attempted for TLS resumption.
   *
   * @param session the SSL session to attempt to resume.
   */
  void setSSLSession(std::shared_ptr<ssl::SSLSession> session);

  /**
   * Note: This function exists for compatibility reasons. It is strongly
   * recommended to use setSSLSession instead. After setRawSSLSession is
   * called, subsequent calls to getSSLSession on the socket will return null.
   *
   * Set the SSL session to be used during sslConn.
   * If the caller wishes to resume the session in TLS 1.3, the caller
   * is responsible for ensuring that the session is resumable.
   * If the session is not resumable, then a full handshake will be performed.
   *
   * @param session the raw SSL session to use during sslConn.
   */
  void setRawSSLSession(folly::ssl::SSLSessionUniquePtr session);

  /**
   * Get the name of the protocol selected by the client during
   * Application Layer Protocol Negotiation (ALPN)
   *
   * Throw an exception if openssl does not support NPN
   *
   * @param protoName      Name of the protocol (not guaranteed to be
   *                       null terminated); will be set to nullptr if
   *                       the client did not negotiate a protocol.
   *                       Note: the AsyncSSLSocket retains ownership
   *                       of this string.
   * @param protoLen       Length of the name.
   */
  virtual void getSelectedNextProtocol(
      const unsigned char** protoName, unsigned* protoLen) const;

  /**
   * Get the name of the protocol selected by the client during
   * Next Protocol Negotiation (NPN) or Application Layer Protocol Negotiation
   * (ALPN)
   *
   * @param protoName      Name of the protocol (not guaranteed to be
   *                       null terminated); will be set to nullptr if
   *                       the client did not negotiate a protocol.
   *                       Note: the AsyncSSLSocket retains ownership
   *                       of this string.
   * @param protoLen       Length of the name.
   * @return false if openssl does not support NPN
   */
  virtual bool getSelectedNextProtocolNoThrow(
      const unsigned char** protoName, unsigned* protoLen) const;

  /**
   * Determine if the session specified during setSSLSession was reused
   * or if the server rejected it and issued a new session.
   *
   * @return true if the session was reused.
   */
  virtual bool getSSLSessionReused() const;

  /**
   * true if the session was resumed using session ID
   *
   * @return true if the session was resumed using a session ID.
   */
  bool sessionIDResumed() const { return sessionIDResumed_; }

  /// Records whether the session was resumed using a session ID.
  /// \param resumed True if the session was resumed using a session ID.
  void setSessionIDResumed(bool resumed) { sessionIDResumed_ = resumed; }

  /**
   * Get the negociated cipher name for this SSL connection.
   * Returns the cipher used or the constant value "NONE" when no SSL session
   * has been established.
   *
   * @return the negotiated cipher name, or "NONE".
   */
  virtual const char* getNegotiatedCipherName() const;

  /**
   * Get the server name for this SSL connection. Returns the SNI sent in the
   * ClientHello, if enableClientHelloParsing() was called.
   *
   * Returns the server name used or the constant value "NONE" when no SSL
   * session has been established.
   * If openssl has no SNI support, throw AsyncSocketException.
   *
   * @return the SNI server name, or "NONE".
   */
  const char* getSSLServerName() const;

  /**
   * Get the server name for this SSL connection.
   * Returns the server name used or the constant value "NONE" when no SSL
   * session has been established.
   * If openssl has no SNI support, return "NONE"
   *
   * @return the SNI server name, or "NONE".
   */
  const char* getSSLServerNameNoThrow() const;

  /**
   * Get the SSL version for this connection.
   * Possible return values are SSL2_VERSION, SSL3_VERSION, TLS1_VERSION,
   * with hexa representations 0x200, 0x300, 0x301,
   * or 0 if no SSL session has been established.
   *
   * @return the SSL/TLS protocol version, or 0.
   */
  int getSSLVersion() const;

  /**
   * Get the signature algorithm used in the cert that is used for this
   * connection.
   *
   * @return the certificate's signature algorithm name.
   */
  const char* getSSLCertSigAlgName() const;

  /**
   * Get the certificate size used for this SSL connection.
   *
   * @return the certificate size in bits.
   */
  int getSSLCertSize() const;

  /// Attaches the socket and its timeouts to the given event base.
  /// \param eventBase Event base to attach the socket to.
  void attachEventBase(EventBase* eventBase) override {
    AsyncSocket::attachEventBase(eventBase);
    handshakeTimeout_.attachEventBase(eventBase);
    connectionTimeout_.attachEventBase(eventBase);
  }

  /// Detaches the socket and its timeouts from the current event base.
  void detachEventBase() override {
    AsyncSocket::detachEventBase();
    handshakeTimeout_.detachEventBase();
    connectionTimeout_.detachEventBase();
  }

  /// Returns whether the socket can be detached from its event base.
  /// \returns True if the socket can be detached.
  bool isDetachable() const override {
    return AsyncSocket::isDetachable() && !handshakeTimeout_.isScheduled();
  }

  /// Attaches the handshake timeout to the given timeout manager.
  /// \param manager Timeout manager to attach the handshake timeout to.
  virtual void attachTimeoutManager(TimeoutManager* manager) {
    handshakeTimeout_.attachTimeoutManager(manager);
  }

  /// Detaches the handshake timeout from its timeout manager.
  virtual void detachTimeoutManager() {
    handshakeTimeout_.detachTimeoutManager();
  }

  /**
   * This function will set the SSL context for this socket to the
   * argument. This should only be used on client SSL Sockets that have
   * already called detachSSLContext();
   *
   * @param ctx the SSL context to attach to this socket.
   */
  void attachSSLContext(const std::shared_ptr<const folly::SSLContext>& ctx);

  /**
   * Detaches the SSL context for this socket.
   */
  void detachSSLContext();

  /**
   * Returns the original folly::SSLContext associated with this socket.
   *
   * Suitable for use in AsyncSSLSocket constructor to construct a new
   * AsyncSSLSocket using an existing socket's context.
   *
   * switchServerSSLContext() does not affect this return value.
   *
   * @return the original SSL context associated with this socket.
   */
  const std::shared_ptr<const folly::SSLContext>& getSSLContext() const {
    return ctx_;
  }

  /**
   * Switch the SSLContext to continue the SSL handshake.
   * It can only be used in server mode.
   *
   * @param handshakeCtx the SSL context to use for the handshake.
   */
  void switchServerSSLContext(
      const std::shared_ptr<const folly::SSLContext>& handshakeCtx);

  /**
   * Did server recognize/support the tlsext_hostname in Client Hello?
   * It can only be used in client mode.
   *
   * @return true - tlsext_hostname is matched by the server
   *         false - tlsext_hostname is not matched or
   *                 is not supported by server
   */
  bool isServerNameMatch() const;

  /**
   * Set the SNI hostname that we'll advertise to the server in the
   * ClientHello message.
   *
   * @param serverName the SNI hostname to advertise.
   */
  void setServerName(std::string serverName) noexcept;

  /// Handles expiration of the handshake or connection timeout.
  /// \param timeout Duration of the timeout that expired.
  void timeoutExpired(std::chrono::milliseconds timeout) noexcept;

  /**
   * Get the list of supported ciphers sent by the client in the client's
   * preference order.
   *
   * @param clientCiphers output string that receives the client's ciphers.
   * @param convertToString whether to convert cipher IDs to human-readable
   *          names.
   */
  void getSSLClientCiphers(
      std::string& clientCiphers, bool convertToString = true) const;

  /**
   * Get the list of compression methods sent by the client in TLS Hello.
   *
   * @return the client's compression methods.
   */
  std::string getSSLClientComprMethods() const;

  /**
   * Get the list of TLS extensions sent by the client in the TLS Hello.
   *
   * @return the client's TLS extensions.
   */
  std::string getSSLClientExts() const;

  /// Returns the signature algorithms sent by the client in the TLS Hello.
  /// \returns The client's signature algorithms.
  std::string getSSLClientSigAlgs() const;

  /**
   * Get the list of versions in the supported versions extension (used to
   * negotiate TLS 1.3).
   *
   * @return the client's supported versions.
   */
  std::string getSSLClientSupportedVersions() const;

  /// Returns the alerts received on this connection as a string.
  /// \returns The received alerts.
  std::string getSSLAlertsReceived() const;

  /// Save an optional alert message generated during certificate verify.
  /// \param alert Alert message to save.
  void setSSLCertVerificationAlert(std::string alert);

  /// Returns the alert message saved during certificate verification.
  /// \returns The saved certificate-verification alert.
  std::string getSSLCertVerificationAlert() const;

  /**
   * Get the list of shared ciphers between the server and the client.
   * Works well for only SSLv2, not so good for SSLv3 or TLSv1.
   *
   * @param sharedCiphers output string that receives the shared ciphers.
   */
  void getSSLSharedCiphers(std::string& sharedCiphers) const;

  /**
   * Get the list of ciphers supported by the server in the server's
   * preference order.
   *
   * @param serverCiphers output string that receives the server's ciphers.
   */
  void getSSLServerCiphers(std::string& serverCiphers) const;

  /**
   * Get the list of next protocols sent from the client. The protocols are
   * directly as the client passed them and may be arbitrary byte sequences
   * of arbitrary length.
   *
   * @return the ALPN protocols offered by the client.
   */
  const std::vector<std::string>& getClientAlpns() const;

  /**
   * Method to check if peer verfication is set.
   *
   * @return true if peer verification is required.
   */
  bool needsPeerVerification() const;

  /// Returns the OpenSSL ex_data index used to store the AsyncSSLSocket.
  /// \returns The ex_data index.
  static int getSSLExDataIndex();
  /// Returns the AsyncSSLSocket associated with the given SSL object.
  /// \param ssl SSL object to look up.
  /// \returns The associated AsyncSSLSocket, or nullptr if none.
  static AsyncSSLSocket* getFromSSL(const SSL* ssl);
  /// OpenSSL BIO write callback that forwards data to the socket.
  /// \param b BIO performing the write.
  /// \param in Buffer of bytes to write.
  /// \param inl Number of bytes to write.
  /// \returns The number of bytes written, or a negative value on error.
  static int bioWrite(BIO* b, const char* in, int inl);
  /// OpenSSL BIO read callback that forwards data from the socket.
  /// \param b BIO performing the read.
  /// \param out Buffer that receives the read bytes.
  /// \param outl Maximum number of bytes to read.
  /// \returns The number of bytes read, or a negative value on error.
  static int bioRead(BIO* b, char* out, int outl);
  /// Resets the state used while parsing the peer's ClientHello.
  /// \param ssl SSL object whose ClientHello parsing state is reset.
  void resetClientHelloParsing(SSL* ssl);
  /// Parses the ALPN extension from a client's ClientHello.
  /// \param sock Socket whose ClientHello is being parsed.
  /// \param cursor Cursor positioned at the ALPN extension data.
  /// \param extensionDataLength Length of the extension data to parse.
  static void parseClientAlpns(
      AsyncSSLSocket* sock,
      folly::io::Cursor& cursor,
      uint16_t& extensionDataLength);
  /// OpenSSL message callback that captures the peer's ClientHello.
  /// \param written Nonzero when the message is being written rather than read.
  /// \param version TLS protocol version of the message.
  /// \param contentType TLS record content type.
  /// \param buf Buffer holding the message bytes.
  /// \param len Length of the message in bytes.
  /// \param ssl SSL object the message belongs to.
  /// \param arg User argument registered with the callback.
  static void clientHelloParsingCallback(
      int written,
      int version,
      int contentType,
      const void* buf,
      size_t len,
      SSL* ssl,
      void* arg);
  /// Returns the SNI server name stored on the given SSL object.
  /// \param ssl SSL object to read the server name from.
  /// \returns The stored SNI server name.
  static const char* getSSLServerNameFromSSL(SSL* ssl);

  /// Returns the parsed ClientHello info; for unit tests.
  /// \returns The parsed ClientHello info, or nullptr if none.
  ssl::ClientHelloInfo* getClientHelloInfo() const {
    return clientHelloInfo_.get();
  }

  /**
   * Returns the time taken to complete a handshake.
   *
   * @return the handshake duration.
   */
  virtual std::chrono::nanoseconds getHandshakeTime() const {
    return handshakeEndTime_ - handshakeStartTime_;
  }

  /// Sets the minimum buffer size below which SSL_write is avoided.
  /// \param minWriteSize Minimum write size threshold in bytes.
  void setMinWriteSize(size_t minWriteSize) { minWriteSize_ = minWriteSize; }

  /// Returns the minimum write size threshold.
  /// \returns The minimum write size threshold in bytes.
  size_t getMinWriteSize() const { return minWriteSize_; }

  /// Returns the peer's certificate, or nullptr if none is available.
  /// \returns The peer's certificate, or nullptr if none.
  const AsyncTransportCertificate* getPeerCertificate() const override;
  /// Returns this endpoint's certificate, or nullptr if none is available.
  /// \returns This endpoint's certificate, or nullptr if none.
  const AsyncTransportCertificate* getSelfCertificate() const override;

  /**
   * Force AsyncSSLSocket object to cache local and peer socket addresses.
   * If called with "true" before connect() this function forces full local
   * and remote socket addresses to be cached in the socket object and available
   * through getLocalAddress()/getPeerAddress() methods even after the socket is
   * closed.
   *
   * @param force whether to cache addresses even on connection failure.
   */
  void forceCacheAddrOnFailure(bool force) { cacheAddrOnFailure_ = force; }

  /// Returns the key used to cache the established session.
  /// \returns The session cache key.
  const std::string& getSessionKey() const { return sessionKey_; }

  /// Sets the key used to cache the established session.
  /// \param sessionKey Session cache key.
  void setSessionKey(std::string sessionKey) {
    sessionKey_ = std::move(sessionKey);
  }

  /// Records whether the certificate was served from cache.
  /// \param hit True if the certificate was served from cache.
  void setCertCacheHit(bool hit) { certCacheHit_ = hit; }

  /// Returns whether the certificate was served from cache.
  /// \returns True if the certificate was served from cache.
  bool getCertCacheHit() const { return certCacheHit_; }

  /// Returns whether session resumption was attempted.
  /// \returns True if session resumption was attempted.
  bool sessionResumptionAttempted() const {
    return sessionResumptionAttempted_;
  }

  /**
   * If the SSL socket was used to connect as well
   * as establish an SSL connection, this gives the total
   * timeout for the connect + SSL connection that was
   * set.
   *
   * @return the total connect + SSL connection timeout.
   */
  std::chrono::milliseconds getTotalConnectTimeout() const {
    return totalConnectTimeout_;
  }

  /// Sets the callback invoked when an OpenSSL async operation finishes.
  /// \param cb Callback to invoke on async operation completion.
  void setAsyncOperationFinishCallback(std::unique_ptr<ReadCallback> cb) {
    asyncOperationFinishCallback_ = std::move(cb);
  }

  /// Enables zero copy only when security negotiation is deferred.
  /// \param enable True to enable zero copy.
  /// \returns True if zero copy was enabled.
  bool setZeroCopy(bool enable) override {
    if (sslState_ == SSLStateEnum::STATE_UNENCRYPTED) {
      return AsyncSocket::setZeroCopy(enable);
    }
    return false;
  }

  /// Returns the name of the negotiated key exchange group.
  /// \returns The negotiated group name.
  const char* getNegotiatedGroup() const;

  /// Creates the underlying SSL object if it does not already exist.
  void ensureSSL();

 private:
  /**
   * Handle the return from invoking SSL_accept
   */
  void handleReturnFromSSLAccept(int ret);

  void init();

  ReadResult performReadSingle(void* buf, const size_t buflen);

  // Need to clean this up during a cancel if callback hasn't fired yet.
  AsyncSSLSocketConnector* allocatedConnectCallback_;

 protected:
  /**
   * Protected destructor.
   *
   * Users of AsyncSSLSocket must never delete it directly.  Instead, invoke
   * destroy() instead.  (See the documentation in DelayedDestruction.h for
   * more details.)
   */
  ~AsyncSSLSocket() override;

  // Inherit event notification methods from AsyncSocket except
  // the following.
  /// Handles a readability event on the socket.
  void handleRead() noexcept override;
  /// Handles a writability event on the socket.
  void handleWrite() noexcept override;
  /// Drives the server-side handshake when the socket becomes ready.
  void handleAccept() noexcept;
  /// Drives the client-side handshake when the connection completes.
  void handleConnect() noexcept override;

  /// Reports that a handshake callback was invoked in an invalid state.
  /// \param callback Handshake callback that was invoked.
  void invalidState(HandshakeCB* callback);
  /// Returns whether the last SSL operation would block, reporting errors.
  /// \param ret Return value of the SSL operation.
  /// \param sslErrorOut Set to the SSL_get_error result.
  /// \param errErrorOut Set to the OpenSSL error queue code.
  /// \returns True if the operation would block.
  bool willBlock(
      int ret, int* sslErrorOut, unsigned long* errErrorOut) noexcept;

  /// Checks whether a read can be satisfied immediately.
  void checkForImmediateRead() noexcept override;
  /// No-op override; AsyncSocket calls this at the wrong time for SSL.
  void handleInitialReadWrite() noexcept override {}

  /// Translates an SSL error code into a write result.
  /// \param rc Return value of the SSL write operation.
  /// \param error SSL_get_error result for the operation.
  /// \returns The resulting write result.
  WriteResult interpretSSLError(int rc, int error);
  /// Reads decrypted data into the given message header.
  /// \param msg Message header that receives the read data.
  /// \param readMode Read mode controlling how much data is read.
  /// \returns The result of the read.
  ReadResult performReadMsg(
      struct ::msghdr& msg,
      AsyncReader::ReadCallback::ReadMode readMode) override;
  /// Writes the given buffers through the SSL layer.
  /// \param vec Array of buffers to write.
  /// \param count Number of buffers in \p vec.
  /// \param flags Write flags controlling the operation.
  /// \param countWritten Set to the number of buffers fully written.
  /// \param partialWritten Set to the bytes written from a partial buffer.
  /// \param writeTag Tag identifying the write request.
  /// \returns The result of the write.
  WriteResult performWrite(
      const iovec* vec,
      uint32_t count,
      WriteFlags flags,
      uint32_t* countWritten,
      uint32_t* partialWritten,
      WriteRequestTag writeTag) override;

  /// Writes a single iovec batch through the SSL layer.
  /// \param vec Array of buffers to write.
  /// \param count Number of buffers in \p vec.
  /// \param flags Write flags controlling the operation.
  /// \param countWritten Set to the number of buffers fully written.
  /// \param partialWritten Set to the bytes written from a partial buffer.
  /// \returns The number of bytes written, or a negative value on error.
  ssize_t performWriteIovec(
      const iovec* vec,
      uint32_t count,
      WriteFlags flags,
      uint32_t* countWritten,
      uint32_t* partialWritten);

  /// Virtual wrapper around SSL_write, solely for testing/mockability.
  /// \param ssl SSL object to write to.
  /// \param buf Buffer of bytes to write.
  /// \param n Number of bytes to write.
  /// \returns The SSL_write return value.
  virtual int sslWriteImpl(SSL* ssl, const void* buf, int n) {
    return SSL_write(ssl, buf, n);
  }

  /// Virtual wrapper around SSL_get_error, solely for testing/mockability.
  /// \param s SSL object to query.
  /// \param ret_code Return code of the SSL operation to interpret.
  /// \returns The SSL_get_error result.
  virtual int sslGetErrorImpl(const SSL* s, int ret_code) {
    return SSL_get_error(s, ret_code);
  }

  /**
   * Apply verification options passed to sslConn/sslAccept or those set
   * in the underlying SSLContext object.
   *
   * @param ssl pointer to the SSL object on which verification options will be
   * applied. If verifyPeer_ was explicitly set either via sslConn/sslAccept,
   * those options override the settings in the underlying SSLContext.
   * @return true if the verification options were applied successfully.
   */
  bool applyVerificationOptions(const ssl::SSLUniquePtr& ssl);

  /**
   * Sets up SSL with a custom write bio which intercepts all writes.
   *
   * @return true, if succeeds and false if there is an error creating the bio.
   */
  bool setupSSLBio();

  // Inherit error handling methods from AsyncSocket, plus the following.
  /// Fails the current handshake and reports the error to the callback.
  /// \param fn Name of the function reporting the failure.
  /// \param ex Exception describing the failure.
  void failHandshake(const char* fn, const AsyncSocketException& ex);

  /// Invokes the handshake callback's error method.
  /// \param ex Exception describing the handshake error.
  void invokeHandshakeErr(const AsyncSocketException& ex);
  /// Invokes the handshake callback's success method.
  void invokeHandshakeCB();

  /// Invokes the connect callback's error method.
  /// \param ex Exception describing the connect error.
  void invokeConnectErr(const AsyncSocketException& ex) override;
  /// Invokes the connect callback's success method.
  void invokeConnectSuccess() override;
  /// Schedules the timeout that bounds the connect attempt.
  void scheduleConnectTimeout() override;

  /// Begins the client-side SSL handshake.
  void startSSLConnect();

  /// OpenSSL info callback used to observe handshake progress.
  /// \param ssl SSL object the event belongs to.
  /// \param where Bitmask describing the current SSL state.
  /// \param ret Return code associated with the event.
  static void sslInfoCallback(const SSL* ssl, int where, int ret);

  /// Creates the underlying SSL object for this connection.
  void createSSL();

  /// Whether the current write to the socket should use MSG_MORE.
  bool corkCurrentWrite_{false};
  /// Whether this socket operates in server mode.
  bool server_{false};
  /// Whether the SSL handshake has completed.
  bool handshakeComplete_{false};
  /// Whether a client-initiated renegotiation was attempted.
  bool renegotiateAttempted_{false};
  /// Current lifecycle state of the SSL connection.
  SSLStateEnum sslState_{SSLStateEnum::STATE_UNINIT};
  /// SSL context associated with this connection.
  std::shared_ptr<const folly::SSLContext> ctx_;
  /// Callback for SSL_accept() or SSL_connect().
  HandshakeCB* handshakeCallback_{nullptr};
  /// Optional verifier for the peer's certificate identity.
  std::shared_ptr<const CertificateIdentityVerifier>
      certificateIdentityVerifier_;
  /// Underlying OpenSSL SSL object.
  ssl::SSLUniquePtr ssl_;
  /// Timeout that bounds the SSL handshake.
  Timeout handshakeTimeout_;
  /// Timeout that bounds the connection attempt.
  Timeout connectionTimeout_;

  /// WriteFlags last passed to performWrite.
  WriteFlags currWriteFlags_{};

  /// Number of bytes to write before the final byte; see performWrite.
  folly::Optional<size_t> currBytesToFinalByte_;

  /// Buffers smaller than this avoid calling SSL_write(); 0 disables it.
  size_t minWriteSize_{1500};

  /// SSL context to use while completing the handshake.
  std::shared_ptr<const folly::SSLContext> handshakeCtx_;
  /// SNI hostname advertised in the ClientHello.
  std::string tlsextHostname_;

  /// Key that can be used for caching the established session.
  std::string sessionKey_;

  /// Peer verification mode applied during the handshake.
  folly::SSLContext::SSLVerifyPeerEnum verifyPeer_{
      folly::SSLContext::SSLVerifyPeerEnum::USE_CTX};

  /// Callback for SSL_CTX_set_verify().
  /// \param preverifyOk Whether OpenSSL's internal checks passed.
  /// \param ctx Certificate store context being verified.
  /// \returns 1 if the certificate is accepted, 0 otherwise.
  static int sslVerifyCallback(int preverifyOk, X509_STORE_CTX* ctx);

  /// Whether the peer's ClientHello should be parsed and captured.
  bool parseClientHello_{false};
  /// Whether to cache addresses even when the connection fails.
  bool cacheAddrOnFailure_{false};
  /// Whether the certificate was served from cache.
  bool certCacheHit_{false};
  /// Parsed information from the peer's ClientHello.
  std::unique_ptr<ssl::ClientHelloInfo> clientHelloInfo_;
  /// Alerts received on this connection.
  std::vector<std::pair<char, StringPiece>> alertsReceived_;

  /// Time at which the SSL handshake started.
  std::chrono::steady_clock::time_point handshakeStartTime_;
  /// Time at which the SSL handshake completed.
  std::chrono::steady_clock::time_point handshakeEndTime_;
  /// Timeout allowed to establish the TCP connection.
  std::chrono::milliseconds handshakeConnectTimeout_{0};
  /// Timeout allowed for the combined TCP + SSL connection.
  std::chrono::milliseconds totalConnectTimeout_{0};

  /// Alert message saved during certificate verification.
  std::string sslVerificationAlert_;

  /// Encoded ALPN protocol list offered during negotiation.
  std::string encodedAlpn_;

  /// Whether session resumption was attempted.
  bool sessionResumptionAttempted_{false};
  /// Whether the SSL session was resumed using a session ID.
  bool sessionIDResumed_{false};
  /// Callback invoked when an OpenSSL async operation finishes.
  std::unique_ptr<ReadCallback> asyncOperationFinishCallback_;
  /// Whether this socket is currently waiting on SSL_accept.
  bool waitingOnAccept_{false};
  /// Manages the session for the socket.
  folly::ssl::SSLSessionManager sslSessionManager_;
};

} // namespace folly
