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

#include <folly/SocketAddress.h>
#include <folly/io/async/EventBase.h>

namespace folly {

/// Base interface for asynchronous sockets bound to an event base.
class AsyncSocketBase {
 public:
  /// Returns the event base this socket is attached to.
  ///
  /// \returns The event base this socket is attached to.
  virtual EventBase* getEventBase() const = 0;
  /// Destroys the socket.
  virtual ~AsyncSocketBase() = default;
  /// Stores the socket's address into the given output parameter.
  ///
  /// \param address Output parameter that receives the socket's address.
  virtual void getAddress(SocketAddress* address) const = 0;
};

} // namespace folly
