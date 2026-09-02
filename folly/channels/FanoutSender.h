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

#include <folly/channels/Channel.h>
#include <folly/channels/detail/PointerVariant.h>
#include <folly/container/F14Set.h>

namespace folly {
namespace channels {

namespace detail {
template <typename ValueType>
class FanoutSenderProcessor;
}

/**
 * A FanoutSender allows fanning out updates to multiple output receivers.
 * Values can be written as with a normal Sender. When there is only one output
 * receiver, the memory used by a FanoutSender (and the corresponding output
 * receiver) is the same as the memory used by a normal channel.
 *
 * When a new output receiver is added, an optional vector of initial values
 * can be provided. These initial values will only be sent to the new receiver.
 *
 * Memory used by closed receivers is reclaimed lazily (when iterating over
 * receivers).
 *
 * Example:
 *
 *  FanoutSender<int> fanoutSender;
 *  auto receiver1 = fanoutSender.subscribe();
 *  auto receiver2 = fanoutSender.subscribe();
 *  auto receiver3 = fanoutSender.subscribe({1, 2, 3});
 *  std::move(fanoutSender).close();
 */
template <typename ValueType>
class FanoutSender {
 public:
  /// Constructs an empty fanout sender with no subscribers.
  FanoutSender();
  /// Move-constructs a fanout sender, transferring ownership.
  /// \param other The fanout sender to move from.
  FanoutSender(FanoutSender&& other) noexcept;
  /// Move-assigns a fanout sender, transferring ownership.
  /// \param other The fanout sender to move from.
  /// \returns A reference to this fanout sender.
  FanoutSender& operator=(FanoutSender&& other) noexcept;
  /// Destroys the fanout sender, closing it if still open.
  ~FanoutSender();

  /**
   * Returns a new output receiver that will receive all values written to the
   * FanoutSender. If the initialValues parameter is provided, the given values
   * will (only) go to the new output receiver.
   *
   * \param initialValues Optional values sent only to the new receiver.
   * \returns A new output receiver subscribed to this fanout sender.
   */
  Receiver<ValueType> subscribe(std::vector<ValueType> initialValues = {});

  /**
   * Subscribes with an already-created sender.
   *
   * \param sender The sender to feed with fanned-out values.
   */
  void subscribe(Sender<ValueType> sender);

  /**
   * Returns whether this fanout sender has any active output receivers.
   *
   * \returns True if there is at least one active receiver, false otherwise.
   */
  bool anySubscribers() const;

  /**
   * Returns the number of output receivers for this fanout sender.
   *
   * \returns The number of active output receivers.
   */
  std::uint64_t numSubscribers() const;

  /**
   * Sends the given value to all corresponding receivers.
   *
   * \param element The value to send to every subscribed receiver.
   */
  template <typename U = ValueType>
  void write(U&& element);

  /**
   * Closes the fanout sender.
   *
   * \param ex Optional exception to close the sender with.
   */
  void close(exception_wrapper ex = exception_wrapper()) &&;

 private:
  bool anySubscribersImpl() const;

  bool hasProcessor() const;

  detail::ChannelBridge<ValueType>* getSingleSender() const;

  detail::FanoutSenderProcessor<ValueType>* getProcessor() const;

  void clearSendersWithClosedReceivers() const;

  mutable detail::PointerVariant<
      detail::ChannelBridge<ValueType>,
      detail::FanoutSenderProcessor<ValueType>>
      senders_;
};
} // namespace channels
} // namespace folly

#include <folly/channels/FanoutSender-inl.h>
