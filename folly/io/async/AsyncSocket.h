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

#include <sys/types.h>

#include <chrono>
#include <map>
#include <memory>
#include <variant>

#include <folly/ConstructorCallbackList.h>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <folly/detail/SocketFastOpen.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufIovecBuilder.h>
#include <folly/io/ShutdownSocketSet.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncSocketTransport.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/IoUringConnect.h>
#include <folly/io/async/IoUringRecv.h>
#include <folly/io/async/IoUringSend.h>
#include <folly/io/async/WriteCallbackWithState.h>
#include <folly/io/async/observer/AsyncSocketObserverContainer.h>
#include <folly/net/NetOpsDispatcher.h>
#include <folly/net/TcpInfo.h>
#include <folly/net/TcpInfoDispatcher.h>
#include <folly/portability/Sockets.h>
#include <folly/small_vector.h>

namespace folly {

/**
 * A class for performing asynchronous I/O on a socket.
 *
 * AsyncSocket allows users to asynchronously wait for data on a socket, and
 * to asynchronously send data.
 *
 * The APIs for reading and writing are intentionally asymmetric.  Waiting for
 * data to read is a persistent API: a callback is installed, and is notified
 * whenever new data is available.  It continues to be notified of new events
 * until it is uninstalled.
 *
 * AsyncSocket does not provide read timeout functionality, because it
 * typically cannot determine when the timeout should be active.  Generally, a
 * timeout should only be enabled when processing is blocked waiting on data
 * from the remote endpoint.  For server sockets, the timeout should not be
 * active if the server is currently processing one or more outstanding
 * requests for this socket.  For client sockets, the timeout should not be
 * active if there are no requests pending on the socket.  Additionally, if a
 * client has multiple pending requests, it will usually want a separate
 * timeout for each request, rather than a single read timeout.
 *
 * The write API is fairly intuitive: a user can request to send a block of
 * data, and a callback will be informed once the entire block has been
 * transferred to the kernel, or on error.  AsyncSocket does provide a send
 * timeout, since most callers want to give up if the remote end stops
 * responding and no further progress can be made sending the data.
 */

/**
 *This is a @deprecated approach to disabling TTLS and should be
 *removed after completing the migration to FOLLY_SO_TTLS_TRUSTED.
 */
#if defined __linux__ && !defined FOLLY_SO_NO_TRANSPARENT_TLS
#define FOLLY_SO_NO_TRANSPARENT_TLS 200
#endif

#if defined __linux__ && !defined FOLLY_SO_TTLS_TRUSTED
#define FOLLY_SO_TTLS_TRUSTED 206
#endif

#if defined __linux__ && !defined FOLLY_SO_TTLS_TRUSTED_VAL_ENCRYPTED
#define FOLLY_SO_TTLS_TRUSTED_VAL_ENCRYPTED 1
#endif

#if defined __linux__ && !defined SO_NO_TSOCKS
#define SO_NO_TSOCKS 201
#endif

#if defined(FOLLY_HAVE_SO_TIMESTAMPING) && FOLLY_HAVE_SO_TIMESTAMPING
#define SO_MAX_ATTEMPTS_ENABLE_BYTEEVENTS 10
#endif

class AsyncSocket
    : public AsyncSocketTransport,
      public IoUringSendCallback,
      public IoUringRecvCallback,
      public IoUringConnectCallback {
 public:
  /// Owning smart pointer for an AsyncSocket, using its custom Destructor.
  using UniquePtr = std::unique_ptr<AsyncSocket, Destructor>;
  /// Byte-level timestamping event reported to observers.
  using ByteEvent = AsyncSocketObserverInterface::ByteEvent;
  /// Observer type for socket lifecycle and byte events.
  using Observer = AsyncSocketObserverContainer::Observer;
  /// Observer whose lifetime is managed by the observer container.
  using ManagedObserver = AsyncSocketObserverContainer::ManagedObserver;

  /// Maximum number of attempts made to enable byte events on the socket.
  static inline constexpr size_t kMaxAttemptsEnableByteEvents = 10;

  /// Callback notified when the socket's EventBase attachment changes.
  class EvbChangeCallback {
   public:
    /// Destroys the callback.
    virtual ~EvbChangeCallback() = default;

    /// Called when the socket has been attached to a new EVB
    /// and is called from within that EVB thread
    /// \param socket The socket that was attached to a new EventBase.
    virtual void evbAttached(AsyncSocket* socket) = 0;

    /// Called when the socket is detached from an EVB and
    /// is called from the EVB thread being detached
    /// \param socket The socket that was detached from its EventBase.
    virtual void evbDetached(AsyncSocket* socket) = 0;
  };

  /**
   * This interface is implemented only for platforms supporting
   * per-socket error queues.
   */
  class ErrMessageCallback {
   public:
    /// Destroys the callback.
    virtual ~ErrMessageCallback() = default;

    /**
     * errMessage() will be invoked when kernel puts a message to
     * the error queue associated with the socket.
     *
     * @param cmsg      Reference to cmsghdr structure describing
     *                  a message read from error queue associated
     *                  with the socket.
     */
    virtual void errMessage(const cmsghdr& cmsg) noexcept = 0;

    /**
     * errMessageError() will be invoked if an error occurs reading a message
     * from the socket error stream.
     *
     * @param ex        An exception describing the error that occurred.
     */
    virtual void errMessageError(const AsyncSocketException& ex) noexcept = 0;
  };

  /// Callback that receives ancillary data read alongside socket data.
  class ReadAncillaryDataCallback {
   public:
    /// Destroys the callback.
    virtual ~ReadAncillaryDataCallback() = default;

    /**
     * `ancillaryData()` is invoked immediately before the corresponding
     * `ReadCallback::readDataAvailable()`, as a pair.
     *
     * You must check for `msg_flags | MSG_CTRUNC`, indicating that some
     * ancillary data was discarded due to lack of space.  This is normally
     * not recoverable, so you can `close` or `failRead` the socket -- see
     * below.
     *
     * ## Allowed socket mutations ###
     *
     * This callback is allowed to `close`, `failRead` (for child classes),
     * or destruct the underlying socket.  It is **NOT** allowed to perform
     * any other mutations, such as `setReadCallback` or `attachEventBase`.
     *
     * If `ancillaryData()` closes or fails the socket, then any data
     * received in the same read as the ancillary data will NOT be delivered
     * to the `ReadCallback`.
     *
     * # Detailed contract
     *
     * This will only be invoked when a `ReadCallback` is installed -- i.e.
     * the socket is connected, neither closed nor in an error state.
     *
     * The supplied buffer will have originated from the most recent call to
     * `getAncillaryDataCtrlBuffer()`.
     *
     * Per POSIX, ancillary data are sent / received with the first byte of
     * the `sendmsg` data buffer, so we guarantee that the subsequent
     * `readDataAvailable()` (if it happens) will include that data byte.
     *
     * @param msg  Can be used with macros from `man cmsg` to access ancillary
     *         data. It is permissible to check `msg_flags & MSG_EOR`.
     *         There is NO CONTRACT about any other `msghdr` fields -- that
     *         is, choosing to read `msg_name*` or `msg_iov*` leads to
     *         undefined behavior.
     * @return Unit on success, or an AsyncSocketException on failure.
     */
    virtual folly::Expected<folly::Unit, AsyncSocketException> ancillaryData(
        struct ::msghdr& msg) noexcept = 0;

    /**
     * Must return a buffer large enough to contain the incoming ancillary
     * data, see `man cmsg` and `CMSG_SPACE`.
     *
     * DANGER: This call must not mutate the socket state.  e.g., you
     * cannot call setReadCB(), setReadAncillaryDataCB() or close()
     * from inside this call.
     *
     * If the supplied buffer is too small, your `ancillaryData()` will see
     * `MSG_CTRUNC`, and the kernel will have discarded some ancillary data.
     *
     * It is possible that `getAncillaryDataCtrlBuffer()` will be called
     * without a corresponding `ancillaryData()` call.  It is the callback's
     * responsibility not to leak the buffers it returns.  Any call to
     * `ancillaryData()` will use the most recently returned buffer.
     *
     * The returned buffer must remain valid until the point where
     * `ancillaryData()` could be called with it.  That is, previously
     * returned buffers may be freed if:
     *  - the socket is closed / fails, or its `ReadCallback` is removed
     *  - the `ReadAncillaryDataCallback` is uninstalled
     *  - `ancillaryData()` completes
     *
     * @return A buffer large enough to hold the incoming ancillary data.
     */
    virtual folly::MutableByteRange getAncillaryDataCtrlBuffer() = 0;
  };

  /**
   * Sometimes `SendMsgParamsCallback` needs to send different ancillary
   * data for different writes, for example when sending FDs over Unix
   * sockets.
   *
   * This opaque type acts as the key to match `writeChain` calls with
   * `getAncillaryData()` and corresponding `wroteBytes()` calls.  It wraps
   * `IOBuf*`, and implements equality, hashing, and ostream writes for
   * debugging.
   *
   * Important usage notes:
   *   - Even though `WriteRequestTag` never dereferences the pointer, it is
   *     still INCORRECT to use it after the write is over, whether or not
   *     the `IOBuf` had been destructed, because the same pointer could now
   *     refer to new, different data that is being written (see
   *     `getReleaseIOBufCallback` for the mechanism).
   *   - Therefore, if you store a `WriteRequestTag`, you must remove it
   *     whenever a write is complete.  This can be done either in
   *     `WriteCallback::{writeErr,writeSuccess}`, or by inheriting from
   *     `AsyncSocket::releaseIOBuf`, or by adding a new method
   *     `SendMsgParamsCallback::onReleaseIOBuf`.
   *   - Not all child classes support write tagging.  Notably, we removed
   *     the `AsyncSSLSocket` implementation since it added complexity and
   *     was not used.  Breadcrumbs are in `bioWrite`, or rev hash
   *     95df2ce7c98a.
   *   - The `EmptyDummy` constructor is for tests, or marking empty tags.
   *     `SendMsgParamsCallback` methods can also be called with an empty
   *     tag if the write is not submitted via `writeChain`.
   */
  struct WriteRequestTag {
    /// Tag type marking an empty tag, used for tests or unsubmitted writes.
    struct EmptyDummy {};

    /// Constructs an empty tag.
    /// \param dummy Marker selecting the empty-tag constructor.
    explicit WriteRequestTag(EmptyDummy dummy) : buf_(nullptr) {}
    /// Constructs a tag wrapping the given IOBuf pointer.
    /// \param buf The IOBuf submitted through `writeChain`.
    explicit WriteRequestTag(folly::IOBuf* buf) : buf_(buf) {}

    /// Compares two tags for equality by wrapped pointer.
    /// \param other The tag to compare against.
    /// \returns `true` if both tags wrap the same pointer.
    bool operator==(const WriteRequestTag& other) const {
      // Remember to also update std::hash<folly::AsyncSocket::WriteRequestTag>
      // and ostream operator<<
      return buf_ == other.buf_;
    }

    /// Check whether the tag is empty.
    /// \returns `true` if the tag wraps a null pointer.
    [[nodiscard]] bool empty() const noexcept { return buf_ == nullptr; }

   private:
    friend struct std::hash<WriteRequestTag>;
    /// Writes a textual representation of the tag to a stream.
    /// \param os The output stream to write to.
    /// \param tag The tag to write.
    /// \returns The output stream.
    friend std::ostream& operator<<(
        std::ostream& os, const WriteRequestTag& tag);

    // The `IOBuf` submitted through `writeChain`
    const folly::IOBuf* buf_;
  };

  /// Callback that supplies flags and ancillary data for `::sendmsg()` calls.
  class SendMsgParamsCallback {
   public:
    /// Destroys the callback.
    virtual ~SendMsgParamsCallback() = default;

    /**
     * getFlags() will be invoked to retrieve the desired flags to be passed
     * to ::sendmsg() system call. It is responsible for converting flags set in
     * the passed folly::WriteFlags enum into a integer flag bitmask that can be
     * passed to ::sendmsg. Some flags in folly::WriteFlags do not correspond to
     * flags that can be passed to ::sendmsg and may instead be handled via
     * getAncillaryData.
     *
     * This method was intentionally declared non-virtual, so there is no way to
     * override it. Instead feel free to override getFlagsImpl(...) instead, and
     * enjoy the convenience of defaultFlags passed there.
     *
     * @param flags     Write flags requested for the given write operation
     * @param zeroCopyEnabled  Whether zero-copy is enabled for this write.
     * @return Integer flag bitmask to pass to `::sendmsg()`.
     */
    int getFlags(folly::WriteFlags flags, bool zeroCopyEnabled) noexcept {
      return getFlagsImpl(flags, getDefaultFlags(flags, zeroCopyEnabled));
    }

    /**
     * getAncillaryData() will be invoked to initialize ancillary data buffer
     * referred by "msg_control" field of msghdr structure passed to ::sendmsg()
     * system call based on the flags set in the passed folly::WriteFlags enum.
     *
     * Some flags in folly::WriteFlags are not relevant during this process;
     * the default implementation only handles timestamping flags.
     *
     * The function requires that the size of buffer passed is equal to the
     * value returned by getAncillaryDataSize() method for the same combination
     * of flags.
     *
     * @param flags     Write flags requested for the given write operation
     * @param data      Pointer to ancillary data buffer to initialize.
     * @param writeTag  Documented on `WriteRequestTag`.
     * @param byteEventsEnabled      If byte events are enabled for this socket.
     *                               When enabled, flags relevant to socket
     *                               timestamps (e.g., TIMESTAMP_TX) should be
     *                               included in ancillary (msg_control) data.
     */
    virtual void getAncillaryData(
        folly::WriteFlags flags,
        void* data,
        const WriteRequestTag& writeTag,
        const bool byteEventsEnabled = false) noexcept;

    /**
     * getAncillaryDataSize() will be invoked to retrieve the size of
     * ancillary data buffer which should be passed to ::sendmsg() system call
     * The result must not exceed `maxAncillaryDataSize`.
     *
     * @param flags     Write flags requested for the given write operation
     * @param writeTag  Documented on `WriteRequestTag`.
     * @param byteEventsEnabled      If byte events are enabled for this socket.
     *                               When enabled, flags relevant to socket
     *                               timestamps (e.g., TIMESTAMP_TX) should be
     *                               included in ancillary (msg_control) data.
     * @return Size, in bytes, of the ancillary data buffer to allocate.
     */
    virtual uint32_t getAncillaryDataSize(
        folly::WriteFlags flags,
        const WriteRequestTag& writeTag,
        const bool byteEventsEnabled = false) noexcept;

    /**
     * Called immediately after a `sendmsg` corresponding to the preceding
     * `getAncillaryData()` successfully sends at least 1 byte.
     *
     * This is required to enable "exactly once" transmission of ancillary
     * data corresponding to `writeTag`.  For example, `AsyncFdSocket` ought
     * not transmit tag-associated FDs twice.  Per POSIX, ancillary data are
     * transmitted together with the first data byte.
     *
     * @param writeTag  Tag identifying the completed write.
     */
    virtual void wroteBytes(const WriteRequestTag& writeTag) noexcept {}

    // This is not an OS limitation (see `/proc/sys/net/core/optmem_max` on
    // Linux) but is done only because today's `AsyncSocket` implementation
    // uses `alloca` to allocate the ancillary data buffer on the stack in
    // order to support a cheap default `SendMsgParamsCallback` on every
    // socket.  If the buffer management could be handed to the socket (e.g.
    // each socket contains a few bytes of buffer for the default callback),
    // then we could delete this maximum, and `getAncillaryDataSize`, in
    // favor of `folly::ByteRange getAncillaryData()`.
    /// Maximum size, in bytes, of an ancillary data buffer.
    static const size_t maxAncillaryDataSize{0x5000};

   private:
    /**
     * getFlagsImpl() will be invoked by getFlags(folly::WriteFlags flags)
     * method to retrieve the flags to be passed to ::sendmsg() system call.
     * SendMsgParamsCallback::getFlags() is calling this method, and returns
     * its results directly to the caller in AsyncSocket.
     * Classes inheriting from SendMsgParamsCallback are welcome to override
     * this method to force SendMsgParamsCallback to return its own set
     * of flags.
     *
     * @param flags        Write flags requested for the given write operation
     * @param defaultflags A set of message flags returned by getDefaultFlags()
     *                     method for the given "flags" mask.
     */
    virtual int getFlagsImpl(folly::WriteFlags /*flags*/, int defaultFlags) {
      return defaultFlags;
    }

    /**
     * getDefaultFlags() will be invoked by  getFlags(folly::WriteFlags flags)
     * to retrieve the default set of flags, and pass them to getFlagsImpl(...)
     *
     * @param flags     Write flags requested for the given write operation
     */
    int getDefaultFlags(folly::WriteFlags flags, bool zeroCopyEnabled) noexcept;
  };

  /**
   * Container with state and processing logic for ByteEvents.
   */
  struct ByteEventHelper {
    bool byteEventsEnabled{false}; ///< Whether byte events are enabled.
    /// Raw bytes written when byte events were enabled.
    long rawBytesWrittenWhenByteEventsEnabled{0};
    folly::Optional<AsyncSocketException>
        maybeEx; ///< Exception recorded if processing failed.

    /**
     * Process a Cmsg and return a ByteEvent if available.
     *
     * The kernel will pass two cmsg for each timestamp:
     *   1. ScmTimestamping: Software / Hardware Timestamps.
     *   2. SockExtendedErrTimestamping: Byte offset associated with timestamp.
     *
     * These messages will be passed back-to-back; processCmsg() can handle them
     * in any order (1 then 2, or 2 then 1), as long the order is consistent
     * across timestamps.
     *
     * processCmsg() gracefully ignores Cmsg unrelated to socket timestamps, but
     * will throw if it receives a sequence of Cmsg that are not compliant with
     * its expectations.
     *
     * @param cmsg             The control message to process.
     * @param rawBytesWritten  Raw bytes written so far on the socket.
     *
     * @return If the helper has received all components required to generate a
     *         ByteEvent (e.g., ScmTimestamping and SockExtendedErrTimestamping
     *         messages), it returns a ByteEvent and clears its local state.
     *         Otherwise, returns an empty optional.
     *
     *         If the helper has previously thrown a ByteEventHelper::Exception,
     *         it will not process further Cmsg and will continuously return an
     *         empty optional.
     *
     * @throws ByteEventHelper::Exception If the helper receives a sequence of
     *         Cmsg that violate its expectations (e.g., multiple ScmTimestamping
     *         messages in a row without corresponding SockExtendedErrTimestamping
     *         messages). Subsequent calls will return an empty optional.
     */
    folly::Optional<ByteEvent> processCmsg(
        const cmsghdr& cmsg, const size_t rawBytesWritten);

    /**
     * Exception class thrown by processCmsg.
     *
     * ByteEventHelper does not know the socket address and thus cannot
     * construct a AsyncSocketException. Instead, ByteEventHelper throws a
     * custom Exception and AsyncSocket rewraps it as an AsyncSocketException.
     */
    class Exception : public std::runtime_error {
      using std::runtime_error::runtime_error;
    };

   private:
    // state, reinitialized each time a complete timestamp is processed
    struct TimestampState {
      bool serrReceived{false};
      uint32_t typeRaw{0};
      uint32_t byteOffsetKernel{0};

      bool scmTsReceived{false};
      folly::Optional<std::chrono::nanoseconds> maybeSoftwareTs;
      folly::Optional<std::chrono::nanoseconds> maybeHardwareTs;
    };
    folly::Optional<TimestampState> maybeTsState_;
  };

  /// Create a new AsyncSocket that is not bound to any EventBase.
  explicit AsyncSocket();
  /**
   * Create a new unconnected AsyncSocket.
   *
   * connect() must later be called on this socket to establish a connection.
   *
   * @param evb  EventBase that will manage this socket.
   */
  explicit AsyncSocket(EventBase* evb);

  /// Set the ShutdownSocketSet that this socket belongs to.
  /// \param wSS  Weak pointer to the ShutdownSocketSet to register with.
  void setShutdownSocketSet(const std::weak_ptr<ShutdownSocketSet>& wSS);

  /**
   * Create a new AsyncSocket and begin the connection process.
   *
   * @param evb             EventBase that will manage this socket.
   * @param address         The address to connect to.
   * @param connectTimeout  Optional timeout in milliseconds for the connection
   *                        attempt.
   * @param useZeroCopy     Optional zerocopy socket mode
   */
  AsyncSocket(
      EventBase* evb,
      const folly::SocketAddress& address,
      uint32_t connectTimeout = 0,
      bool useZeroCopy = false);

  /**
   * Create a new AsyncSocket and begin the connection process.
   *
   * @param evb             EventBase that will manage this socket.
   * @param ip              IP address to connect to (dotted-quad).
   * @param port            Destination port in host byte order.
   * @param connectTimeout  Optional timeout in milliseconds for the connection
   *                        attempt.
   * @param useZeroCopy     Optional zerocopy socket mode
   */
  AsyncSocket(
      EventBase* evb,
      const std::string& ip,
      uint16_t port,
      uint32_t connectTimeout = 0,
      bool useZeroCopy = false);

  /**
   * Create a AsyncSocket from an already connected socket file descriptor.
   *
   * Note that while AsyncSocket enables TCP_NODELAY for sockets it creates
   * when connecting, it does not change the socket options when given an
   * existing file descriptor.  If callers want TCP_NODELAY enabled when using
   * this version of the constructor, they need to explicitly call
   * setNoDelay(true) after the constructor returns.
   *
   * @param evb            EventBase that will manage this socket.
   * @param fd             File descriptor to take over (connected socket).
   * @param zeroCopyBufId  Zerocopy buf id to start with.
   * @param peerAddress    Optional peer address (eg: returned from accept). If
   *                       nullptr, AsyncSocket will lazily attempt to determine
   *                       it from fd via a system call.
   * @param maybeConnectionEstablishTime  Optional parameter indicating when the
   *                                      connection was established. Can be
   *                                      used by acceptors to record when a
   *                                      connection was established and make
   *                                      this information available via the
   *                                      getConnectionEstablishTime() method.
   */
  AsyncSocket(
      EventBase* evb,
      NetworkSocket fd,
      uint32_t zeroCopyBufId = 0,
      const SocketAddress* peerAddress = nullptr,
      folly::Optional<std::chrono::steady_clock::time_point>
          maybeConnectionEstablishTime = folly::none);

  /**
   * Create an AsyncSocket from a different, already connected AsyncSocket.
   *
   * Similar to AsyncSocket(evb, fd) when fd was previously owned by an
   * AsyncSocket.
   *
   * @param oldAsyncSocket  The socket to take ownership of.
   */
  explicit AsyncSocket(AsyncSocket::UniquePtr oldAsyncSocket);

  /**
   * Create an AsyncSocket from a different, already connected AsyncSocket.
   *
   * Similar to AsyncSocket(evb, fd) when fd was previously owned by an
   * AsyncSocket. Caller must call destroy on old AsyncSocket unless it is
   * in a smart pointer with appropriate destructor.
   *
   * @param oldAsyncSocket  The socket to take over the connection from.
   */
  explicit AsyncSocket(AsyncSocket* oldAsyncSocket);

  /**
   * Helper function to create an AsyncSocket..
   *
   * This passes in the correct destructor object, since AsyncSocket's
   * destructor is protected and cannot be invoked directly.
   *
   * @param evb  EventBase that will manage this socket.
   * @return A UniquePtr owning the new socket.
   */
  static UniquePtr newSocket(EventBase* evb) {
    return UniquePtr{new AsyncSocket(evb)};
  }

  /**
   * Helper function to create an AsyncSocket.
   *
   * @param evb             EventBase that will manage this socket.
   * @param address         The address to connect to.
   * @param connectTimeout  Optional timeout in milliseconds for the connection.
   * @param useZeroCopy     Optional zerocopy socket mode.
   * @return A UniquePtr owning the new socket.
   */
  static UniquePtr newSocket(
      EventBase* evb,
      const folly::SocketAddress& address,
      uint32_t connectTimeout = 0,
      bool useZeroCopy = false) {
    return UniquePtr{
        new AsyncSocket(evb, address, connectTimeout, useZeroCopy)};
  }

  /**
   * Helper function to create an AsyncSocket.
   *
   * @param evb             EventBase that will manage this socket.
   * @param ip              IP address to connect to (dotted-quad).
   * @param port            Destination port in host byte order.
   * @param connectTimeout  Optional timeout in milliseconds for the connection.
   * @param useZeroCopy     Optional zerocopy socket mode.
   * @return A UniquePtr owning the new socket.
   */
  static UniquePtr newSocket(
      EventBase* evb,
      const std::string& ip,
      uint16_t port,
      uint32_t connectTimeout = 0,
      bool useZeroCopy = false) {
    return UniquePtr{
        new AsyncSocket(evb, ip, port, connectTimeout, useZeroCopy)};
  }

  /**
   * Helper function to create an AsyncSocket.
   *
   * @param evb          EventBase that will manage this socket.
   * @param fd           File descriptor to take over (connected socket).
   * @param peerAddress  Optional peer address of the connected socket.
   * @return A UniquePtr owning the new socket.
   */
  static UniquePtr newSocket(
      EventBase* evb,
      NetworkSocket fd,
      const SocketAddress* peerAddress = nullptr) {
    return UniquePtr{new AsyncSocket(evb, fd, 0, peerAddress)};
  }

  /**
   * Destroy the socket.
   *
   * AsyncSocket::destroy() must be called to destroy the socket.
   * The normal destructor is private, and should not be invoked directly.
   * This prevents callers from deleting a AsyncSocket while it is invoking a
   * callback.
   */
  void destroy() override;

  /**
   * Get the EventBase used by this socket.
   *
   * @return The EventBase managing this socket.
   */
  EventBase* getEventBase() const override { return eventBase_; }

  /**
   * Get the network socket used by the AsyncSocket.
   *
   * @return The underlying network socket.
   */
  NetworkSocket getNetworkSocket() const override { return fd_; }

  /**
   * Extract the file descriptor from the AsyncSocket.
   *
   * This will immediately cause any installed callbacks to be invoked with an
   * error.  The AsyncSocket may no longer be used after the file descriptor
   * has been extracted.
   *
   * This method should be used with care as the resulting fd is not guaranteed
   * to perfectly reflect the state of the AsyncSocket (security state,
   * pre-received data, etc.).
   *
   * Returns the file descriptor.  The caller assumes ownership of the
   * descriptor, and it will not be closed when the AsyncSocket is destroyed.
   *
   * @return The detached file descriptor, now owned by the caller.
   */
  virtual NetworkSocket detachNetworkSocket();

  /**
   * Initiate a connection.
   *
   * @param callback  The callback to inform when the connection attempt
   *                  completes.
   * @param address   The address to connect to.
   * @param timeout   A timeout value, in milliseconds.  If the connection
   *                  does not succeed within this period,
   *                  callback->connectError() will be invoked.
   * @param options   Socket options to apply during the connection.
   * @param bindOptions Either a SocketAddress to bind to, or a NetworkSocket
   *                  with an address already bound to it via bind().
   *                  Ownership of a NetworkSocket is transferred from the
   *                  caller to this AsyncSocket.
   * @param ifName    Name of the network interface to bind to, if any.
   */
  virtual void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      int timeout = 0,
      const SocketOptionMap& options = emptySocketOptionMap,
      const BindOptions& bindOptions = anyAddress(),
      const std::string& ifName = "") noexcept override;

  /// Initiate a connection to the given IP and port.
  /// \param callback  The callback to inform when the attempt completes.
  /// \param ip        The IP address to connect to (dotted-quad).
  /// \param port      Destination port in host byte order.
  /// \param timeout   Timeout in milliseconds; 0 means no timeout.
  /// \param options   Socket options to apply during the connection.
  void connect(
      ConnectCallback* callback,
      const std::string& ip,
      uint16_t port,
      int timeout = 0,
      const SocketOptionMap& options = emptySocketOptionMap) noexcept;

  /**
   * If a connect request is in-flight, cancels it and closes the socket
   * immediately. Otherwise, this is a no-op.
   *
   * This does not invoke any connection related callbacks. Call this to
   * prevent any connect callback while cleaning up, etc.
   */
  void cancelConnect() override;

  /**
   * Set the send timeout.
   *
   * If write requests do not make any progress for more than the specified
   * number of milliseconds, fail all pending writes and close the socket.
   *
   * If write requests are currently pending when setSendTimeout() is called,
   * the timeout interval is immediately restarted using the new value.
   *
   * (See the comments for AsyncSocket for an explanation of why AsyncSocket
   * provides setSendTimeout() but not setRecvTimeout().)
   *
   * @param milliseconds  The timeout duration, in milliseconds.  If 0, no
   *                      timeout will be used.
   */
  void setSendTimeout(uint32_t milliseconds) override;

  /**
   * Get the send timeout.
   *
   * @return Returns the current send timeout, in milliseconds.  A return value
   *         of 0 indicates that no timeout is set.
   */
  uint32_t getSendTimeout() const override { return sendTimeout_; }

  /**
   * Set the maximum number of reads to execute from the underlying
   * socket each time the EventBase detects that new ingress data is
   * available. The default is unlimited, but callers can use this method
   * to limit the amount of data read from the socket per event loop
   * iteration.
   *
   * @param maxReads  Maximum number of reads per data-available event;
   *                  a value of zero means unlimited.
   */
  void setMaxReadsPerEvent(uint16_t maxReads) { maxReadsPerEvent_ = maxReads; }

  /**
   * Get the maximum number of reads this object will execute from
   * the underlying socket each time the EventBase detects that new
   * ingress data is available.
   *
   * @returns Maximum number of reads per data-available event; a value
   *          of zero means unlimited.
   */
  uint16_t getMaxReadsPerEvent() const { return maxReadsPerEvent_; }

  /**
   * Set a pointer to ErrMessageCallback implementation which will be
   * receiving notifications for messages posted to the error queue
   * associated with the socket.
   * ErrMessageCallback is implemented only for platforms with
   * per-socket error message queues support (recvmsg() system call must
   * )
   *
   * @param callback  The error message callback to install.
   */
  virtual void setErrMessageCB(ErrMessageCallback* callback);

  /**
   * Get a pointer to ErrMessageCallback implementation currently
   * registered with this socket.
   *
   * @return The currently registered error message callback.
   */
  virtual ErrMessageCallback* getErrMessageCallback() const;

  /**
   * Set a pointer to ReadAncillaryDataCallback implementation which will
   * be invoked with the ancillary data when we read a buffer from the
   * associated socket.
   * ReadAncillaryDataCallback is implemented only for platforms with
   * kernel timestamp support.
   *
   * @param callback  The ancillary data callback to install.
   */
  virtual void setReadAncillaryDataCB(ReadAncillaryDataCallback* callback);

  /**
   * Get a pointer to ReadAncillaryDataCallback implementation currently
   * registered with this socket.
   *
   * @return The currently registered ancillary data callback.
   */
  virtual ReadAncillaryDataCallback* getReadAncillaryDataCallback() const;

  /**
   * Set a pointer to SendMsgParamsCallback implementation which
   * will be used to form ::sendmsg() system call parameters
   *
   * @param callback  The send message params callback to install.
   */
  virtual void setSendMsgParamCB(SendMsgParamsCallback* callback);

  /**
   * Get a pointer to SendMsgParamsCallback implementation currently
   * registered with this socket.
   *
   * @return The currently registered send message params callback.
   */
  virtual SendMsgParamsCallback* getSendMsgParamsCB() const;

  /**
   * Override netops::Dispatcher to be used for netops:: calls.
   *
   * Pass empty shared_ptr to reset to default.
   * Override can be used by unit tests to intercept and mock netops:: calls.
   *
   * @param dispatcher  The dispatcher to use, or empty to reset to default.
   */
  virtual void setOverrideNetOpsDispatcher(
      std::shared_ptr<netops::Dispatcher> dispatcher) {
    netops_.setOverride(std::move(dispatcher));
  }

  /**
   * Returns override netops::Dispatcher being used for netops:: calls.
   *
   * Returns empty shared_ptr if no override set.
   * Override can be used by unit tests to intercept and mock netops:: calls.
   *
   * @return The override dispatcher, or empty if none is set.
   */
  virtual std::shared_ptr<netops::Dispatcher> getOverrideNetOpsDispatcher()
      const {
    return netops_.getOverride();
  }

  /**
   * Override folly::TcpInfoDispatcher to be used for getting TcpInfo.
   *
   * Pass empty shared_ptr to reset to default.
   * Override can be used by unit tests to intercept and mock
   * TcpInfo::initFromFd calls.
   *
   * @param dispatcher  The dispatcher to use, or empty to reset to default.
   */
  virtual void setOverrideTcpInfoDispatcher(
      std::shared_ptr<folly::TcpInfoDispatcher> dispatcher) {
    tcpInfoDispatcher_.setOverride(std::move(dispatcher));
  }

  /**
   * Returns override folly::TcpInfoDispatcher being used for tcpinfo calls.
   *
   * Returns empty shared_ptr if no override set.
   * Override can be used by unit tests to intercept and mock
   * TcpInfo::initFromFd calls.
   *
   * @return The override dispatcher, or empty if none is set.
   */
  virtual std::shared_ptr<folly::TcpInfoDispatcher>
  getOverrideTcpInfoDispatcher() const {
    return tcpInfoDispatcher_.getOverride();
  }

  // Read and write methods

  /// Install the read callback that receives data read from the socket.
  /// \param callback  The read callback to install.
  void setReadCB(ReadCallback* callback) override;
  /// Get the currently installed read callback.
  /// \returns The currently installed read callback.
  ReadCallback* getReadCallback() const override;

  /**
   * Create a memory store to use for zero copy reads.
   *
   * The memory store contains a fixed number of entries, each with a fixed
   * size.  When data is read using zero-copy the kernel will place it in one
   * of these entries, and it will be returned to the callback with
   * readZeroCopyDataAvailable().  The callback must release the IOBuf
   * reference to make the entry available again for future zero-copy reads.
   * If all entries are exhausted the read code will fall back to non-zero-copy
   * reads.
   *
   * Note: it is the caller's responsibility to ensure that they do not destroy
   * the ZeroCopyMemStore while it still has any outstanding entries in use.
   * The caller must ensure the ZeroCopyMemStore exists until all callers have
   * finished using any data returned via zero-copy reads, and released the
   * IOBuf objects containing that data.
   *
   * @param entries  The number of entries to allocate in the memory store.
   * @param size     The size of each entry, in bytes.  This should be a
   *                 multiple of the kernel page size.
   * @return A memory store for use with zero-copy reads.
   */

  static std::unique_ptr<AsyncReader::ReadCallback::ZeroCopyMemStore>
  createDefaultZeroCopyMemStore(size_t entries, size_t size);

  /// Enable or disable zero-copy writes on this socket.
  /// \param enable  Whether to enable zero-copy writes.
  /// \returns `true` if zero-copy was successfully configured.
  bool setZeroCopy(bool enable) override;
  /// Get whether zero-copy writes are enabled.
  /// \returns Whether zero-copy writes are enabled.
  bool getZeroCopy() const override { return zeroCopyEnabled_; }

  /// Get the current zero-copy buffer id.
  /// \returns The current zero-copy buffer id.
  uint32_t getZeroCopyBufId() const { return zeroCopyBufId_; }

  // Transfers outstanding zero-copy write completion state from `other` (whose
  // fd must already be detached) to this socket during an fd handoff.
  /// Transfer outstanding zero-copy write completion state from another socket.
  /// \param other  The socket whose zero-copy state is moved from.
  void moveZeroCopyStateFrom(AsyncSocket& other);

  /// Get the threshold at which zero-copy is re-enabled.
  /// \returns The threshold at which zero-copy is re-enabled.
  size_t getZeroCopyReenableThreshold() const {
    return zeroCopyReenableThreshold_;
  }

  /// Set the function used to decide whether a write should use zero-copy.
  /// \param func  The zero-copy enable predicate.
  void setZeroCopyEnableFunc(AsyncWriter::ZeroCopyEnableFunc func) override;

  /// Set the minimum write size for which zero-copy is used.
  /// \param threshold  The zero-copy size threshold, in bytes.
  void setZeroCopyEnableThreshold(size_t threshold) override;

  /// Set the threshold at which zero-copy is re-enabled after being disabled.
  /// \param threshold  The zero-copy re-enable threshold.
  void setZeroCopyReenableThreshold(size_t threshold);

  /// Configuration for draining outstanding zero-copy completions on close.
  struct ZeroCopyDrainConfig {
    /// Delay between drain attempts.
    std::chrono::milliseconds drainDelay{1000};
    /// Upper bound on total drain time.
    ///
    /// Upper bound on how long the socket will stay alive polling MSG_ERRQUEUE
    /// for outstanding zero-copy completions. If the kernel has not returned
    /// them by then the buffers are released anyway, so a socket whose
    /// completions never arrive cannot pin an fd and its IOBufs forever.
    std::chrono::milliseconds maxDrainDuration{std::chrono::minutes(5)};
    std::optional<unsigned short> linger; ///< Optional SO_LINGER value.
  };

  /// Set the zero-copy drain configuration used on close.
  /// \param config  The drain configuration to apply.
  void setZeroCopyDrainConfig(const ZeroCopyDrainConfig& config);

  /// Write a buffer to the socket.
  /// \param callback  Write completion/error callback.
  /// \param buf       Pointer to the data to write.
  /// \param bytes     Number of bytes to write.
  /// \param flags     Set of write flags.
  void write(
      WriteCallback* callback,
      const void* buf,
      size_t bytes,
      WriteFlags flags = WriteFlags::NONE) override;
  /// Write an iovec array to the socket.
  /// \param callback  Write completion/error callback.
  /// \param vec       Array of buffers to write.
  /// \param count     Number of elements in `vec`.
  /// \param flags     Set of write flags.
  void writev(
      WriteCallback* callback,
      const iovec* vec,
      size_t count,
      WriteFlags flags = WriteFlags::NONE) override;
  /// Write an IOBuf chain to the socket.
  /// \param callback  Write completion/error callback.
  /// \param buf       The IOBuf chain to write; ownership is transferred.
  /// \param flags     Set of write flags.
  void writeChain(
      WriteCallback* callback,
      std::unique_ptr<folly::IOBuf>&& buf,
      WriteFlags flags = WriteFlags::NONE) override;

  class WriteRequest;
  /// Enqueue a pending write request on the socket.
  /// \param req  The write request to enqueue.
  virtual void writeRequest(WriteRequest* req);
  /// Notify the socket that a write request is ready to be processed.
  void writeRequestReady() { handleWrite(); }

  // Methods inherited from AsyncTransport

  /// Close the socket after pending writes complete.
  void close() override;
  /// Close the socket immediately, failing any pending writes.
  void closeNow() override;
  /// Close the socket by sending a TCP reset.
  void closeWithReset() override;
  /// Shut down the write half of the socket after pending writes complete.
  void shutdownWrite() override;
  /// Shut down the write half of the socket immediately.
  void shutdownWriteNow() override;

  /// Check whether the socket has readable data available.
  /// \returns Whether the socket has readable data available.
  bool readable() const override;
  /// Check whether the socket can accept writes without blocking.
  /// \returns Whether the socket can accept writes without blocking.
  bool writable() const override;
  /// Check whether a connection attempt is still pending.
  /// \returns Whether a connection attempt is still pending.
  bool isPending() const override;
  /// Check whether the peer has hung up the connection.
  /// \returns Whether the peer has hung up the connection.
  bool hangup() const override;
  /// Check whether the socket is in a good (usable) state.
  /// \returns Whether the socket is in a good (usable) state.
  bool good() const override;
  /// Check whether the socket is in an error state.
  /// \returns Whether the socket is in an error state.
  bool error() const override;
  /// Attach the socket to an EventBase.
  /// \param eventBase  The EventBase to attach to.
  void attachEventBase(EventBase* eventBase) override;
  /// Detach the socket from its current EventBase.
  void detachEventBase() override;
  /// Check whether the socket can currently be detached from its EventBase.
  /// \returns Whether the socket can currently be detached from its EventBase.
  bool isDetachable() const override;

  /// Get the local address of the socket.
  /// \param address  Out-parameter set to the local address.
  void getLocalAddress(folly::SocketAddress* address) const override;
  /// Get the peer address of the socket.
  /// \param address  Out-parameter set to the peer address.
  void getPeerAddress(folly::SocketAddress* address) const override;

  /// Check whether end-of-record tracking is enabled.
  /// \returns Whether end-of-record tracking is enabled.
  bool isEorTrackingEnabled() const override { return trackEor_; }

  /// Enable or disable end-of-record tracking.
  /// \param track  Whether to enable EOR tracking.
  void setEorTracking(bool track) override { trackEor_ = track; }

  /// Check whether a connection attempt is currently in progress.
  /// \returns Whether a connection attempt is currently in progress.
  bool connecting() const override { return (state_ == StateEnum::CONNECTING); }

  /// Check whether the socket was closed due to a peer-initiated event.
  /// \returns Whether the socket was closed due to a peer-initiated event.
  virtual bool isClosedByPeer() const {
    return (
        state_ == StateEnum::CLOSED &&
        (readErr_ == READ_EOF || readErr_ == READ_ERROR));
  }

  /// Check whether the socket was closed by the local side rather than peer.
  /// \returns Whether the socket was closed by the local side rather than peer.
  virtual bool isClosedBySelf() const {
    return (
        state_ == StateEnum::CLOSED &&
        (readErr_ != READ_EOF && readErr_ != READ_ERROR));
  }

  /// Get the number of application bytes written to the socket.
  /// \returns Number of application bytes written to the socket.
  size_t getAppBytesWritten() const override { return appBytesWritten_; }

  /// Get the number of raw bytes written to the socket.
  /// \returns Number of raw bytes written to the socket.
  size_t getRawBytesWritten() const override { return rawBytesWritten_; }

  /// Get the number of application bytes received from the socket.
  /// \returns Number of application bytes received from the socket.
  size_t getAppBytesReceived() const override { return appBytesReceived_; }

  /// Get the number of raw bytes received from the socket.
  /// \returns Number of raw bytes received from the socket.
  size_t getRawBytesReceived() const override { return getAppBytesReceived(); }

  /// Get the number of application bytes buffered pending write.
  /// \returns Number of application bytes buffered pending write.
  size_t getAppBytesBuffered() const override {
    return totalAppBytesScheduledForWrite_ - appBytesWritten_;
  }
  /// Get the number of raw bytes buffered pending write.
  /// \returns Number of raw bytes buffered pending write.
  size_t getRawBytesBuffered() const override { return getAppBytesBuffered(); }

  /// Get the number of bytes allocated in buffers pending write.
  /// \returns Number of bytes allocated in buffers pending write.
  size_t getAllocatedBytesBuffered() const override {
    return allocatedBytesBuffered_;
  }

  // End of methods inherited from AsyncTransport

  /// Get the elapsed time of the connection attempt.
  /// \returns The elapsed time of the connection attempt.
  std::chrono::nanoseconds getConnectTime() const {
    return connectEndTime_ - connectStartTime_;
  }

  /// Get the configured connection timeout.
  /// \returns The configured connection timeout.
  std::chrono::milliseconds getConnectTimeout() const {
    return connectTimeout_;
  }

  /**
   * Returns when connect() started.
   *
   * @return The time point when connect() started.
   */
  std::chrono::steady_clock::time_point getConnectStartTime() const {
    return connectStartTime_;
  }

  /**
   * Returns when connect() finished (either successfully or failed).
   *
   * @return The time point when connect() finished.
   */
  std::chrono::steady_clock::time_point getConnectEndTime() const {
    return connectEndTime_;
  }

  /**
   * Returns when the connection was established.
   *
   *  -  If connect() was called and succeeded, this is the same as
   *     getConnectEndTime().
   *
   *  -  If AsyncSocket was initialized with a file descriptor (e.g., by an
   *     acceptor), returns the connection establishment time passed to the
   *     constructor. If no time was passed, returns folly::none.
   *
   * @return The connection establishment time, or folly::none if unknown.
   */
  folly::Optional<std::chrono::steady_clock::time_point>
  getConnectionEstablishTime() const {
    return maybeConnectionEstablishTime_;
  }

  /// Get whether an attempt to use TCP Fast Open was made.
  /// \returns Whether an attempt to use TCP Fast Open was made.
  bool getTFOAttempted() const { return tfoInfo_.attempted; }

  /**
   * Returns whether or not the attempt to use TFO
   * finished successfully. This does not necessarily
   * mean TFO worked, just that trying to use TFO
   * succeeded.
   *
   * @return Whether the attempt to use TFO finished.
   */
  bool getTFOFinished() const { return tfoInfo_.finished; }

  /**
   * Returns whether or not TFO attempt succeeded on this
   * connection.
   * For servers this is pretty straightforward API and can
   * be invoked right after the connection is accepted. This API
   * will perform one syscall.
   * This API is a bit tricky to use for clients, since clients
   * only know this for sure after the SYN-ACK is returned. So it's
   * appropriate to call this only after the first application
   * data is read from the socket when the caller knows that
   * the SYN has been ACKed by the server.
   *
   * @return Whether the TFO attempt succeeded on this connection.
   */
  bool getTFOSucceeded() const override;

  // Methods controlling socket options

  /**
   * Force writes to be transmitted immediately.
   *
   * This controls the TCP_NODELAY socket option.  When enabled, TCP segments
   * are sent as soon as possible, even if it is not a full frame of data.
   * When disabled, the data may be buffered briefly to try and wait for a full
   * frame of data.
   *
   * By default, TCP_NODELAY is enabled for AsyncSocket objects.
   *
   * This method will fail if the socket is not currently open.
   *
   * @param noDelay  Whether to enable TCP_NODELAY.
   * @return Returns 0 if the TCP_NODELAY flag was successfully updated,
   *         or a non-zero errno value on error.
   */
  int setNoDelay(bool noDelay) override;

  /**
   * Set the FD_CLOEXEC flag so that the socket will be closed if the program
   * later forks and execs.
   */
  void setCloseOnExec();

  /**
   * Set the Flavor of Congestion Control to be used for this Socket
   * Please check '/lib/modules/<kernel>/kernel/net/ipv4' for tcp_*.ko
   * first to make sure the module is available for plugging in
   * Alternatively you can choose from net.ipv4.tcp_allowed_congestion_control
   *
   * @param cname  Name of the congestion control algorithm to use.
   * @return Returns 0 on success, or a non-zero errno value on error.
   */
  int setCongestionFlavor(const std::string& cname);

  /**
   * Forces ACKs to be sent immediately
   *
   * @param quickack  Whether to enable TCP_QUICKACK.
   * @return Returns 0 if the TCP_QUICKACK flag was successfully updated,
   *         or a non-zero errno value on error.
   */
  int setQuickAck(bool quickack);

  /**
   * Set the send bufsize
   *
   * @param bufsize  The send buffer size, in bytes.
   * @return Returns 0 on success, or a non-zero errno value on error.
   */
  int setSendBufSize(size_t bufsize);

  /**
   * Set the recv bufsize
   *
   * @param bufsize  The receive buffer size, in bytes.
   * @return Returns 0 on success, or a non-zero errno value on error.
   */
  int setRecvBufSize(size_t bufsize);

#if defined(__linux__)
  /**
   * @brief This method is used to get the number of bytes that are currently
   *        stored in the TCP send/tx buffer
   *
   * @return the number of bytes in the send/tx buffer or folly::none if there
   *         was a problem
   */
  size_t getSendBufInUse() const;

  /**
   * @brief This method is used to get the number of bytes that are currently
   *        stored in the TCP receive/rx buffer
   *
   * @return the number of bytes in the receive/rx buffer or folly::none if
   *         there was a problem
   */
  size_t getRecvBufInUse() const;
#endif

/**
 * Sets a specific tcp personality
 * Available only on kernels 3.2 and greater
 */
#define SO_SET_NAMESPACE 41
  /// Set a specific TCP personality (kernels 3.2 and greater only).
  /// \param profd  The TCP profile descriptor to apply.
  /// \returns Returns 0 on success, or a non-zero errno value on error.
  int setTCPProfile(int profd);

  /**
   * Generic API for reading a socket option.
   *
   * @param level     same as the "level" parameter in getsockopt().
   * @param optname   same as the "optname" parameter in getsockopt().
   * @param optval    pointer to the variable in which the option value should
   *                  be returned.
   * @param optlen    value-result argument, initially containing the size of
   *                  the buffer pointed to by optval, and modified on return
   *                  to indicate the actual size of the value returned.
   * @return          same as the return value of getsockopt().
   */
  template <typename T>
  int getSockOpt(int level, int optname, T* optval, socklen_t* optlen) {
    return netops_->getsockopt(fd_, level, optname, (void*)optval, optlen);
  }

  /**
   * Generic API for setting a socket option.
   *
   * @param level     same as the "level" parameter in getsockopt().
   * @param optname   same as the "optname" parameter in getsockopt().
   * @param optval    the option value to set.
   * @return          same as the return value of setsockopt().
   */
  template <typename T>
  int setSockOpt(int level, int optname, const T* optval) {
    return netops_->setsockopt(fd_, level, optname, optval, sizeof(T));
  }

  /// Set a socket option from a raw buffer.
  /// \param level    Same as the "level" parameter in setsockopt().
  /// \param optname  Same as the "optname" parameter in setsockopt().
  /// \param optval   Pointer to the option value to set.
  /// \param optsize  Size, in bytes, of the value pointed to by optval.
  /// \returns Same as the return value of setsockopt().
  int setSockOpt(
      int level, int optname, const void* optval, socklen_t optsize) override {
    return netops_->setsockopt(fd_, level, optname, optval, optsize);
  }

  /**
   * Virtual method for reading a socket option returning integer
   * value, which is the most typical case. Convenient for overriding
   * and mocking.
   *
   * @param level     same as the "level" parameter in getsockopt().
   * @param optname   same as the "optname" parameter in getsockopt().
   * @param optval    same as "optval" parameter in getsockopt().
   * @param optlen    same as "optlen" parameter in getsockopt().
   * @return          same as the return value of getsockopt().
   */
  virtual int getSockOptVirtual(
      int level, int optname, void* optval, socklen_t* optlen) {
    return netops_->getsockopt(fd_, level, optname, optval, optlen);
  }

  /**
   * Virtual method for setting a socket option accepting integer
   * value, which is the most typical case. Convenient for overriding
   * and mocking.
   *
   * @param level     same as the "level" parameter in setsockopt().
   * @param optname   same as the "optname" parameter in setsockopt().
   * @param optval    same as "optval" parameter in setsockopt().
   * @param optlen    same as "optlen" parameter in setsockopt().
   * @return          same as the return value of setsockopt().
   */
  virtual int setSockOptVirtual(
      int level, int optname, void const* optval, socklen_t optlen) {
    return netops_->setsockopt(fd_, level, optname, optval, optlen);
  }

  /**
   * Set pre-received data, to be returned to read callback before any data
   * from the socket.
   *
   * @param data  The pre-received data to buffer.
   */
  void setPreReceivedData(std::unique_ptr<IOBuf> data) override {
    if (preReceivedData_) {
      preReceivedData_->prependChain(std::move(data));
    } else {
      preReceivedData_ = std::move(data);
    }
  }

  /// Take ownership of any buffered pre-received data.
  /// \returns The pre-received data, or nullptr if none is buffered.
  std::unique_ptr<IOBuf> takePreReceivedData() override {
    return std::move(preReceivedData_);
  }

  /**
   * Enables TFO behavior on the AsyncSocket if FOLLY_ALLOW_TFO
   * is set.
   */
  void enableTFO() override {
    // No-op if folly does not allow tfo
#if defined(FOLLY_ALLOW_TFO) && FOLLY_ALLOW_TFO
    tfoInfo_.enabled = true;
#endif
  }

  /**
   * Sets TOS or traffic class. Throws an exception on error.
   *
   * @param tosOrTrafficClass  The TOS (IPv4) or traffic class (IPv6) to set.
   */
  void setTosOrTrafficClass(int tosOrTrafficClass);

  /**
   * This flag controls whether or not IP_BIND_ADDRESS_NO_PORT is enabled for
   * AsyncSocket sockets. This is enabled by default.
   *
   * @param flag  Whether to enable IP_BIND_ADDRESS_NO_PORT.
   */
  void setBindAddressNoPort(bool flag) { bindAddressNoPort_ = flag; }

  /// Disable transparent TLS for this socket.
  void disableTransparentTls() override { noTransparentTls_ = true; }

  /// Disable transparent SOCKS proxying for this socket.
  void disableTSocks() { noTSocks_ = true; }

  /// State of the socket's connection lifecycle.
  enum class StateEnum : uint8_t {
    UNINIT, ///< Not yet initialized.
    CONNECTING, ///< A connection attempt is in progress.
    ESTABLISHED, ///< The connection is established.
    CLOSED, ///< The socket is closed.
    ERROR, ///< The socket is in an error state.
    FAST_OPEN, ///< The socket is in TCP Fast Open state.
  };

  /// Install a callback notified about buffered write events.
  /// \param cb  The buffer callback to install.
  void setBufferCallback(BufferCallback* cb);

  // Callers should set this prior to connecting the socket for the safest
  // behavior.
  /// Install a callback notified when the socket's EventBase changes.
  /// \param cb  The EventBase-change callback to install.
  void setEvbChangedCallback(std::unique_ptr<EvbChangeCallback> cb) {
    evbChangeCb_ = std::move(cb);
  }

  /**
   * Attempt to cache the current local and peer addresses (if not already
   * cached) so that they are available from getPeerAddress() and
   * getLocalAddress() even after the socket is closed.
   */
  void cacheAddresses() override;

  /**
   * Returns true if there is any zero copy write in progress
   * Needs to be called from within the socket's EVB thread
   *
   * @return Whether any zero-copy write is in progress.
   */
  bool isZeroCopyWriteInProgress() const noexcept;

  /**
   * Tries to process the msg error queue
   * And returns true if there are no more zero copy writes in progress
   *
   * @return Whether there are no more zero-copy writes in progress.
   */
  bool processZeroCopyWriteInProgress() noexcept;

  /**
   * Whether socket should be closed on write failure (true by default).
   *
   * @param closeOnFailedWrite  Whether to close the socket on write failure.
   */
  void setCloseOnFailedWrite(bool closeOnFailedWrite) {
    closeOnFailedWrite_ = closeOnFailedWrite;
  }

  /**
   * Get folly::TcpInfo from socket
   *
   * @param options  Lookup options controlling which TcpInfo is retrieved.
   * @return The TcpInfo on success, or an error code on failure.
   */
  folly::Expected<folly::TcpInfo, std::errc> getTcpInfo(
      const TcpInfo::LookupOptions& options);

  /**
   * writeReturn is the total number of bytes written, or WRITE_ERROR on error.
   * If no data has been written, 0 is returned.
   * exception is a more specific exception that cause a write error.
   * Not all writes have exceptions associated with them thus writeReturn
   * should be checked to determine whether the operation resulted in an error.
   */
  struct WriteResult {
    /// Constructs a result from a write return value.
    /// \param ret  Bytes written, or WRITE_ERROR on error.
    explicit WriteResult(ssize_t ret) : writeReturn(ret) {}

    /// Constructs a result from a write return value and an exception.
    /// \param ret  Bytes written, or WRITE_ERROR on error.
    /// \param e    The exception that caused the write error.
    WriteResult(ssize_t ret, std::unique_ptr<const AsyncSocketException> e)
        : writeReturn(ret), exception(std::move(e)) {}

    ssize_t writeReturn; ///< Bytes written, or WRITE_ERROR on error.
    std::unique_ptr<const AsyncSocketException>
        exception; ///< Exception describing the write error, if any.
  };

  /**
   * readReturn is the number of bytes read, or READ_EOF on EOF, or
   * READ_ERROR on error, or READ_BLOCKING if the operation will
   * block.
   * exception is a more specific exception that may have caused a read error.
   * Not all read errors have exceptions associated with them thus readReturn
   * should be checked to determine whether the operation resulted in an error.
   */
  struct ReadResult {
    /// Constructs a result from a read return value.
    /// \param ret  Bytes read, or one of the READ_* sentinel values.
    explicit ReadResult(ssize_t ret) : readReturn(ret) {}

    /// Constructs a result from a read return value and an exception.
    /// \param ret  Bytes read, or one of the READ_* sentinel values.
    /// \param e    The exception that caused the read error.
    ReadResult(ssize_t ret, std::unique_ptr<const AsyncSocketException> e)
        : readReturn(ret), exception(std::move(e)) {}

    ssize_t readReturn; ///< Bytes read, or a READ_* sentinel value.
    std::unique_ptr<const AsyncSocketException>
        exception; ///< Exception describing the read error, if any.
  };

  /**
   * A WriteRequest object tracks information about a pending write operation.
   */
  class WriteRequest {
   public:
    /// Constructs a write request for the given socket and callback.
    /// \param socket    The owning socket.
    /// \param callback  The write completion/error callback.
    WriteRequest(AsyncSocket* socket, WriteCallback* callback)
        : socket_(socket),
          callbackWithState_(WriteCallbackWithState(callback)),
          releaseIOBufCallback_(
              callback ? callback->getReleaseIOBufCallback() : nullptr) {}

    /// Constructs a write request for the given socket and callback state.
    /// \param socket             The owning socket.
    /// \param callbackWithState  The write callback along with its state.
    WriteRequest(AsyncSocket* socket, WriteCallbackWithState callbackWithState)
        : socket_(socket),
          callbackWithState_(callbackWithState),
          releaseIOBufCallback_(
              callbackWithState.getCallback()
                  ? callbackWithState.getCallback()->getReleaseIOBufCallback()
                  : nullptr) {}

    /// Start processing this write request.
    virtual void start() {}

    /// Destroy this write request.
    virtual void destroy() = 0;

    /// Attempt to write the pending data.
    /// \returns The result of the write attempt.
    virtual WriteResult performWrite() = 0;

    /// Consume the data that has been written.
    virtual void consume() = 0;

    /// Check whether all data for this request has been written.
    /// \returns Whether all data for this request has been written.
    virtual bool isComplete() = 0;

    /// Get the next write request in the chain.
    /// \returns The next write request in the chain, or nullptr.
    WriteRequest* getNext() const { return next_; }

    /// Get the write completion callback.
    /// \returns The write completion callback.
    WriteCallback* getCallback() const {
      return callbackWithState_.getCallback();
    }

    /// Get the write callback along with its state.
    /// \returns The write callback along with its state.
    WriteCallbackWithState& getCallbackWithState() {
      return callbackWithState_;
    }

    /// Get the total number of bytes written for this request.
    /// \returns The total number of bytes written for this request.
    uint32_t getTotalBytesWritten() const { return totalBytesWritten_; }

    /// Append another write request after this one.
    /// \param next  The request to append.
    void append(WriteRequest* next) {
      assert(next_ == nullptr);
      next_ = next;
    }

    /// Fail this write request with an exception.
    /// \param fn  Name of the failing function, for diagnostics.
    /// \param ex  The exception describing the failure.
    void fail(const char* fn, const AsyncSocketException& ex) {
      socket_->failWrite(fn, ex);
    }

    /// Record that some bytes were written for this request.
    /// \param count  Number of bytes written.
    void bytesWritten(size_t count) {
      totalBytesWritten_ += uint32_t(count);
      socket_->appBytesWritten_ += count;
    }

   protected:
    // protected destructor, to ensure callers use destroy()
    /// Destroys the write request; callers must use destroy() instead.
    virtual ~WriteRequest() {}

    AsyncSocket* socket_; ///< parent socket
    WriteRequest* next_{nullptr}; ///< pointer to next WriteRequest
    WriteCallbackWithState callbackWithState_; ///< completion callback
    ReleaseIOBufCallback* releaseIOBufCallback_; ///< release IOBuf callback
    uint32_t totalBytesWritten_{0}; ///< total bytes written
  };

 public:
  /**
   * Observer of socket events.
   */
  class LegacyLifecycleObserver : public AsyncSocketObserverInterface {
   public:
    /**
     * Observer configuration.
     *
     * Specifies events observer wants to receive. Cannot be changed post
     * initialization because the transport may turn on / off instrumentation
     * when observers are added / removed, based on the observer configuration.
     */
    struct Config {
      /// Constructs a config with instrumentation disabled.
      Config() = default;
      /// Copy-constructs a config.
      /// \param other The config to copy from.
      Config(const Config& other) = default;
      /// Copy-assigns a config.
      /// \param other The config to copy from.
      /// \returns A reference to this config.
      Config& operator=(const Config& other) = default;
      /// Destroys the config.
      virtual ~Config() = default;

      /// Whether the observer receives ByteEvents.
      bool byteEvents{false};

      /// Whether the observer is notified during the prewrite stage and can
      /// add WriteFlags.
      bool prewrite{false};

      /**
       * Enable all events in config.
       */
      virtual void enableAllEvents() {
        byteEvents = true;
        prewrite = true;
      }

      /**
       * Returns a config where all events are enabled.
       *
       * @return A config with all events enabled.
       */
      static Config getConfigAllEventsEnabled() {
        Config config = {};
        config.enableAllEvents();
        return config;
      }
    };

    /**
     * Constructor for observer, uses default config (instrumentation disabled).
     */
    LegacyLifecycleObserver() : LegacyLifecycleObserver(Config()) {}

    /**
     * Constructor for observer.
     *
     * @param observerConfig  Config, defaults to auxilary instrumentation
     *                        disabled.
     */
    explicit LegacyLifecycleObserver(const Config& observerConfig)
        : observerConfig_(observerConfig) {}

    /// Destroys the observer.
    ~LegacyLifecycleObserver() override = default;

    /**
     * Returns observer's configuration.
     *
     * @return            Observer configuration.
     */
    const Config& getConfig() { return observerConfig_; }

    /**
     * observerAttach() will be invoked when an observer is added.
     *
     * @param socket   Socket where observer was installed.
     */
    virtual void observerAttach(AsyncSocket* socket) noexcept = 0;

    /**
     * observerDetached() will be invoked if the observer is uninstalled prior
     * to socket destruction.
     *
     * No further events will be invoked after observerDetach().
     *
     * @param socket   Socket where observer was uninstalled.
     */
    virtual void observerDetach(AsyncSocket* socket) noexcept = 0;

    /**
     * destroy() will be invoked when the socket's destructor is invoked.
     *
     * No further events will be invoked after destroy().
     *
     * @param socket   Socket being destroyed.
     */
    virtual void destroy(AsyncSocket* socket) noexcept = 0;

   protected:
    // observer configuration; cannot be changed post instantiation
    const Config observerConfig_; ///< Observer configuration.
  };

  /**
   * Adds a lifecycle observer.
   *
   * Observers can tie their lifetime to aspects of this socket's lifecycle /
   * lifetime and perform inspection at various states.
   *
   * This enables instrumentation to be added without changing / interfering
   * with how the application uses the socket.
   *
   * Observer should implement AsyncSocket::LegacyLifecycleObserver to
   * receive additional lifecycle events specific to AsyncSocket.
   *
   * @param observer     Observer to add (implements LegacyLifecycleObserver).
   */
  virtual void addLifecycleObserver(LegacyLifecycleObserver* observer);

  /**
   * Removes a lifecycle observer.
   *
   * @param observer     Observer to remove.
   * @return             Whether observer found and removed from list.
   */
  virtual bool removeLifecycleObserver(LegacyLifecycleObserver* observer);

  /**
   * Returns installed lifecycle observers.
   *
   * @return             Vector with installed observers.
   */
  [[nodiscard]] virtual std::vector<LegacyLifecycleObserver*>
  getLifecycleObservers() const;

  /**
   * Adds an observer.
   *
   * If the observer is already added, this is a no-op.
   *
   * @param observer     Observer to add.
   * @return             Whether the observer was added (fails if no list).
   */
  virtual bool addObserver(Observer* observer) {
    if (auto list = getAsyncSocketObserverContainer()) {
      list->addObserver(observer);
      return true;
    }
    return false;
  }

  /**
   * Adds an observer.
   *
   * If the observer is already added, this is a no-op.
   *
   * @param observer     Observer to add.
   * @return             Whether the observer was added (fails if no list).
   */
  bool addObserver(std::shared_ptr<Observer> observer) {
    if (auto list = getAsyncSocketObserverContainer()) {
      list->addObserver(std::move(observer));
      return true;
    }
    return false;
  }

  /**
   * Removes an observer.
   *
   * @param observer     Observer to remove.
   * @return             Whether the observer was found and removed.
   */
  virtual bool removeObserver(Observer* observer) {
    if (auto list = getAsyncSocketObserverContainer()) {
      return list->removeObserver(observer);
    }
    return false;
  }

  /**
   * Removes an observer.
   *
   * @param observer     Observer to remove.
   * @return             Whether the observer was found and removed.
   */
  virtual bool removeObserver(std::shared_ptr<Observer> observer) {
    if (auto list = getAsyncSocketObserverContainer()) {
      return list->removeObserver(std::move(observer));
    }
    return false;
  }

  /**
   * Get number of observers.
   *
   * @return             Number of observers.
   */
  [[nodiscard]] virtual size_t numObservers() {
    if (auto list = getAsyncSocketObserverContainer()) {
      return list->numObservers();
    }
    return 0;
  }

  /**
   * Returns list of attached observers that are of type T.
   *
   * @return             Attached observers of type T.
   */
  template <typename T = Observer>
  std::vector<T*> findObservers() {
    if (auto list = getAsyncSocketObserverContainer()) {
      return list->findObservers<T>();
    }
    return {};
  }

 private:
  /**
   * Returns the AsyncSocketObserverContainer or nullptr if not available.
   */
  [[nodiscard]] AsyncSocketObserverContainer*
  getAsyncSocketObserverContainer() {
    return &observerContainer_;
  }

 public:
  /**
   * Split iovec array at given byte offsets; produce a new array with result.
   *
   * @param startOffset  Byte offset at which the output begins.
   * @param endOffset    Byte offset at which the output ends.
   * @param srcVec       Source iovec array.
   * @param srcCount     Number of entries in `srcVec`.
   * @param dstVec       Destination iovec array to populate.
   * @param dstCount     On return, the number of entries written to `dstVec`.
   */
  static void splitIovecArray(
      const size_t startOffset,
      const size_t endOffset,
      const iovec* srcVec,
      const size_t srcCount,
      iovec* dstVec,
      size_t& dstCount);

  /// Apply a set of socket options at the given point in the socket lifecycle.
  /// \param options  The socket options to apply.
  /// \param pos      When, relative to bind/connect, the options are applied.
  void applyOptions(
      const SocketOptionMap& options, SocketOptionKey::ApplyPos pos);

 protected:
  /// Sentinel results returned by read operations.
  enum ReadResultEnum {
    READ_EOF = 0, ///< End of file reached.
    READ_ERROR = -1, ///< A read error occurred.
    READ_BLOCKING = -2, ///< The read would block.
    READ_NO_ERROR = -3, ///< No error has occurred.
    READ_ASYNC = -4, ///< The read is being completed asynchronously.
  };

  /// Sentinel results returned by write operations.
  enum WriteResultEnum {
    WRITE_ERROR = -1, ///< A write error occurred.
  };

  /**
   * Protected destructor.
   *
   * Users of AsyncSocket must never delete it directly.  Instead, invoke
   * destroy() instead.  (See the documentation in DelayedDestruction.h for
   * more details.)
   */
  ~AsyncSocket() override;

  /// Writes a textual representation of a socket state to a stream.
  /// \param os     The output stream to write to.
  /// \param state  The state to write.
  /// \returns The output stream.
  friend std::ostream& operator<<(std::ostream& os, const StateEnum& state);

  /// Flags describing which halves of the socket have been shut down.
  enum ShutdownFlags {
    /// shutdownWrite() called, but we are still waiting on writes to drain
    SHUT_WRITE_PENDING = 0x01,
    /// writes have been completely shut down
    SHUT_WRITE = 0x02,
    /**
     * Reads have been shutdown.
     *
     * At the moment we don't distinguish between remote read shutdown
     * (received EOF from the remote end) and local read shutdown.  We can
     * only receive EOF when a read callback is set, and we immediately inform
     * it of the EOF.  Therefore there doesn't seem to be any reason to have a
     * separate state of "received EOF but the local side may still want to
     * read".
     *
     * We also don't currently provide any API for only shutting down the read
     * side of a socket.  (This is a no-op as far as TCP is concerned, anyway.)
     */
    SHUT_READ = 0x04,
  };

  /// Write request that writes raw byte buffers to the socket.
  class BytesWriteRequest;

  /// Timeout that fires when a connect or write does not make progress.
  class WriteTimeout : public AsyncTimeout {
   public:
    /// Constructs the timeout for the given socket and EventBase.
    /// \param socket     The owning socket.
    /// \param eventBase  The EventBase that schedules the timeout.
    WriteTimeout(AsyncSocket* socket, EventBase* eventBase)
        : AsyncTimeout(eventBase), socket_(socket) {}

    /// Invoked when the timeout expires.
    void timeoutExpired() noexcept override { socket_->timeoutExpired(); }

   private:
    AsyncSocket* socket_;
  };

  /// Event handler that dispatches fd readiness events to the socket.
  class IoHandler : public EventHandler {
   public:
    /// Constructs a handler not yet bound to a file descriptor.
    /// \param socket     The owning socket.
    /// \param eventBase  The EventBase monitoring the fd.
    IoHandler(AsyncSocket* socket, EventBase* eventBase)
        : EventHandler(eventBase, NetworkSocket()), socket_(socket) {}
    /// Constructs a handler bound to a file descriptor.
    /// \param socket     The owning socket.
    /// \param eventBase  The EventBase monitoring the fd.
    /// \param fd         The file descriptor to monitor.
    IoHandler(AsyncSocket* socket, EventBase* eventBase, NetworkSocket fd)
        : EventHandler(eventBase, fd), socket_(socket) {}

    /// Invoked when the monitored fd becomes ready.
    /// \param events  Bitmask of ready event flags.
    void handlerReady(uint16_t events) noexcept override {
      socket_->ioReady(events);
    }

   private:
    AsyncSocket* socket_;
  };

  /// Initialize the socket's internal state.
  void init();

  /// Loop callback that performs an immediate read on the next loop iteration.
  class ImmediateReadCB : public folly::EventBase::LoopCallback {
   public:
    /// Constructs the callback for the given socket.
    /// \param socket  The owning socket.
    explicit ImmediateReadCB(AsyncSocket* socket) : socket_(socket) {}
    /// Invoked on the next loop iteration to check for an immediate read.
    void runLoopCallback() noexcept override {
      DestructorGuard dg(socket_);
      socket_->checkForImmediateRead();
    }

   private:
    AsyncSocket* socket_;
  };

  /**
   * Schedule checkForImmediateRead to be executed in the next loop
   * iteration.
   */
  void scheduleImmediateRead() noexcept {
    if (good()) {
      eventBase_->runInLoop(&immediateReadHandler_);
    }
  }

  /**
   * Schedule handleInitalReadWrite to run in the next iteration.
   */
  void scheduleInitialReadWrite() noexcept {
    if (good()) {
      DestructorGuard dg(this);
      eventBase_->runInLoop([this, dg] {
        if (good()) {
          handleInitialReadWrite();
        }
      });
    }
  }

  /// Drain and process the socket's error queue.
  void drainErrorQueue() noexcept;

  // event notification methods

  /// Handle fd readiness events reported by the EventBase.
  /// \param events  Bitmask of ready event flags.
  void ioReady(uint16_t events) noexcept;
  /// Check whether an immediate read can be performed and do so if possible.
  virtual void checkForImmediateRead() noexcept;
  /// Handle the initial read/write once the socket becomes connected.
  virtual void handleInitialReadWrite() noexcept;
  /// Obtain a buffer into which incoming data can be read.
  /// \param buf     Out-parameter set to the read buffer.
  /// \param buflen  Out-parameter set to the read buffer length.
  virtual void prepareReadBuffer(void** buf, size_t* buflen);
  /// Obtain a set of iovec buffers into which incoming data can be read.
  /// \param iovs  Out-parameter populated with the read buffers.
  virtual void prepareReadBuffers(IOBufIovecBuilder::IoVecVec& iovs);
  /// Handle messages available on the socket error queue.
  /// \returns The number of error messages processed.
  virtual size_t handleErrMessages() noexcept;
  /// Handle readable data on the socket.
  virtual void handleRead() noexcept;
  /// Handle the socket becoming writable.
  virtual void handleWrite() noexcept;
  /// Handle completion of a connection attempt.
  virtual void handleConnect() noexcept;
  /// Handle expiration of the connect/write timeout.
  void timeoutExpired() noexcept;
  /// Check whether there are writes still pending on the socket.
  /// \returns Whether there are writes still pending on the socket.
  bool hasPendingWrites() noexcept;

  /**
   * Handler for when the file descriptor is attached to the AsyncSocket.

   * This updates the EventHandler to start using the fd and notifies all
   * observers attached to the socket. This is necessary to let
   * observers know about an attached fd immediately (i.e., on connection
   * attempt) rather than when the connection succeeds.
   */
  virtual void handleNetworkSocketAttached();

  /**
   * Populate an iovec array from an IOBuf and attempt to write it.
   *
   * @param callback Write completion/error callback.
   * @param vec      Target iovec array; caller retains ownership.
   * @param count    Number of IOBufs to write, beginning at start of buf.
   * @param buf      Chain of iovecs.
   * @param flags    set of flags for the underlying write calls, like cork
   */
  void writeChainImpl(
      WriteCallback* callback,
      iovec* vec,
      size_t count,
      std::unique_ptr<folly::IOBuf>&& buf,
      WriteFlags flags);

  /**
   * Write as much data as possible to the socket without blocking,
   * and queue up any leftover data to send when the socket can
   * handle writes again.
   *
   * @param callback    The callback to invoke when the write is completed.
   * @param vec         Array of buffers to write; this method will make a
   *                    copy of the vector (but not the buffers themselves)
   *                    if the write has to be completed asynchronously.
   * @param count       Number of elements in vec.
   * @param buf         The IOBuf that manages the buffers referenced by
   *                    vec, or a pointer to nullptr if the buffers are not
   *                    associated with an IOBuf.  Note that ownership of
   *                    the IOBuf is transferred here; upon completion of
   *                    the write, the AsyncSocket deletes the IOBuf.
   * @param totalBytes  The total number of bytes to be written.
   * @param flags       Set of write flags.
   */
  void writeImpl(
      WriteCallback* callback,
      const iovec* vec,
      size_t count,
      std::unique_ptr<folly::IOBuf>&& buf,
      size_t totalBytes,
      WriteFlags flags = WriteFlags::NONE);

  /**
   * Attempt to write to the socket.
   *
   * @param vec             The iovec array pointing to the buffers to write.
   * @param count           The length of the iovec array.
   * @param flags           Set of write flags.
   * @param countWritten    On return, the value pointed to by this parameter
   *                          will contain the number of iovec entries that were
   *                          fully written.
   * @param partialWritten  On return, the value pointed to by this parameter
   *                          will contain the number of bytes written in the
   *                          partially written iovec entry.
   * @param writeTag        Tag identifying the write, for ancillary data.
   *
   * @return Returns a WriteResult. See WriteResult for more details.
   */
  virtual WriteResult performWrite(
      const iovec* vec,
      uint32_t count,
      WriteFlags flags,
      uint32_t* countWritten,
      uint32_t* partialWritten,
      WriteRequestTag writeTag);

  /**
   * Prepares a msghdr and sends the message over the socket using sendmsg
   *
   * @param vec             The iovec array pointing to the buffers to write.
   * @param count           The length of the iovec array.
   * @param flags           Set of write flags.
   * @param writeTag        Tag identifying the write, for ancillary data.
   * @return Returns a WriteResult describing the outcome.
   */
  virtual AsyncSocket::WriteResult sendSocketMessage(
      const iovec* vec,
      size_t count,
      WriteFlags flags,
      WriteRequestTag writeTag);

  /**
   * Sends the message over the socket using sendmsg
   *
   * @param fd        Socket to send the message on.
   * @param msg       Message to send
   * @param msg_flags Flags to pass to sendmsg
   * @return Returns a WriteResult describing the outcome.
   */
  virtual AsyncSocket::WriteResult sendSocketMessage(
      NetworkSocket fd, struct msghdr* msg, int msg_flags);

  /// Send a message using the TCP Fast Open path.
  /// \param fd         Socket to send the message on.
  /// \param msg        Message to send.
  /// \param msg_flags  Flags to pass to sendmsg.
  /// \returns Number of bytes sent, or -1 on error.
  virtual ssize_t tfoSendMsg(
      NetworkSocket fd, struct msghdr* msg, int msg_flags);

  /// Perform a blocking connect() on the underlying socket.
  /// \param addr  The address to connect to.
  /// \param len   Length, in bytes, of the address structure.
  /// \returns 0 on success, or a non-zero errno value on error.
  int socketConnect(const struct sockaddr* addr, socklen_t len);

  /// Schedule the connection timeout to fire.
  virtual void scheduleConnectTimeout();
  /// Register the socket to receive connection-completion events.
  void registerForConnectEvents();

  /// Update the socket's event registration to match its current state.
  /// \returns Whether the update succeeded.
  bool updateEventRegistration();

  /**
   * Update event registration.
   *
   * @param enable Flags of events to enable. Set it to 0 if no events
   * need to be enabled in this call.
   * @param disable Flags of events
   * to disable. Set it to 0 if no events need to be disabled in this
   * call.
   *
   * @return true iff the update is successful.
   */
  bool updateEventRegistration(uint16_t enable, uint16_t disable);

  // Attempt to read into one or more `struct iovec`s.  The caller is
  // responsible for setting `msg.msg_iov` and `msg.msg_iovlen` to the
  // buffers that will receive the read, and for initializing
  // `msg.msg_name*`.  In the case that `readAncillaryCallback_` is set, the
  // caller may also want to populate `msg_control`, `msg_controllen`, and
  // `msg_flags` -- if no ancillary data are being read, it's fine to leave
  // them at their defaults of 0.
  /// Read into one or more iovecs from the socket.
  /// \param msg       The message header describing the receive buffers.
  /// \param readMode  The read mode to use.
  /// \returns The result of the read attempt.
  virtual ReadResult performReadMsg(
      struct ::msghdr& msg, AsyncReader::ReadCallback::ReadMode readMode);

  // Actually close the file descriptor and set it to -1 so we don't
  // accidentally close it again.
  /// Close the underlying file descriptor and mark it invalid.
  void doClose();

  // error handling methods

  /// Outcome of a read attempt used to drive the read loop.
  enum class ReadCode {
    READ_NOT_SUPPORTED = 0, ///< The read mode is not supported.
    READ_CONTINUE = 1, ///< Continue reading.
    READ_DONE = 2, ///< Reading is complete for now.
  };

  /// Begin failing the socket, transitioning it into the error state.
  void startFail();
  /// Finish failing the socket after startFail().
  void finishFail();
  /// Finish failing the socket with a specific exception.
  /// \param ex  The exception describing the failure.
  void finishFail(const AsyncSocketException& ex);
  /// Invoke all pending callbacks with the given error.
  /// \param ex  The exception to deliver.
  void invokeAllErrors(const AsyncSocketException& ex);
  /// Fail the socket and all its callbacks.
  /// \param fn  Name of the failing function, for diagnostics.
  /// \param ex  The exception describing the failure.
  void fail(const char* fn, const AsyncSocketException& ex);
  /// Fail a pending connection attempt.
  /// \param fn  Name of the failing function, for diagnostics.
  /// \param ex  The exception describing the failure.
  void failConnect(const char* fn, const AsyncSocketException& ex);
  /// Fail the pending read and notify the read callback.
  /// \param fn  Name of the failing function, for diagnostics.
  /// \param ex  The exception describing the failure.
  /// \returns The read code to drive the read loop.
  ReadCode failRead(const char* fn, const AsyncSocketException& ex);
  /// Fail a read from the socket error queue.
  /// \param fn  Name of the failing function, for diagnostics.
  /// \param ex  The exception describing the failure.
  void failErrMessageRead(const char* fn, const AsyncSocketException& ex);
  /// Fail a specific write and notify its callback.
  /// \param fn            Name of the failing function, for diagnostics.
  /// \param callback      The write callback to notify.
  /// \param bytesWritten  Number of bytes written before the failure.
  /// \param ex            The exception describing the failure.
  void failWrite(
      const char* fn,
      WriteCallback* callback,
      size_t bytesWritten,
      const AsyncSocketException& ex);
  /// Fail the current write.
  /// \param fn  Name of the failing function, for diagnostics.
  /// \param ex  The exception describing the failure.
  void failWrite(const char* fn, const AsyncSocketException& ex);
  /// Fail all pending writes.
  /// \param ex  The exception describing the failure.
  void failAllWrites(const AsyncSocketException& ex);
  /// Fail outstanding byte events.
  /// \param ex  The exception describing the failure.
  void failByteEvents(const AsyncSocketException& ex);
  /// Notify the connect callback of a connection error.
  /// \param ex  The exception describing the error.
  virtual void invokeConnectErr(const AsyncSocketException& ex);
  /// Notify the connect callback of a successful connection.
  virtual void invokeConnectSuccess();
  /// Notify the connect callback that a connection attempt is starting.
  virtual void invokeConnectAttempt();
  /// Notify a connect callback that the socket is in an invalid state.
  /// \param callback  The connect callback to notify.
  void invalidState(ConnectCallback* callback);
  /// Notify an error-message callback that the socket is in an invalid state.
  /// \param callback  The error-message callback to notify.
  void invalidState(ErrMessageCallback* callback);
  /// Notify a read callback that the socket is in an invalid state.
  /// \param callback  The read callback to notify.
  void invalidState(ReadCallback* callback);
  /// Notify a write callback that the socket is in an invalid state.
  /// \param callback  The write callback to notify.
  void invalidState(WriteCallback* callback);

  /// Prefix a message with this socket's address, for diagnostics.
  /// \param s  The message to prefix.
  /// \returns The message prefixed with the socket address.
  std::string withAddr(folly::StringPiece s);

  /// Cache the socket's local address for later retrieval.
  void cacheLocalAddress() const;
  /// Cache the socket's peer address for later retrieval.
  void cachePeerAddress() const;

  /// Check whether a write with the given flags requests zero-copy.
  /// \param flags  The write flags to inspect.
  /// \returns Whether the write flags request a zero-copy write.
  bool isZeroCopyRequest(WriteFlags flags);

  /// Check whether a control message is a zero-copy completion.
  /// \param cmsg  The control message to inspect.
  /// \returns Whether the control message is a zero-copy completion.
  bool isZeroCopyMsg(const cmsghdr& cmsg) const;
  /// Process a zero-copy completion control message.
  /// \param cmsg  The control message to process.
  void processZeroCopyMsg(const cmsghdr& cmsg);

  /// Get and advance the next zero-copy buffer id.
  /// \returns The next zero-copy buffer id.
  uint32_t getNextZeroCopyBufId() { return zeroCopyBufId_++; }
  /// Adjust the given write flags for zero-copy behavior.
  /// \param flags  The write flags to adjust in place.
  void adjustZeroCopyFlags(folly::WriteFlags& flags);
  /// Track an IOBuf submitted for a zero-copy write.
  /// \param buf  The IOBuf to track; ownership is transferred.
  /// \param cb   Callback invoked when the IOBuf can be released.
  void addZeroCopyBuf(
      std::unique_ptr<folly::IOBuf>&& buf, ReleaseIOBufCallback* cb);
  /// Add a reference to an already-tracked zero-copy IOBuf.
  /// \param ptr  Pointer to the IOBuf.
  void addZeroCopyBuf(folly::IOBuf* ptr);
  /// Associate an IOBuf with the current zero-copy buffer id.
  /// \param buf  The IOBuf to associate; ownership is transferred.
  /// \param cb   Callback invoked when the IOBuf can be released.
  void setZeroCopyBuf(
      std::unique_ptr<folly::IOBuf>&& buf, ReleaseIOBufCallback* cb);
  /// Check whether the given IOBuf is tracked for zero-copy.
  /// \param ptr  Pointer to the IOBuf.
  /// \returns Whether the given IOBuf is tracked for zero-copy.
  bool containsZeroCopyBuf(folly::IOBuf* ptr);
  /// Release the zero-copy buffer associated with the given id.
  /// \param id  The zero-copy buffer id to release.
  void releaseZeroCopyBuf(uint32_t id);

  /// Drain the queue of pending zero-copy completions.
  void drainZeroCopyQueue();
  /// Schedule a later attempt to drain pending zero-copy completions.
  void scheduleZeroCopyDrain();
  /// Release all outstanding zero-copy buffers immediately.
  void forceReleaseZeroCopyBufs();
  /// Close the underlying network socket.
  void closeNetworkSocket();

  /// Release an IOBuf back to its owner.
  /// \param buf       The IOBuf to release; ownership is transferred.
  /// \param callback  Callback to notify of the release.
  void releaseIOBuf(
      std::unique_ptr<folly::IOBuf> buf,
      ReleaseIOBufCallback* callback) override;
  /// Detach an IOBuf that is being tracked by the socket.
  /// \param buf  The IOBuf to detach.
  void detachIOBuf(const folly::IOBuf& buf) override;

  /// Process a completed zero-copy read.
  /// \returns The read code to drive the read loop.
  ReadCode processZeroCopyRead();
  /// Process a completed normal (non-zero-copy) read.
  /// \returns The read code to drive the read loop.
  ReadCode processNormalRead();
  /**
   * Attempt to enable Observer ByteEvents for this socket.
   *
   * Once enabled, ByteEvents rename enabled for the socket's life.
   *
   * ByteEvents are delivered to Observers; when an observer is added:
   *    - If this function has already been called, byteEventsEnabled() or
   *      byteEventsUnavailable() will be called, depending on ByteEvent state.
   *    - Else if the socket is connected, this function is called immediately.
   *    - Else if the socket has not yet connected, this function will be called
   *      after the socket has connected (ByteEvents cannot be set up earlier).
   *
   * If ByteEvents are successfully enabled, byteEventsEnabled() will be called
   * on each Observer that has requested ByteEvents. If unable to enable, or if
   * ByteEvents become unavailable (e.g., due to close), byteEventsUnavailable()
   * will be called on each Observer that has requested ByteEvents.
   *
   * This function does need to be explicitly called under other circumstances.
   */
  virtual void enableByteEvents();

  /*
   * IoUringConnectCallback
   */
  /// Handle a successful io_uring connect completion.
  void connectSuccess() override;
  /// Handle an io_uring connect timeout.
  void connectTimeout() override;

  /*
   * IoUringSendCallback
   */
  /// Handle a partial io_uring send completion.
  /// \param bytesWritten  Number of bytes written in this completion.
  void sendPartial(size_t bytesWritten = 0) override;
  /// Handle a completed io_uring send.
  /// \param bytesWritten  Number of bytes written.
  void sendDone(size_t bytesWritten = 0) override;
  /// Handle an io_uring send error.
  /// \param err  The errno value describing the error.
  void sendErr(int err) override;

  /*
   * IoUringRecvCallback
   */
  /// Handle a successful io_uring receive.
  /// \param data  The received data.
  void recvSuccess(std::unique_ptr<IOBuf> data) override;
  /// Handle an io_uring receive reaching end of file.
  void recvEOF() noexcept override;
  /// Handle an io_uring receive error.
  /// \param err        The errno value describing the error.
  /// \param exception  Exception describing the error, if any.
  void recvErr(
      int err,
      std::unique_ptr<const AsyncSocketException> exception) noexcept override;

  /// Policy deciding whether a write should use zero-copy.
  using ZeroCopyEnablePolicy =
      std::variant<std::monostate, AsyncWriter::ZeroCopyEnableFunc, size_t>;
  ZeroCopyEnablePolicy zeroCopyEnablePolicy_; ///< Active zero-copy policy.

  // a folly::IOBuf can be used in multiple partial requests
  // there is a that maps a buffer id to a raw folly::IOBuf ptr
  // and another one that adds a ref count for a folly::IOBuf that is either
  // the original ptr or nullptr
  uint32_t zeroCopyBufId_{0}; ///< Next zero-copy buffer id to assign.

  ZeroCopyDrainConfig zeroCopyDrainConfig_; ///< Zero-copy drain configuration.

  // Set when the post-close zero-copy drain starts; bounds how long
  // scheduleZeroCopyDrain() may keep rescheduling itself.
  std::chrono::steady_clock::time_point
      zeroCopyDrainDeadline_{}; ///< Deadline for the post-close drain.

  /// Reference-counting info for a zero-copy IOBuf.
  struct IOBufInfo {
    uint32_t count_{0}; ///< Number of outstanding references.
    ReleaseIOBufCallback* cb_{nullptr}; ///< Release callback, if any.
    std::unique_ptr<folly::IOBuf> buf_; ///< The owned IOBuf.
  };

  std::unordered_map<uint32_t, folly::IOBuf*>
      idZeroCopyBufPtrMap_; ///< Maps buffer id to raw IOBuf pointer.
  std::unordered_map<folly::IOBuf*, IOBufInfo>
      idZeroCopyBufInfoMap_; ///< Maps IOBuf pointer to its reference info.

  StateEnum state_{StateEnum::UNINIT}; ///< StateEnum describing current state
  uint8_t shutdownFlags_{0}; ///< Shutdown state (ShutdownFlags)
  uint16_t eventFlags_; ///< EventBase::HandlerFlags settings
  NetworkSocket fd_; ///< The socket file descriptor
  mutable folly::SocketAddress addr_; ///< The address we tried to connect to
  mutable folly::SocketAddress
      localAddr_; ///< The address we are connecting from
  uint32_t sendTimeout_; ///< The send timeout, in milliseconds
  uint16_t maxReadsPerEvent_; ///< Max reads per event loop iteration

  int8_t readErr_{READ_NO_ERROR}; ///< The read error encountered, if any

  EventBase* eventBase_; ///< The EventBase
  WriteTimeout writeTimeout_; ///< A timeout for connect and write
  IoHandler ioHandler_; ///< A EventHandler to monitor the fd
  ImmediateReadCB immediateReadHandler_; ///< LoopCallback for checking read

  ConnectCallback* connectCallback_; ///< ConnectCallback
  ErrMessageCallback* errMessageCallback_; ///< TimestampCallback
  ReadAncillaryDataCallback*
      readAncillaryDataCallback_; ///< AncillaryDataCallback
  SendMsgParamsCallback* ///< Callback for retrieving
      sendMsgParamCallback_; ///< ::sendmsg() parameters
  ReadCallback* readCallback_; ///< ReadCallback
  WriteRequest* writeReqHead_; ///< Chain of WriteRequests
  WriteRequest* writeReqTail_; ///< End of WriteRequest chain
  std::weak_ptr<ShutdownSocketSet>
      wShutdownSocketSet_; ///< ShutdownSocketSet this socket belongs to.
  size_t appBytesReceived_; ///< Num of bytes received from socket
  size_t appBytesWritten_{0}; ///< Num of bytes written to socket
  size_t rawBytesWritten_{0}; ///< Num of (raw) bytes written to socket
  // The total num of bytes passed to AsyncSocket's write functions. It doesn't
  // include failed writes, but it does include buffered writes.
  size_t totalAppBytesScheduledForWrite_; ///< Total bytes scheduled for write.
  // Num of bytes allocated in IOBufs pending write.
  size_t allocatedBytesBuffered_{0}; ///< Bytes allocated in buffers pending write.

  // Lifecycle observers.
  //
  // Use small_vector to avoid heap allocation for up to two observers, unless
  // mobile, in which case we fallback to std::vector to prioritize code size.
  /// Container type used to store lifecycle observers.
  using LifecycleObserverVecImpl = conditional_t<
      !kIsMobile,
      folly::small_vector<LegacyLifecycleObserver*, 2>,
      std::vector<LegacyLifecycleObserver*>>;
  LifecycleObserverVecImpl lifecycleObservers_; ///< Installed lifecycle observers.

  // Pre-received data, to be returned to read callback before any data from the
  // socket.
  std::unique_ptr<IOBuf> preReceivedData_; ///< Buffered pre-received data.

  std::chrono::steady_clock::time_point
      connectStartTime_; ///< When connect() started.

  std::chrono::steady_clock::time_point
      connectEndTime_; ///< When connect() completed.

  // When the connection was established.
  //
  //  -  If connect() was called and succeeded, this is the same as
  //     connectEndTime_.
  //
  //  -  If AsyncSocket was initialized with a file descriptor (e.g., by an
  //     acceptor), this is the connection establishment time passed to the
  //     constructor. If no time was passed, this is folly::none.
  folly::Optional<std::chrono::steady_clock::time_point>
      maybeConnectionEstablishTime_; ///< When the connection was established.

  std::chrono::milliseconds connectTimeout_{0}; ///< Configured connect timeout.

  std::unique_ptr<EvbChangeCallback>
      evbChangeCb_{nullptr}; ///< EventBase-change callback, if any.

  BufferCallback* bufferCallback_{nullptr}; ///< Buffered-write callback, if any.

  /// State tracking a TCP Fast Open attempt.
  struct TCPFastOpenInfo {
    bool attempted{false}; ///< Whether TFO was attempted.
    bool enabled{false}; ///< Whether TFO is enabled.
    bool finished{false}; ///< Whether the TFO attempt finished.
  };

  TCPFastOpenInfo tfoInfo_; ///< TCP Fast Open state.
  bool bindAddressNoPort_{true}; ///< Whether IP_BIND_ADDRESS_NO_PORT is set.
  bool noTransparentTls_{false}; ///< Whether transparent TLS is disabled.
  bool noTSocks_{false}; ///< Whether transparent SOCKS is disabled.
  // Whether to track EOR or not.
  bool trackEor_{false}; ///< Whether end-of-record tracking is enabled.
  Optional<int> tosOrTrafficClass_; ///< TOS or traffic class override, if any.

  // ByteEvent state
  std::unique_ptr<ByteEventHelper> byteEventHelper_; ///< ByteEvent state.

  bool zeroCopyEnabled_{false}; ///< Whether zero-copy writes are enabled.
  bool zeroCopyVal_{false}; ///< Cached zero-copy socket option value.
  // zerocopy re-enable logic
  size_t zeroCopyReenableThreshold_{0}; ///< Threshold to re-enable zero-copy.
  size_t zeroCopyReenableCounter_{0}; ///< Progress toward re-enabling zero-copy.

  // zerocopy read
  bool zerocopyReadDisabled_{false}; ///< Whether zero-copy reads are disabled.
  int zerocopyReadErr_{0}; ///< Last zero-copy read errno.

  bool closeOnFailedWrite_{true}; ///< Whether to close on a failed write.

  bool useIoUring_{false}; ///< Whether io_uring is used for this socket.
  bool iouRecvHandleDetached_{false}; ///< Whether the io_uring recv handle is detached.
  IoUringConnectHandle::UniquePtr iouConnectHandle_; ///< io_uring connect handle.
  IoUringSendHandle::UniquePtr iouSendHandle_; ///< io_uring send handle.
  IoUringRecvHandle::UniquePtr iouRecvHandle_; ///< io_uring recv handle.

  netops::DispatcherContainer netops_; ///< netops dispatcher container.

  folly::TcpInfoDispatcherContainer
      tcpInfoDispatcher_; ///< TcpInfo dispatcher container.

  // Container of observers for the socket / transport.
  //
  // This member MUST be last in the list of members (other than
  // constructorCallbackList_) to ensure it is destroyed first, before any other
  // members are destroyed. This ensures that observers can inspect any socket /
  // transport state available through public methods when destruction of the
  // transport begins.
  AsyncSocketObserverContainer
      observerContainer_; ///< Container of observers for this socket.

  /// Callbacks invoked when an AsyncSocket is constructed.
  ///
  /// Allows other functions to register for callbacks when new
  /// `AsyncSocket()`s are created. Must be the LAST member defined to ensure
  /// other members are initialized before access; see
  /// `ConstructorCallbackList.h` for details.
  ConstructorCallbackList<AsyncSocket> constructorCallbackList_{this};
};

std::ostream& operator<<(
    std::ostream& os, const folly::AsyncSocket::WriteRequestTag& tag);

} // namespace folly

/// Hash specialization for AsyncSocket::WriteRequestTag.
template <>
struct std::hash<folly::AsyncSocket::WriteRequestTag> {
  /// Computes a hash for a write request tag.
  /// \param writeTag  The tag to hash.
  /// \returns The hash of the tag's wrapped pointer.
  std::size_t operator()(
      const folly::AsyncSocket::WriteRequestTag& writeTag) const {
    return std::hash<const folly::IOBuf*>{}(writeTag.buf_);
  }
};
