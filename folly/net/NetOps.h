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

#include <cstdint>

#include <folly/Portability.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/IOVec.h>
#include <folly/portability/SysTypes.h>
#include <folly/portability/Time.h>
#include <folly/portability/Windows.h>

#ifdef _WIN32

#include <WS2tcpip.h> // @manual

using nfds_t = int;
using sa_family_t = ADDRESS_FAMILY;

// these are not supported
#define SO_EE_ORIGIN_ZEROCOPY 0
#define SO_ZEROCOPY 0
#define SO_TXTIME 0
#define MSG_ZEROCOPY 0x0
#define SOL_UDP 0x0
#define UDP_SEGMENT 0x0
#define IP_BIND_ADDRESS_NO_PORT 0

// We don't actually support either of these flags
// currently.
#define MSG_DONTWAIT 0x1000
#define MSG_EOR 0
struct msghdr {
  void* msg_name;
  socklen_t msg_namelen;
  struct iovec* msg_iov;
  size_t msg_iovlen;
  void* msg_control;
  size_t msg_controllen;
  int msg_flags;
};

struct mmsghdr {
  struct msghdr msg_hdr;
  unsigned int msg_len;
};

struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[108];
};

#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

// These are the same, but PF_LOCAL
// isn't defined by WinSock.
#define AF_LOCAL PF_UNIX
#define PF_LOCAL PF_UNIX

// This isn't defined by Windows, and we need to
// distinguish it from SO_REUSEADDR
#define SO_REUSEPORT 0x7001

// Someone thought it would be a good idea
// to define a field via a macro...
#undef s_host
#else

#if defined(__EMSCRIPTEN__)
#include <sys/types.h>
#endif

#include <netdb.h>
#include <poll.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <sys/un.h>

#if !defined(__EMSCRIPTEN__)
#ifdef MSG_ERRQUEUE
#define FOLLY_HAVE_MSG_ERRQUEUE 1
#ifdef SCM_TIMESTAMPING
#ifndef FOLLY_HAVE_SO_TIMESTAMPING
#define FOLLY_HAVE_SO_TIMESTAMPING 1
#endif
#ifndef TCP_ZEROCOPY_RECEIVE
#define TCP_ZEROCOPY_RECEIVE 35
#endif
#else
#ifndef TCP_ZEROCOPY_RECEIVE
#define TCP_ZEROCOPY_RECEIVE 0
#endif
#endif
/* for struct sock_extended_err*/
#include <linux/errqueue.h>
#endif
#endif

#ifndef SO_EE_ORIGIN_ZEROCOPY
#define SO_EE_ORIGIN_ZEROCOPY 5
#endif

#ifndef SO_EE_CODE_ZEROCOPY_COPIED
#define SO_EE_CODE_ZEROCOPY_COPIED 1
#endif

#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif

#ifndef SO_TXTIME
#define SO_TXTIME 61
#define SCM_TXTIME SO_TXTIME
#endif

#ifdef FOLLY_HAVE_MSG_ERRQUEUE
namespace folly {
namespace netops {

/* Copied from uapi/linux/net_tstamp.h */
enum txtime_flags {
  SOF_TXTIME_DEADLINE_MODE = (1 << 0),
  SOF_TXTIME_REPORT_ERRORS = (1 << 1),

  SOF_TXTIME_FLAGS_LAST = SOF_TXTIME_REPORT_ERRORS,
  SOF_TXTIME_FLAGS_MASK = (SOF_TXTIME_FLAGS_LAST - 1) | SOF_TXTIME_FLAGS_LAST
};

/* Copied from uapi/linux/net_tstamp.h */
enum timestamping_flags {
  SOF_TIMESTAMPING_TX_HARDWARE = (1 << 0),
  SOF_TIMESTAMPING_TX_SOFTWARE = (1 << 1),
  SOF_TIMESTAMPING_RX_HARDWARE = (1 << 2),
  SOF_TIMESTAMPING_RX_SOFTWARE = (1 << 3),
  SOF_TIMESTAMPING_SOFTWARE = (1 << 4),
  SOF_TIMESTAMPING_SYS_HARDWARE = (1 << 5),
  SOF_TIMESTAMPING_RAW_HARDWARE = (1 << 6),
  SOF_TIMESTAMPING_OPT_ID = (1 << 7),
  SOF_TIMESTAMPING_TX_SCHED = (1 << 8),
  SOF_TIMESTAMPING_TX_ACK = (1 << 9),
  SOF_TIMESTAMPING_OPT_CMSG = (1 << 10),
  SOF_TIMESTAMPING_OPT_TSONLY = (1 << 11),
  SOF_TIMESTAMPING_OPT_STATS = (1 << 12),
  SOF_TIMESTAMPING_OPT_PKTINFO = (1 << 13),
  SOF_TIMESTAMPING_OPT_TX_SWHW = (1 << 14),

  SOF_TIMESTAMPING_LAST = SOF_TIMESTAMPING_OPT_TX_SWHW,
  SOF_TIMESTAMPING_MASK = (SOF_TIMESTAMPING_LAST - 1) | SOF_TIMESTAMPING_LAST
};

/* Copied from uapi/linux/net_tstamp.h */
enum tstamp_flags {
  SCM_TSTAMP_SND, /* driver passed skb to NIC, or HW */
  SCM_TSTAMP_SCHED, /* data entered the packet scheduler */
  SCM_TSTAMP_ACK, /* data acknowledged by peer */
};

struct sock_txtime {
  __kernel_clockid_t clockid; /* reference clockid */
  __u32 flags; /* as defined by enum txtime_flags */
};

/* Copied from uapi/linux/tcp.h */
/* setsockopt(fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, ...) */

struct tcp_zerocopy_receive {
  __u64 address; /* in: address of mapping */
  __u32 length; /* in/out: number of bytes to map/mapped */
  __u32 recv_skip_hint; /* out: amount of bytes to skip */
  __u32 inq; /* out: amount of bytes in read queue */
  __s32 err; /* out: socket error */
  __u64 copybuf_address; /* in: copybuf address (small reads) */
  __s32 copybuf_len; /* in/out: copybuf bytes avail/used or error */
  __u32 flags; /* in: flags */
  __u64 msg_control; /* ancillary data */
  __u64 msg_controllen;
  __u32 msg_flags;
  __u32 reserved; /* set to 0 for now */
};

/* Copied from uapi/linux/if_xdp.h. The trailing `tx_metadata_len` field
 * was added in Linux kernel 6.8; older kernel headers in the build env
 * may lack it. The kernel parses XDP_UMEM_REG by `optlen`, so passing
 * this struct on an older kernel just causes the trailing field to be
 * ignored.
 */
struct xdp_umem_reg {
  __u64 addr;
  __u64 len;
  __u32 chunk_size;
  __u32 headroom;
  __u32 flags;
  __u32 tx_metadata_len;
};

/* Copied from uapi/linux/if_xdp.h. This type was added in Linux kernel
 * 6.8; older kernel headers in the build env may lack it. Layout must
 * remain byte-compatible with the kernel definition since the kernel
 * reads it directly out of UMEM at runtime.
 */
FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wnested-anon-types")
struct xsk_tx_metadata {
  __u64 flags;

  union {
    struct {
      __u16 csum_start;
      __u16 csum_offset;
      __u64 launch_time;
    } request;

    struct {
      __u64 tx_timestamp;
    } completion;
  };
};
FOLLY_POP_WARNING
} // namespace netops
} // namespace folly
#endif

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif

#ifndef SOL_UDP
#define SOL_UDP 17
#endif

#ifndef ETH_MAX_MTU
#define ETH_MAX_MTU 0xFFFFU
#endif

#ifndef UDP_NO_CHECK6_TX
#define UDP_NO_CHECK6_TX 101 /* Disable sending checksum for UDP6X */
#endif

#ifndef UDP_NO_CHECK6_RX
#define UDP_NO_CHECK6_RX 102 /* Disable accpeting checksum for UDP6 */
#endif

#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif

#ifndef UDP_GRO
#define UDP_GRO 104
#endif

#ifndef UDP_MAX_SEGMENTS
#define UDP_MAX_SEGMENTS (1 << 6UL)
#endif

#if !defined(MSG_WAITFORONE) && !defined(__wasm__)
/// Header for a single message in a scatter/gather multi-message operation.
struct mmsghdr {
  /// The message header.
  struct msghdr msg_hdr;
  /// The number of bytes transmitted for the message.
  unsigned int msg_len;
};
#endif

#ifndef IP_BIND_ADDRESS_NO_PORT
#define IP_BIND_ADDRESS_NO_PORT 24
#endif

/* AF_XDP TX metadata flags. Added in Linux kernel 6.8. */
#ifndef XDP_TX_METADATA
#define XDP_TX_METADATA (1 << 1)
#endif

#ifndef XDP_TXMD_FLAGS_CHECKSUM
#define XDP_TXMD_FLAGS_CHECKSUM (1 << 1)
#endif

#ifndef XDP_UMEM_TX_METADATA_LEN
#define XDP_UMEM_TX_METADATA_LEN (1 << 2)
#endif

#endif

// Set to 1 in the POSIX branch above where the platform supports it. Windows
// never reaches that branch, so give it a value here rather than leaving it
// undefined for the `#if FOLLY_HAVE_SO_TIMESTAMPING` use sites.
#ifndef FOLLY_HAVE_SO_TIMESTAMPING
#define FOLLY_HAVE_SO_TIMESTAMPING 0
#endif

// Various sendmsg structs and ops.
#ifdef _WIN32
#define XPLAT_MSGHDR WSAMSG
#define XPLAT_CMSGHDR WSACMSGHDR
#define F_CMSG_LEN WSA_CMSG_LEN
#define F_COPY_CMSG_INT_DATA(cm, val, len) *(PDWORD)WSA_CMSG_DATA(cm) = *(val)
#else /* !_WIN32 */
#define XPLAT_MSGHDR struct msghdr
#define XPLAT_CMSGHDR struct cmsghdr
#define F_CMSG_LEN CMSG_LEN
#define F_COPY_CMSG_INT_DATA(cm, val, len) memcpy(CMSG_DATA(cm), val, len)
#endif /* _WIN32 */

namespace folly {
/// Cross-platform socket operation wrappers.
namespace netops {
/// Poll descriptor is intended to be byte-for-byte identical to pollfd,
/// except that it is typed as containing a NetworkSocket for sane interactions.
struct PollDescriptor {
  /// The socket to poll.
  NetworkSocket fd;
  /// The events to wait for.
  int16_t events;
  /// The events that occurred.
  int16_t revents;
};

/**
 * A msghdr/WSAMSG struct wrapper for cross-platform use.
 */
class Msgheader {
 public:
  /// Sets the destination address of the message.
  ///
  /// \param addrStorage The address to set.
  /// \param len The length of the address.
  void setName(sockaddr_storage* addrStorage, size_t len);
  /// Sets the scatter/gather I/O buffers of the message.
  ///
  /// \param vec The array of I/O vectors.
  /// \param iovec_len The number of I/O vectors.
  void setIovecs(const struct iovec* vec, size_t iovec_len);
  /// Sets the control (ancillary data) buffer pointer.
  ///
  /// \param ctrlBuf The control buffer.
  void setCmsgPtr(char* ctrlBuf);
  /// Sets the length of the control (ancillary data) buffer.
  ///
  /// \param len The control buffer length.
  void setCmsgLen(size_t len);
  /// Sets the message flags.
  ///
  /// \param flags The flags to set.
  void setFlags(int flags);
  /// Increases the control buffer length by the given amount.
  ///
  /// \param val The number of bytes to add.
  void incrCmsgLen(size_t val);
  /// Returns the first or next control message header.
  ///
  /// \param cm The current header, or null to get the first.
  /// \returns The requested control message header, or null if none.
  XPLAT_CMSGHDR* getFirstOrNextCmsgHeader(XPLAT_CMSGHDR* cm);
  /// Returns the underlying platform message header.
  ///
  /// \returns The wrapped message header.
  XPLAT_MSGHDR* getMsg();

 private:
  XPLAT_MSGHDR msg_;
#ifdef _WIN32
  std::unique_ptr<WSABUF[]> wsaBufs_;
#endif

  XPLAT_CMSGHDR* cmsgNextHrd(XPLAT_CMSGHDR* cm);
  XPLAT_CMSGHDR* cmsgFirstHrd();
};

/// Accepts a connection on a listening socket.
///
/// \param s The listening socket.
/// \param addr Buffer that receives the peer address.
/// \param addrlen In/out length of the address buffer.
/// \returns The accepted socket, or an invalid socket on error.
NetworkSocket accept(NetworkSocket s, sockaddr* addr, socklen_t* addrlen);
/// Binds a socket to a local address.
///
/// \param s The socket to bind.
/// \param name The local address to bind to.
/// \param namelen The length of the address.
/// \returns 0 on success, or -1 on error.
int bind(NetworkSocket s, const sockaddr* name, socklen_t namelen);
/// Closes a socket.
///
/// \param s The socket to close.
/// \returns 0 on success, or -1 on error.
int close(NetworkSocket s);
/// Connects a socket to a remote address.
///
/// \param s The socket to connect.
/// \param name The remote address to connect to.
/// \param namelen The length of the address.
/// \returns 0 on success, or -1 on error.
int connect(NetworkSocket s, const sockaddr* name, socklen_t namelen);
/// Retrieves the address of the connected peer.
///
/// \param s The socket to query.
/// \param name Buffer that receives the peer address.
/// \param namelen In/out length of the address buffer.
/// \returns 0 on success, or -1 on error.
int getpeername(NetworkSocket s, sockaddr* name, socklen_t* namelen);
/// Retrieves the local address bound to a socket.
///
/// \param s The socket to query.
/// \param name Buffer that receives the local address.
/// \param namelen In/out length of the address buffer.
/// \returns 0 on success, or -1 on error.
int getsockname(NetworkSocket s, sockaddr* name, socklen_t* namelen);
/// Retrieves a socket option.
///
/// \param s The socket to query.
/// \param level The protocol level of the option.
/// \param optname The option name.
/// \param optval Buffer that receives the option value.
/// \param optlen In/out length of the value buffer.
/// \returns 0 on success, or -1 on error.
int getsockopt(
    NetworkSocket s, int level, int optname, void* optval, socklen_t* optlen);
/// Converts an IPv4 dotted-decimal string to a network address.
///
/// \param cp The address string.
/// \param inp Buffer that receives the converted address.
/// \returns Nonzero on success, or 0 on error.
int inet_aton(const char* cp, in_addr* inp);
/// Marks a socket as accepting connections.
///
/// \param s The socket to listen on.
/// \param backlog The maximum length of the pending connection queue.
/// \returns 0 on success, or -1 on error.
int listen(NetworkSocket s, int backlog);
/// Waits for events on a set of poll descriptors.
///
/// \param fds The array of poll descriptors.
/// \param nfds The number of descriptors in the array.
/// \param timeout The timeout in milliseconds.
/// \returns The number of ready descriptors, 0 on timeout, or -1 on error.
int poll(PollDescriptor fds[], nfds_t nfds, int timeout);
/// Receives data from a connected socket.
///
/// \param s The socket to receive from.
/// \param buf The buffer that receives the data.
/// \param len The size of the buffer.
/// \param flags Receive flags.
/// \returns The number of bytes received, or -1 on error.
ssize_t recv(NetworkSocket s, void* buf, size_t len, int flags);
/// Receives data and records the source address.
///
/// \param s The socket to receive from.
/// \param buf The buffer that receives the data.
/// \param len The size of the buffer.
/// \param flags Receive flags.
/// \param from Buffer that receives the source address.
/// \param fromlen In/out length of the source address buffer.
/// \returns The number of bytes received, or -1 on error.
ssize_t recvfrom(
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
ssize_t recvmsg(NetworkSocket s, msghdr* message, int flags);
/// Receives multiple messages in a single call.
///
/// \param s The socket to receive from.
/// \param msgvec The array of message headers to fill.
/// \param vlen The number of entries in the array.
/// \param flags Receive flags.
/// \param timeout The timeout, or null to block.
/// \returns The number of messages received, or -1 on error.
int recvmmsg(
    NetworkSocket s,
    mmsghdr* msgvec,
    unsigned int vlen,
    unsigned int flags,
    timespec* timeout);
#ifdef _WIN32
ssize_t wsaRecvMesg(NetworkSocket s, WSAMSG* wsaMsg);
#endif
/// Sends data on a connected socket.
///
/// \param s The socket to send on.
/// \param buf The data to send.
/// \param len The number of bytes to send.
/// \param flags Send flags.
/// \returns The number of bytes sent, or -1 on error.
ssize_t send(NetworkSocket s, const void* buf, size_t len, int flags);
/// Sends data to a specific destination address.
///
/// \param s The socket to send on.
/// \param buf The data to send.
/// \param len The number of bytes to send.
/// \param flags Send flags.
/// \param to The destination address.
/// \param tolen The length of the destination address.
/// \returns The number of bytes sent, or -1 on error.
ssize_t sendto(
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
ssize_t sendmsg(NetworkSocket socket, const msghdr* message, int flags);
#ifdef _WIN32
ssize_t wsaSendMsgDirect(NetworkSocket socket, WSAMSG* msg);
#endif

/// Sends multiple messages in a single call.
///
/// \param socket The socket to send on.
/// \param msgvec The array of messages to send.
/// \param vlen The number of entries in the array.
/// \param flags Send flags.
/// \returns The number of messages sent, or -1 on error.
int sendmmsg(
    NetworkSocket socket, mmsghdr* msgvec, unsigned int vlen, int flags);
/// Sets a socket option.
///
/// \param s The socket to modify.
/// \param level The protocol level of the option.
/// \param optname The option name.
/// \param optval The option value to set.
/// \param optlen The length of the value.
/// \returns 0 on success, or -1 on error.
int setsockopt(
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
int shutdown(NetworkSocket s, int how);
/// Creates a socket.
///
/// \param af The address family.
/// \param type The socket type.
/// \param protocol The protocol.
/// \returns The new socket, or an invalid socket on error.
NetworkSocket socket(int af, int type, int protocol);
/// Creates a connected pair of sockets.
///
/// \param domain The communication domain.
/// \param type The socket type.
/// \param protocol The protocol.
/// \param sv Array that receives the two connected sockets.
/// \returns 0 on success, or -1 on error.
int socketpair(int domain, int type, int protocol, NetworkSocket sv[2]);

// And now we diverge from the Posix way of doing things and just do things
// our own way.
/// Puts a socket into non-blocking mode.
///
/// \param s The socket to modify.
/// \returns 0 on success, or -1 on error.
int set_socket_non_blocking(NetworkSocket s);
/// Puts a socket into blocking mode.
///
/// \param s The socket to modify.
/// \returns 0 on success, or -1 on error.
int set_socket_blocking(NetworkSocket s);
/// Sets the close-on-exec flag on a socket.
///
/// \param s The socket to modify.
/// \returns 0 on success, or -1 on error.
int set_socket_close_on_exec(NetworkSocket s);

#ifdef _WIN32
// Allow override for translation of WSA errors with analytics/tracking.
typedef int (*wsa_error_translator_ptr)(
    NetworkSocket socket, intptr_t api, intptr_t ret, int wsa_error);
void set_wsa_error_translator(
    wsa_error_translator_ptr translator, wsa_error_translator_ptr* previousOut);
#endif

} // namespace netops
} // namespace folly
