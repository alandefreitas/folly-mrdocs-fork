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
#include <folly/container/F14Set.h>
#include <folly/executors/SequencedExecutor.h>

namespace folly {
namespace channels {

namespace detail {
template <typename KeyType, typename ValueType>
class IMergeChannelProcessor;
}

/// Event signaling that a receiver was added to the merge channel.
struct MergeChannelReceiverAdded {};
/// Event signaling that a receiver was removed from the merge channel.
struct MergeChannelReceiverRemoved {};
/// Event signaling that a merged receiver closed.
struct MergeChannelReceiverClosed {
  /// The exception the receiver closed with, if any.
  exception_wrapper exception;
};

/// An event emitted by a merge channel, tagged with the source receiver key.
template <typename KeyType, typename ValueType>
struct MergeChannelEvent {
  /// The variant of value and lifecycle events a merge channel can emit.
  using EventType = std::variant<
      ValueType,
      MergeChannelReceiverAdded,
      MergeChannelReceiverRemoved,
      MergeChannelReceiverClosed>;

  /// The key of the receiver that produced this event.
  KeyType key;
  /// The value or lifecycle event produced by the receiver.
  EventType event;
};

/**
 * A merge channel allows one to merge multiple receivers into a single
 * output receiver. The set of receivers being merged can be changed at
 * runtime. Each receiver is added with a key that can be used to remove
 * the receiver at a later point.
 *
 * Example:
 *
 *  // Example function that returns a receiver for a given entity:
 *  Receiver<int> subscribe(std::string entity);
 *
 *  // Example function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  auto [outputReceiver, mergeChannel]
 *      = createMergeChannel<std::string, int>(getExecutor());
 *  mergeChannel.addNewReceiver("abc", subscribe("abc"));
 *  mergeChannel.addNewReceiver("def", subscribe("def"));
 *  mergeChannel.removeReceiver("abc");
 *  std::move(mergeChannel).close();
 */
template <typename KeyType, typename ValueType>
class MergeChannel {
  using TProcessor = detail::IMergeChannelProcessor<KeyType, ValueType>;

 public:
  /// Constructs a merge channel backed by the given processor.
  /// \param processor The processor driving this merge channel.
  explicit MergeChannel(
      detail::IMergeChannelProcessor<KeyType, ValueType>* processor);
  /// Move-constructs a merge channel, transferring ownership.
  /// \param other The merge channel to move from.
  MergeChannel(MergeChannel&& other) noexcept;
  /// Move-assigns a merge channel, transferring ownership.
  /// \param other The merge channel to move from.
  /// \returns A reference to this merge channel.
  MergeChannel& operator=(MergeChannel&& other) noexcept;
  /// Destroys the merge channel, closing it if still open.
  ~MergeChannel();

  /**
   * Returns whether this MergeChannel is a valid object.
   *
   * \returns True if this merge channel is valid, false otherwise.
   */
  explicit operator bool() const;

  /**
   * Adds a new receiver to be merged, along with a given key. If the key
   * matches the key of an existing receiver, that existing receiver is replaced
   * with the new one (and updates from the old receiver will no longer be
   * merged). An added receiver can later be removed by passing the same key to
   * removeReceiver.
   *
   * \param key The key identifying the new receiver.
   * \param receiver The receiver to merge into the output.
   */
  template <typename TReceiver>
  void addNewReceiver(KeyType key, TReceiver receiver);

  /**
   * Removes the receiver added with the given key. The receiver will be
   * asynchronously removed, so the consumer may still receive some values from
   * this receiver after this call.
   *
   * \param key The key of the receiver to remove.
   */
  void removeReceiver(KeyType key);

  /**
   * Returns a set of keys for receivers that are merged into this MergeChannel.
   *
   * \returns The set of keys for the currently merged receivers.
   */
  folly::F14FastSet<KeyType> getReceiverKeys();

  /**
   * Returns whether a receiver with the given key exists.
   *
   * \param key The key to look up.
   * \returns True if a receiver with the given key exists, false otherwise.
   */
  template <typename K>
  bool hasReceiverKey(const K& key);

  /**
   * Closes the merge channel.
   *
   * \param ex Optional exception to close the channel with.
   */
  void close(std::optional<exception_wrapper> ex = std::nullopt) &&;

 private:
  TProcessor* processor_;
};

/**
 * Creates a new merge channel.
 *
 * @param executor: The SequencedExecutor to use for merging values.
 *
 * @returns A pair holding the output receiver and the new merge channel.
 */
template <typename KeyType, typename ValueType>
std::pair<
    Receiver<MergeChannelEvent<KeyType, ValueType>>,
    MergeChannel<KeyType, ValueType>>
createMergeChannel(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor);
} // namespace channels
} // namespace folly

#include <folly/channels/MergeChannel-inl.h>
