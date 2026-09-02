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

#include <folly/Function.h>
#include <folly/IndexedMemPool.h>
#include <folly/Portability.h>
#include <folly/concurrency/CacheLocality.h>
#include <folly/synchronization/SaturatingSemaphore.h>
#include <folly/system/ThreadName.h>

#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>

namespace folly {

/// Flat combining (FC) was introduced in the SPAA 2010 paper Flat
/// Combining and the Synchronization-Parallelism Tradeoff, by Danny
/// Hendler, Itai Incze, Nir Shavit, and Moran Tzafrir.
/// http://mcg.cs.tau.ac.il/projects/projects/flat-combining
///
/// FC is an alternative to coarse-grained locking for making
/// sequential data structures thread-safe while minimizing the
/// synchronization overheads and cache coherence traffic associated
/// with locking.
///
/// Under FC, when a thread finds the lock contended, it can
/// request (using a request record) that the lock holder execute its
/// operation on the shared data structure. There can be a designated
/// combiner thread or any thread can act as the combiner when it
/// holds the lock.
///
/// Potential advantages of FC include:
/// - Reduced cache coherence traffic
/// - Reduced synchronization overheads, as the overheads of releasing
///   and acquiring the lock are eliminated from the critical path of
///   operating on the data structure.
/// - Opportunities for smart combining, where executing multiple
///   operations together may take less time than executing the
///   operations separately, e.g., K delete_min operations on a
///   priority queue may be combined to take O(K + log N) time instead
///   of O(K * log N).
///
/// This implementation of flat combining supports:

/// - A simple interface that requires minimal extra code by the
///   user. To use this interface efficiently the user-provided
///   functions must be copyable to folly::Function without dynamic
///   allocation. If this is impossible or inconvenient, the user is
///   encouraged to use the custom interface described below.
/// - A custom interface that supports custom combining and custom
///   request structure, either for the sake of smart combining or for
///   efficiently supporting operations that are not be copyable to
///   folly::Function without dynamic allocation.
/// - Both synchronous and asynchronous operations.
/// - Request records with and without thread-caching.
/// - Combining with and without a dedicated combiner thread.
///
/// This implementation differs from the algorithm in the SPAA 2010 paper:
/// - It does not require thread caching of request records
/// - It supports a dedicated combiner
/// - It supports asynchronous operations
///
/// The generic FC class template supports generic data structures and
/// utilities with arbitrary operations. The template supports static
/// polymorphism for the combining function to enable custom smart
/// combining.
///
/// A simple example of using the FC template:
///   class ConcurrentFoo : public FlatCombining<ConcurrentFoo> {
///     Foo foo_; // sequential data structure
///    public:
///     T bar(V& v) { // thread-safe execution of foo_.bar(v)
///       T result;
///       // Note: fn must be copyable to folly::Function without dynamic
///       // allocation. Otherwise, it is recommended to use the custom
///       // interface and manage the function arguments and results
///       // explicitly in a custom request structure.
///       auto fn = [&] { result = foo_.bar(v); };
///       this->requestFC(fn);
///       return result;
///     }
///   };
///
/// See test/FlatCombiningExamples.h for more examples. See the
/// comments for requestFC() below for a list of simple and custom
/// variants of that function.

template <
    typename T, // concurrent data structure using FC interface
    typename Mutex = std::mutex,
    template <typename> class Atom = std::atomic,
    typename Req = /* default dummy type */ bool>
class FlatCombining {
  using SavedFn = folly::Function<void()>;

 public:
  /// Combining request record.
  class Rec {
    alignas(hardware_destructive_interference_size)
        folly::SaturatingSemaphore<false, Atom> valid_;
    folly::SaturatingSemaphore<false, Atom> done_;
    folly::SaturatingSemaphore<false, Atom> disconnected_;
    size_t index_;
    size_t next_;
    uint64_t last_;
    Req req_;
    SavedFn fn_;

   public:
    /// Constructs a record marked done and disconnected.
    Rec() {
      setDone();
      setDisconnected();
    }

    /// Marks the record as holding a valid request.
    void setValid() { valid_.post(); }

    /// Clears the valid flag on the record.
    void clearValid() { valid_.reset(); }

    /// Reports whether the record holds a valid request.
    ///
    /// \returns true if the record is valid, otherwise false.
    bool isValid() const { return valid_.ready(); }

    /// Marks the record's request as done.
    void setDone() { done_.post(); }

    /// Clears the done flag on the record.
    void clearDone() { done_.reset(); }

    /// Reports whether the record's request is done.
    ///
    /// \returns true if the request is done, otherwise false.
    bool isDone() const { return done_.ready(); }

    /// Blocks until the record's request is done.
    void awaitDone() { done_.wait(); }

    /// Marks the record as disconnected from the pending list.
    void setDisconnected() { disconnected_.post(); }

    /// Clears the disconnected flag on the record.
    void clearDisconnected() { disconnected_.reset(); }

    /// Reports whether the record is disconnected from the pending list.
    ///
    /// \returns true if the record is disconnected, otherwise false.
    bool isDisconnected() const { return disconnected_.ready(); }

    /// Sets the record's index within the pool.
    ///
    /// \param index The pool index to store.
    void setIndex(const size_t index) { index_ = index; }

    /// Returns the record's index within the pool.
    ///
    /// \returns The stored pool index.
    size_t getIndex() const { return index_; }

    /// Sets the index of the next record in the pending list.
    ///
    /// \param next The next record's index.
    void setNext(const size_t next) { next_ = next; }

    /// Returns the index of the next record in the pending list.
    ///
    /// \returns The next record's index.
    size_t getNext() const { return next_; }

    /// Records the pass number in which the record was last combined.
    ///
    /// \param pass The pass number to store.
    void setLast(const uint64_t pass) { last_ = pass; }

    /// Returns the pass number in which the record was last combined.
    ///
    /// \returns The stored pass number.
    uint64_t getLast() const { return last_; }

    /// Returns a reference to the record's request payload.
    ///
    /// \returns A mutable reference to the request.
    Req& getReq() { return req_; }

    /// Stores the function to execute for this record's request.
    ///
    /// \param fn The callable to run when the request is combined.
    template <typename Func>
    void setFn(Func&& fn) {
      static_assert(
          std::is_nothrow_constructible<
              folly::Function<void()>,
              std::decay_t<Func>>::value,
          "Try using a smaller function object that can fit in folly::Function "
          "without allocation, or use the custom interface of requestFC() to "
          "manage the requested function's arguments and results explicitly "
          "in a custom request structure without allocation.");
      fn_ = std::forward<Func>(fn);
      assert(fn_);
    }

    /// Clears the stored function.
    void clearFn() {
      fn_ = {};
      assert(!fn_);
    }

    /// Returns a reference to the stored function.
    ///
    /// \returns A mutable reference to the saved function.
    SavedFn& getFn() { return fn_; }

    /// Marks the request complete by clearing valid and setting done.
    void complete() {
      clearValid();
      assert(!isDone());
      setDone();
    }
  };

  /// Memory pool that owns and recycles combining request records.
  using Pool = folly::
      IndexedMemPool<Rec, 32, 4, Atom, IndexedMemPoolTraitsLazyRecycle<Rec>>;

 public:
  /// The constructor takes three optional arguments:
  /// - Optional dedicated combiner thread (default true)
  /// - Number of records (if 0, then kDefaultNumRecs)
  /// - A hint for the max. number of combined operations per
  ///   combining session that is checked at the beginning of each pass
  ///   on the request records (if 0, then kDefaultMaxops)
  ///
  /// \param dedicated Whether to spawn a dedicated combiner thread.
  /// \param numRecs Number of combining records; 0 selects `kDefaultNumRecs`.
  /// \param maxOps Hint for the max operations per session; 0 selects
  /// `kDefaultMaxOps`.
  /// \param dedicatedCombinerThreadName Optional name for the dedicated
  /// combiner thread.
  explicit FlatCombining(
      const bool dedicated = true,
      const uint32_t numRecs = 0, // number of combining records
      const uint32_t maxOps = 0, // hint of max ops per combining session
      std::optional<std::string> dedicatedCombinerThreadName = std::nullopt)
      : numRecs_(numRecs == 0 ? kDefaultNumRecs : numRecs),
        maxOps_(maxOps == 0 ? kDefaultMaxOps : maxOps),
        recs_(NULL_INDEX),
        dedicated_(dedicated),
        recsPool_(numRecs_) {
    if (dedicatedCombinerThreadName && !dedicated) {
      throw std::runtime_error(
          "can't set the name of a dedicated combiner thread if this thread is not created at all");
    }

    if (dedicated_) {
      // dedicated combiner thread
      combiner_ = std::thread([this, dedicatedCombinerThreadName] {
        if (dedicatedCombinerThreadName) {
          folly::setThreadName(*dedicatedCombinerThreadName);
        }
        dedicatedCombining();
      });
    }
  }

  /// Destructor: If there is a dedicated combiner, the destructor
  /// flags it to shutdown. Otherwise, the destructor waits for all
  /// pending asynchronous requests to be completed.
  ~FlatCombining() {
    if (dedicated_) {
      shutdown();
      combiner_.join();
    } else {
      drainAll();
    }
  }

  /// Deleted copy constructor; instances are non-copyable.
  FlatCombining(const FlatCombining& other) = delete;
  /// Deleted copy assignment; instances are non-copyable.
  FlatCombining& operator=(const FlatCombining& other) = delete;
  /// Deleted move constructor; instances are non-movable.
  FlatCombining(FlatCombining&& other) = delete;
  /// Deleted move assignment; instances are non-movable.
  FlatCombining& operator=(FlatCombining&& other) = delete;

  /// Wait for all pending operations to complete. Useful primarily
  /// when there are asynchronous operations without a dedicated
  /// combiner.
  void drainAll() {
    for (size_t i = getRecsHead(); i != NULL_INDEX; i = nextIndex(i)) {
      Rec& rec = recsPool_[i];
      awaitDone(rec);
    }
  }

  /// Give the caller exclusive access.
  void acquireExclusive() { m_.lock(); }

  /// Try to give the caller exclusive access.
  ///
  /// \returns true if exclusive access was acquired, otherwise false.
  bool tryExclusive() { return m_.try_lock(); }

  /// Release exclusive access. The caller must have exclusive access.
  void releaseExclusive() { m_.unlock(); }

  /// Give the lock holder ownership of the mutex and exclusive access.
  /// No need for explicit release.
  ///
  /// \param l The lock holder to take ownership of the mutex.
  template <typename LockHolder>
  void holdLock(LockHolder& l) {
    l = LockHolder(m_);
  }

  /// Give the caller's lock holder ownership of the mutex but without
  /// exclusive access. The caller can later use the lock holder to try
  /// to acquire exclusive access.
  ///
  /// \param l The lock holder to associate with the mutex.
  /// \param tag Tag selecting the deferred-lock overload.
  template <typename LockHolder>
  void holdLock(LockHolder& l, std::defer_lock_t tag) {
    l = LockHolder(m_, std::defer_lock);
  }

  /// Execute an operation without combining.
  ///
  /// \param opFn The operation to execute under the exclusive lock.
  template <typename OpFunc>
  void requestNoFC(OpFunc& opFn) {
    std::lock_guard guard(m_);
    opFn();
  }

  /// Requests execution of an operation, combining it with others when
  /// possible.
  ///
  /// This function first tries to execute the operation without
  /// combining. If unuccessful, it allocates a combining record if
  /// needed. If there are no available records, it waits for exclusive
  /// access and executes the operation. If a record is available and
  /// ready for use, it fills the record and indicates that the request
  /// is valid for combining. If the request is synchronous (by default
  /// or necessity), it waits for the operation to be completed by a
  /// combiner and optionally extracts the result, if any.
  ///
  /// This function can be called in several forms:
  ///   Simple forms that do not require the user to define a Req structure
  ///   or to override any request processing member functions:
  ///     requestFC(opFn)
  ///     requestFC(opFn, rec) // provides its own pre-allocated record
  ///     requestFC(opFn, rec, syncop) // asynchronous if syncop == false
  ///   Custom forms that require the user to define a Req structure and to
  ///   override some request processing member functions:
  ///     requestFC(opFn, fillFn)
  ///     requestFC(opFn, fillFn, rec)
  ///     requestFC(opFn, fillFn, rec, syncop)
  ///     requestFC(opFn, fillFn, resFn)
  ///     requestFC(opFn, fillFn, resFn, rec)
  ///
  /// \param opFn The operation to execute.
  /// \param rec Optional pre-allocated record; nullptr allocates one.
  /// \param syncop Whether the request is synchronous.
  template <typename OpFunc>
  void requestFC(OpFunc&& opFn, Rec* rec = nullptr, bool syncop = true) {
    auto dummy = [](Req&) {};
    requestOp(
        std::forward<OpFunc>(opFn),
        dummy /* fillFn */,
        dummy /* resFn */,
        rec,
        syncop,
        false /* simple */);
  }
  /// Requests a combinable operation using a custom fill function.
  ///
  /// \param opFn The operation to execute.
  /// \param fillFn Fills the request record before combining.
  /// \param rec Optional pre-allocated record; nullptr allocates one.
  /// \param syncop Whether the request is synchronous.
  template <typename OpFunc, typename FillFunc>
  void requestFC(
      OpFunc&& opFn,
      const FillFunc& fillFn,
      Rec* rec = nullptr,
      bool syncop = true) {
    auto dummy = [](Req&) {};
    requestOp(
        std::forward<OpFunc>(opFn),
        fillFn,
        dummy /* resFn */,
        rec,
        syncop,
        true /* custom */);
  }
  /// Requests a combinable operation with custom fill and result functions.
  ///
  /// \param opFn The operation to execute.
  /// \param fillFn Fills the request record before combining.
  /// \param resFn Extracts the result after the operation completes.
  /// \param rec Optional pre-allocated record; nullptr allocates one.
  template <typename OpFunc, typename FillFunc, typename ResFn>
  void requestFC(
      OpFunc&& opFn,
      const FillFunc& fillFn,
      const ResFn& resFn,
      Rec* rec = nullptr) {
    // must wait for result to execute resFn -- so it must be synchronous
    requestOp(
        std::forward<OpFunc>(opFn),
        fillFn,
        resFn,
        rec,
        true /* sync */,
        true /* custom*/);
  }

  /// Allocate a record.
  ///
  /// \returns A pointer to a free record, or nullptr if none is available.
  Rec* allocRec() {
    auto idx = recsPool_.allocIndex();
    if (idx == NULL_INDEX) {
      return nullptr;
    }
    Rec& rec = recsPool_[idx];
    rec.setIndex(idx);
    return &rec;
  }

  /// Free a record.
  ///
  /// \param rec The record to recycle; nullptr is ignored.
  void freeRec(Rec* rec) {
    if (rec == nullptr) {
      return;
    }
    auto idx = rec->getIndex();
    recsPool_.recycleIndex(idx);
  }

  /// Returns the number of uncombined operations so far.
  ///
  /// \returns The count of operations executed without combining.
  uint64_t getNumUncombined() const { return uncombined_; }

  /// Returns the number of combined operations so far.
  ///
  /// \returns The count of operations executed by a combiner.
  uint64_t getNumCombined() const { return combined_; }

  /// Returns the number of combining passes so far.
  ///
  /// \returns The count of combining passes.
  uint64_t getNumPasses() const { return passes_; }

  /// Returns the number of combining sessions so far.
  ///
  /// \returns The count of combining sessions.
  uint64_t getNumSessions() const { return sessions_; }

 protected:
  /// Sentinel index representing the absence of a record.
  const size_t NULL_INDEX = 0;
  /// Default hint for the maximum operations per combining session.
  const uint32_t kDefaultMaxOps = 100;
  /// Default number of combining records.
  const uint64_t kDefaultNumRecs = 64;
  /// Number of idle passes after which the combiner may sleep.
  const uint64_t kIdleThreshold = 10;

  /// The mutex guarding exclusive access to the data structure.
  alignas(hardware_destructive_interference_size) Mutex m_;

  /// Signals the dedicated combiner that requests are pending.
  alignas(hardware_destructive_interference_size)
      folly::SaturatingSemaphore<true, Atom> pending_;
  /// Set to request shutdown of the dedicated combiner.
  Atom<bool> shutdown_{false};

  /// The number of combining records in the pool.
  alignas(hardware_destructive_interference_size) uint32_t numRecs_;
  /// The hint for the maximum operations per combining session.
  uint32_t maxOps_;
  /// The head index of the list of pending records.
  Atom<size_t> recs_;
  /// Whether a dedicated combiner thread is used.
  bool dedicated_;
  /// The dedicated combiner thread, if any.
  std::thread combiner_;
  /// The pool that owns the combining records.
  Pool recsPool_;

  /// Running count of uncombined operations.
  alignas(hardware_destructive_interference_size) uint64_t uncombined_ = 0;
  /// Running count of combined operations.
  uint64_t combined_ = 0;
  /// Running count of combining passes.
  uint64_t passes_ = 0;
  /// Running count of combining sessions.
  uint64_t sessions_ = 0;

  /// Core request path shared by all requestFC() overloads.
  ///
  /// \param opFn The operation to execute.
  /// \param fillFn Fills the request record for custom requests.
  /// \param resFn Extracts the result for custom requests.
  /// \param rec Optional pre-allocated record.
  /// \param syncop Whether the request is synchronous.
  /// \param custom Whether the custom fill/result path is used.
  template <typename OpFunc, typename FillFunc, typename ResFn>
  void requestOp(
      OpFunc&& opFn,
      const FillFunc& fillFn,
      const ResFn& resFn,
      Rec* rec,
      bool syncop,
      const bool custom) {
    std::unique_lock l(this->m_, std::defer_lock);
    if (l.try_lock()) {
      // No contention
      ++uncombined_;
      tryCombining();
      opFn();
      return;
    }

    // Try FC
    bool tc = (rec != nullptr);
    if (!tc) {
      // if an async op doesn't have a thread-cached record then turn
      // it into a synchronous op.
      syncop = true;
      rec = allocRec();
    }
    if (rec == nullptr) {
      // Can't use FC - Must acquire lock
      l.lock();
      ++uncombined_;
      tryCombining();
      opFn();
      return;
    }

    // Use FC
    // Wait if record is in use
    awaitDone(*rec);
    rec->clearDone();
    // Fill record
    if (custom) {
      // Fill the request (custom)
      Req& req = rec->getReq();
      fillFn(req);
      rec->clearFn();
    } else {
      rec->setFn(std::forward<OpFunc>(opFn));
    }
    // Indicate that record is valid
    assert(!rec->isValid());
    rec->setValid();
    // end of combining critical path
    setPending();
    // store-load order setValid before isDisconnected
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (rec->isDisconnected()) {
      rec->clearDisconnected();
      pushRec(rec->getIndex());
      setPending();
    }
    // If synchronous wait for the request to be completed
    if (syncop) {
      awaitDone(*rec);
      if (custom) {
        Req& req = rec->getReq();
        resFn(req); // Extract the result (custom)
      }
      if (!tc) {
        freeRec(rec); // Free the temporary record.
      }
    }
  }

  /// Pushes a record index onto the head of the pending list.
  ///
  /// \param idx The record index to push.
  void pushRec(size_t idx) {
    Rec& rec = recsPool_[idx];
    while (true) {
      auto head = recs_.load(std::memory_order_acquire);
      rec.setNext(head); // there shouldn't be a data race here
      if (recs_.compare_exchange_weak(head, idx)) {
        return;
      }
    }
  }

  /// Returns the head index of the pending record list.
  ///
  /// \returns The index at the head of the pending list.
  size_t getRecsHead() { return recs_.load(std::memory_order_acquire); }

  /// Returns the index of the record following the given one.
  ///
  /// \param idx The current record index.
  /// \returns The next record index.
  size_t nextIndex(size_t idx) { return recsPool_[idx].getNext(); }

  /// Clears the pending signal.
  void clearPending() { pending_.reset(); }

  /// Raises the pending signal.
  void setPending() { pending_.post(); }

  /// Reports whether the pending signal is raised.
  ///
  /// \returns true if requests are pending, otherwise false.
  bool isPending() const { return pending_.ready(); }

  /// Blocks until the pending signal is raised.
  void awaitPending() { pending_.wait(); }

  /// Runs one combining session over the pending records.
  ///
  /// \returns The number of operations combined in the session.
  uint64_t combiningSession() {
    uint64_t combined = 0;
    do {
      uint64_t count = static_cast<T*>(this)->combiningPass();
      if (count == 0) {
        break;
      }
      combined += count;
      ++this->passes_;
    } while (combined < this->maxOps_);
    return combined;
  }

  /// Performs combining sessions while requests are pending, when there is no
  /// dedicated combiner.
  void tryCombining() {
    if (!dedicated_) {
      while (isPending()) {
        clearPending();
        ++sessions_;
        combined_ += combiningSession();
      }
    }
  }

  /// Loop run by the dedicated combiner thread until shutdown.
  void dedicatedCombining() {
    while (true) {
      awaitPending();
      clearPending();
      if (shutdown_.load()) {
        break;
      }
      while (true) {
        uint64_t count;
        ++sessions_;
        {
          std::lock_guard guard(m_);
          count = combiningSession();
          combined_ += count;
        }
        if (count < maxOps_) {
          break;
        }
      }
    }
  }

  /// Waits for the given record's request to be done.
  ///
  /// \param rec The record to wait on.
  void awaitDone(Rec& rec) {
    if (dedicated_) {
      rec.awaitDone();
    } else {
      awaitDoneTryLock(rec);
    }
  }

  /// Waits for the request to be done and occasionally tries to
  /// acquire the lock and to do combining. Used only in the absence
  /// of a dedicated combiner.
  ///
  /// \param rec The record to wait on.
  void awaitDoneTryLock(Rec& rec) {
    assert(!dedicated_);
    int count = 0;
    while (!rec.isDone()) {
      if (count == 0) {
        std::unique_lock l(m_, std::defer_lock);
        if (l.try_lock()) {
          setPending();
          tryCombining();
        }
      } else {
        folly::asm_volatile_pause();
        if (++count == 1000) {
          count = 0;
        }
      }
    }
  }

  /// Requests shutdown of the dedicated combiner thread.
  void shutdown() {
    shutdown_.store(true);
    setPending();
  }

  // The following member functions may be overridden for customization

  /// Executes a combined custom request; must be overridden by the derived
  /// class when the custom interface is used.
  ///
  /// \param req The request to execute.
  void combinedOp(Req& req) {
    throw std::runtime_error(
        "FlatCombining::combinedOp(Req&) must be overridden in the derived"
        " class if called.");
  }

  /// Processes a single request record, running its function and completing it.
  ///
  /// \param rec The record to process.
  void processReq(Rec& rec) {
    SavedFn& opFn = rec.getFn();
    if (opFn) {
      // simple interface
      opFn();
    } else {
      // custom interface
      Req& req = rec.getReq();
      static_cast<T*>(this)->combinedOp(req); // defined in derived class
    }
    rec.setLast(passes_);
    rec.complete();
  }

  /// Performs one pass over the pending records, processing valid requests and
  /// disconnecting idle ones.
  ///
  /// \returns The number of requests processed in the pass.
  uint64_t combiningPass() {
    uint64_t count = 0;
    auto idx = getRecsHead();
    Rec* prev = nullptr;
    while (idx != NULL_INDEX) {
      Rec& rec = recsPool_[idx];
      auto next = rec.getNext();
      bool valid = rec.isValid();
      if (!valid && (passes_ - rec.getLast() > kIdleThreshold) &&
          (prev != nullptr)) {
        // Disconnect
        prev->setNext(next);
        rec.setDisconnected();
        // store-load order setDisconnected before isValid
        std::atomic_thread_fence(std::memory_order_seq_cst);
        valid = rec.isValid();
      } else {
        prev = &rec;
      }
      if (valid) {
        processReq(rec);
        ++count;
      }
      idx = next;
    }
    return count;
  }
};

} // namespace folly
