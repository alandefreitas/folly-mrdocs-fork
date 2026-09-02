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

/*
 * This header defines two classes that very nearly model
 * AssociativeContainer (but not quite).  These implement set-like and
 * map-like behavior on top of a sorted vector, instead of using
 * rb-trees like std::set and std::map.
 *
 * This is potentially useful in cases where the number of elements in
 * the set or map is small, or when you want to avoid using more
 * memory than necessary and insertions/deletions are much more rare
 * than lookups (these classes have O(N) insertions/deletions).
 *
 * In the interest of using these in conditions where the goal is to
 * minimize memory usage, they support a GrowthPolicy parameter, which
 * is a class defining a single function called increase_capacity,
 * which will be called whenever we are about to insert something: you
 * can then decide to call reserve() based on the current capacity()
 * and size() of the passed in vector-esque Container type.  An
 * example growth policy that grows one element at a time:
 *
 *    struct OneAtATimePolicy {
 *      template <class Container>
 *      void increase_capacity(Container& c) {
 *        if (c.size() == c.capacity()) {
 *          c.reserve(c.size() + 1);
 *        }
 *      }
 *    };
 *
 *    typedef sorted_vector_set<int,
 *                              std::less<int>,
 *                              std::allocator<int>,
 *                              OneAtATimePolicy>
 *            OneAtATimeIntSet;
 *
 * Important differences from std::set and std::map:
 *   - insert() and erase() invalidate iterators and references.
       erase(iterator) returns an iterator pointing to the next valid element.
 *   - insert() and erase() are O(N)
 *   - our iterators model RandomAccessIterator
 *   - sorted_vector_map::value_type is pair<K,V>, not pair<const K,V>.
 *     (This is basically because we want to store the value_type in
 *     std::vector<>, which requires it to be Assignable.)
 *   - insert() single key variants, emplace(), and emplace_hint() only provide
 *     the strong exception guarantee (unchanged when exception is thrown) when
 *     std::is_nothrow_move_constructible<value_type>::value is true.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/CppAttributes.h>
#include <folly/ScopeGuard.h>
#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/lang/Access.h>
#include <folly/lang/Exception.h>
#include <folly/memory/MemoryResource.h>
#include <folly/small_vector.h>

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace detail {

template <typename, typename Compare, typename Key, typename T>
struct sorted_vector_enable_if_is_transparent {};

template <typename Compare, typename Key, typename T>
struct sorted_vector_enable_if_is_transparent<
    std::void_t<typename Compare::is_transparent>,
    Compare,
    Key,
    T> {
  using type = T;
};

// This wrapper goes around a GrowthPolicy and provides iterator
// preservation semantics, but only if the growth policy is not the
// default (i.e. nothing).
template <class Policy>
struct growth_policy_wrapper : private Policy {
  template <class Container, class Iterator>
  Iterator increase_capacity(Container& c, Iterator desired_insertion) {
    using diff_t = typename Container::difference_type;
    diff_t d = desired_insertion - c.begin();
    Policy::increase_capacity(c);
    return c.begin() + d;
  }
};
template <>
struct growth_policy_wrapper<void> {
  template <class Container, class Iterator>
  Iterator increase_capacity(Container&, Iterator it) {
    return it;
  }
};

template <class OurContainer, class Vector, class GrowthPolicy, class Value>
typename OurContainer::iterator insert_with_hint(
    OurContainer& sorted,
    Vector& cont,
    typename OurContainer::const_iterator hint,
    Value&& value,
    GrowthPolicy& po) {
  const typename OurContainer::value_compare& cmp(sorted.value_comp());
  if (hint == cont.end() || cmp(value, *hint)) {
    if (hint == cont.begin() || cmp(*(hint - 1), value)) {
      hint = po.increase_capacity(cont, hint);
      return cont.emplace(hint, std::forward<Value>(value));
    } else {
      return sorted.emplace(std::forward<Value>(value)).first;
    }
  }

  if (cmp(*hint, value)) {
    if (hint + 1 == cont.end() || cmp(value, *(hint + 1))) {
      hint = po.increase_capacity(cont, hint + 1);
      return cont.emplace(hint, std::forward<Value>(value));
    } else {
      return sorted.emplace(std::forward<Value>(value)).first;
    }
  }

  // Value and *hint did not compare, so they are equal keys.
  return sorted.begin() + std::distance(sorted.cbegin(), hint);
}

template <typename Iterator, typename Compare>
bool is_sorted_unique(Iterator begin, Iterator end, Compare const& comp) {
  if (begin == end) {
    return true;
  }
  for (auto next = std::next(begin); next != end; ++begin, ++next) {
    if (!comp(*begin, *next)) {
      return false;
    }
  }
  return true;
}

template <typename Container, typename Compare>
Container&& as_sorted_unique(Container&& container, Compare const& comp) {
  std::sort(container.begin(), container.end(), comp);
  container.erase(
      std::unique(
          container.begin(),
          container.end(),
          [&](auto const& a, auto const& b) {
            return !comp(a, b) && !comp(b, a);
          }),
      container.end());
  return static_cast<Container&&>(container);
}

template <typename Container, typename Compare>
class DirectMutationGuard {
 public:
  DirectMutationGuard(
      Container& container, const Compare& comp, bool isSortedUnique)
      : container_(container), comp_(comp), isSortedUnique_(isSortedUnique) {}

  ~DirectMutationGuard() noexcept(false) {
    if (isSortedUnique_) {
      assert(
          detail::is_sorted_unique(
              container_.begin(), container_.end(), comp_));
      return;
    }
    as_sorted_unique(container_, comp_);
  }

  Container& get() { return container_; }

 private:
  Container& container_;
  const Compare comp_;
  const bool isSortedUnique_;
};

template <class OurContainer, class Vector, class InputIterator>
void bulk_insert(
    OurContainer& sorted,
    Vector& cont,
    InputIterator first,
    InputIterator last,
    bool range_is_sorted_unique = false) {
  // Prevent deref of middle where middle == cont.end().
  if (first == last) {
    return;
  }

  auto const prev_size = cont.size();
  cont.insert(cont.end(), first, last);
  auto const middle = cont.begin() + prev_size;

  auto const& cmp(sorted.value_comp());
  if (range_is_sorted_unique) {
    assert(is_sorted_unique(middle, cont.end(), cmp));
  } else if (!std::is_sorted(middle, cont.end(), cmp)) {
    std::sort(middle, cont.end(), cmp);
  }

  // We do not need to consider elements strictly smaller than the smallest new
  // element in merge/unique.
  auto merge_begin = middle;
  while (merge_begin != cont.begin() && !cmp(*(merge_begin - 1), *middle)) {
    --merge_begin;
  }

  if (merge_begin != middle) {
    std::inplace_merge(cont.begin(), middle, cont.end(), cmp);
  } else if (range_is_sorted_unique) {
    // Old and new elements are already disjoint and unique. This includes the
    // case when cont is initially empty.
    return;
  }

  cont.erase(
      std::unique(
          merge_begin,
          cont.end(),
          [&](typename OurContainer::value_type const& a,
              typename OurContainer::value_type const& b) {
            return !cmp(a, b);
          }),
      cont.end());
}

} // namespace detail

//////////////////////////////////////////////////////////////////////

/**
 * A sorted_vector_set is a container similar to std::set<>, but
 * implemented as a sorted array with std::vector<>.
 *
 * @tparam T               Data type to store
 * @tparam Compare         Comparison function that imposes a
 *                              strict weak ordering over instances of T
 * @tparam Allocator       allocation policy
 * @tparam GrowthPolicy    policy object to control growth
 */
template <
    class T,
    class Compare = std::less<T>,
    class Allocator = std::allocator<T>,
    class GrowthPolicy = void,
    class Container = std::vector<T, Allocator>>
class sorted_vector_set : detail::growth_policy_wrapper<GrowthPolicy> {
  detail::growth_policy_wrapper<GrowthPolicy>& get_growth_policy() {
    return *this;
  }

  template <typename K, typename V, typename C = Compare>
  using if_is_transparent =
      _t<detail::sorted_vector_enable_if_is_transparent<void, C, K, V>>;

  struct EBO;

 public:
  /// The element type stored in the set.
  using value_type = T;
  /// The key type of the set (same as the element type).
  using key_type = T;
  /// The comparator type used to order keys.
  using key_compare = Compare;
  /// The comparator type used to order elements.
  using value_compare = Compare;
  /// The allocator type of the underlying container.
  using allocator_type = Allocator;
  /// The underlying container type.
  using container_type = Container;

  /// Pointer to an element.
  using pointer = typename Container::pointer;
  /// Reference to an element.
  using reference = typename Container::reference;
  /// Reference to a const element.
  using const_reference = typename Container::const_reference;
  /// Pointer to a const element.
  using const_pointer = typename Container::const_pointer;
  /*
   * XXX: Our normal iterator ought to also be a constant iterator
   * (cf. Defect Report 103 for std::set), but this is a bit more of a
   * pain.
   */
  /// Iterator over elements.
  using iterator = typename Container::iterator;
  /// Iterator over const elements.
  using const_iterator = typename Container::const_iterator;
  /// Signed difference type between two iterators.
  using difference_type = typename Container::difference_type;
  /// Unsigned size type.
  using size_type = typename Container::size_type;
  /// Reverse iterator over elements.
  using reverse_iterator = typename Container::reverse_iterator;
  /// Reverse iterator over const elements.
  using const_reverse_iterator = typename Container::const_reverse_iterator;
  /// Guard type for direct mutation of the underlying container.
  using direct_mutation_guard =
      detail::DirectMutationGuard<Container, value_compare>;

  /// Constructs an empty set.
  sorted_vector_set() : m_(Compare(), Allocator()) {}

  /// Copy-constructs from another set.
  ///
  /// \param other The set to copy from.
  sorted_vector_set(const sorted_vector_set& other) = default;

  /// Copy-constructs from another set using the given allocator.
  ///
  /// \param other The set to copy from.
  /// \param alloc Allocator to use for the new set.
  sorted_vector_set(const sorted_vector_set& other, const Allocator& alloc)
      : m_(other.m_, alloc) {}

  /// Move-constructs from another set.
  ///
  /// \param other The set to move from.
  sorted_vector_set(sorted_vector_set&& other) = default;

  /// Move-constructs from another set using the given allocator.
  ///
  /// \param other The set to move from.
  /// \param alloc Allocator to use for the new set.
  sorted_vector_set(sorted_vector_set&& other, const Allocator& alloc) noexcept(
      std::is_nothrow_constructible<EBO, EBO&&, const Allocator&>::value)
      : m_(std::move(other.m_), alloc) {}

  /// Constructs an empty set using the given allocator.
  ///
  /// \param alloc Allocator to use for the new set.
  explicit sorted_vector_set(const Allocator& alloc) : m_(Compare(), alloc) {}

  /// Constructs an empty set with the given comparator and allocator.
  ///
  /// \param comp Comparator that orders the elements.
  /// \param alloc Allocator to use for the new set.
  explicit sorted_vector_set(
      const Compare& comp, const Allocator& alloc = Allocator())
      : m_(comp, alloc) {}

  /// Constructs a set from the elements in the range [first, last).
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  /// \param comp Comparator that orders the elements.
  /// \param alloc Allocator to use for the new set.
  template <class InputIterator>
  sorted_vector_set(
      InputIterator first,
      InputIterator last,
      const Compare& comp = Compare(),
      const Allocator& alloc = Allocator())
      : m_(comp, alloc) {
    // This is linear if [first, last) is already sorted (and if we
    // can figure out the distance between the two iterators).
    insert(first, last);
  }

  /// Constructs a set from the range [first, last) using the given allocator.
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  /// \param alloc Allocator to use for the new set.
  template <class InputIterator>
  sorted_vector_set(
      InputIterator first, InputIterator last, const Allocator& alloc)
      : m_(Compare(), alloc) {
    // This is linear if [first, last) is already sorted (and if we
    // can figure out the distance between the two iterators).
    insert(first, last);
  }

  /// Constructs a set from an initializer list.
  ///
  /// \param list Elements to insert.
  /// \param comp Comparator that orders the elements.
  /// \param alloc Allocator to use for the new set.
  /* implicit */ sorted_vector_set(
      std::initializer_list<value_type> list,
      const Compare& comp = Compare(),
      const Allocator& alloc = Allocator())
      : m_(comp, alloc) {
    insert(list.begin(), list.end());
  }

  /// Constructs a set from an initializer list using the given allocator.
  ///
  /// \param list Elements to insert.
  /// \param alloc Allocator to use for the new set.
  sorted_vector_set(
      std::initializer_list<value_type> list, const Allocator& alloc)
      : m_(Compare(), alloc) {
    insert(list.begin(), list.end());
  }

  /// Construct a sorted_vector_set by stealing the storage of a prefilled
  /// container. The container need not be sorted already. This supports
  /// bulk construction of sorted_vector_set with zero allocations, not counting
  /// those performed by the caller. (The iterator range constructor performs at
  /// least one allocation).
  ///
  /// Note that `sorted_vector_set(const Container& container)` is not provided,
  /// since the purpose of this constructor is to avoid an unnecessary copy.
  ///
  /// \param container Container whose storage is stolen.
  /// \param comp Comparator that orders the elements.
  explicit sorted_vector_set(
      Container&& container, const Compare& comp = Compare())
      : sorted_vector_set(
            sorted_unique,
            detail::as_sorted_unique(std::move(container), comp),
            comp) {}

  /// Construct a sorted_vector_set by stealing the storage of a prefilled
  /// container. Its elements must be sorted and unique, as sorted_unique_t
  /// hints. Supports bulk construction of sorted_vector_set with zero
  /// allocations, not counting those performed by the caller. (The iterator
  /// range constructor performs at least one allocation).
  ///
  /// Note that `sorted_vector_set(sorted_unique_t, const Container& container)`
  /// is not provided, since the purpose of this constructor is to avoid an extra
  /// copy.
  ///
  /// \param tag Tag indicating the input is already sorted and unique.
  /// \param container Container whose storage is stolen.
  /// \param comp Comparator that orders the elements.
  sorted_vector_set(
      sorted_unique_t tag,
      Container&& container,
      const Compare& comp =
          Compare()) noexcept(std::
                                  is_nothrow_constructible<
                                      EBO,
                                      const Compare&,
                                      Container&&>::value)
      : m_(comp, std::move(container)) {
    assert(
        detail::is_sorted_unique(
            m_.cont_.begin(), m_.cont_.end(), value_comp()));
  }

  /// Returns the allocator associated with the underlying container.
  ///
  /// \returns The allocator of the underlying container.
  Allocator get_allocator() const { return m_.cont_.get_allocator(); }

  /// Returns a const reference to the underlying container.
  ///
  /// \returns A const reference to the underlying container.
  const Container& get_container() const noexcept { return m_.cont_; }

  /**
   * Directly mutate the container.
   *
   * Get a guarded reference to the underlying container for direct mutation.
   * sorted_unique_t signals that user will make sure that after the
   * modification the container will have its values as sorted-unique
   * (conforming to container's value_comp). Violating this assumption will
   * result in undefined behavior.
   *
   * This function is not safe to use concurrently with other functions.
   *
   * \param tag Signals that the caller maintains the sorted-unique invariant.
   * \returns A guard granting direct access to the underlying container.
   */
  direct_mutation_guard get_container_for_direct_mutation(
      sorted_unique_t tag) noexcept {
    return direct_mutation_guard{
        m_.cont_, value_comp(), /* range_is_sorted_unique */ true};
  }

  /**
   * Directly mutate the container.
   *
   * Get a guarded reference to the underlying container for direct mutation.
   * The container will initially be sorted and unique. You are not required to
   * maintain the sorted-unique invariant while mutating. When the guard is
   * released, it will sort and unique-ify the container.
   *
   * This function is not safe to use concurrently with other functions.
   *
   * \returns A guard granting direct access to the underlying container.
   */
  direct_mutation_guard get_container_for_direct_mutation() noexcept {
    return direct_mutation_guard{
        m_.cont_, value_comp(), /* range_is_sorted_unique */ false};
  }

  /**
   * Directly swap the container. Similar to swap()
   *
   * \param newContainer The container to swap in; it is sorted and uniqued.
   */
  void swap_container(Container& newContainer) {
    detail::as_sorted_unique(newContainer, value_comp());
    using std::swap;
    swap(m_.cont_, newContainer);
  }
  /// Directly swaps in newContainer, which must already be sorted and unique.
  ///
  /// \param tag Tag indicating the input is already sorted and unique.
  /// \param newContainer The pre-sorted, unique container to swap in.
  void swap_container(sorted_unique_t tag, Container& newContainer) {
    assert(
        detail::is_sorted_unique(
            newContainer.begin(), newContainer.end(), value_comp()));
    using std::swap;
    swap(m_.cont_, newContainer);
  }

  /// Copy-assigns from another set.
  ///
  /// \param other The set to copy from.
  /// \returns A reference to this set.
  sorted_vector_set& operator=(const sorted_vector_set& other) = default;

  /// Move-assigns from another set.
  ///
  /// \param other The set to move from.
  /// \returns A reference to this set.
  sorted_vector_set& operator=(sorted_vector_set&& other) = default;

  /// Replaces the contents with the elements of the initializer list.
  ///
  /// \param ilist Elements to assign.
  /// \returns A reference to this set.
  sorted_vector_set& operator=(std::initializer_list<value_type> ilist) {
    clear();
    insert(ilist.begin(), ilist.end());
    return *this;
  }

  /// Returns the comparator used to order keys.
  ///
  /// \returns The key comparator.
  key_compare key_comp() const { return m_; }
  /// Returns the comparator used to order elements.
  ///
  /// \returns The element comparator.
  value_compare value_comp() const { return m_; }

  /// Returns an iterator to the first element.
  ///
  /// \returns An iterator to the first element.
  iterator begin() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.begin();
  }
  /// Returns an iterator past the last element.
  ///
  /// \returns An iterator past the last element.
  iterator end() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return m_.cont_.end(); }
  /// Returns a const iterator to the first element.
  ///
  /// \returns A const iterator to the first element.
  const_iterator cbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.cbegin();
  }
  /// Returns a const iterator to the first element.
  ///
  /// \returns A const iterator to the first element.
  const_iterator begin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.begin();
  }
  /// Returns a const iterator past the last element.
  ///
  /// \returns A const iterator past the last element.
  const_iterator cend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.cend();
  }
  /// Returns a const iterator past the last element.
  ///
  /// \returns A const iterator past the last element.
  const_iterator end() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.end();
  }
  /// Returns a reverse iterator to the last element.
  ///
  /// \returns A reverse iterator to the last element.
  reverse_iterator rbegin() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rbegin();
  }
  /// Returns a reverse iterator before the first element.
  ///
  /// \returns A reverse iterator before the first element.
  reverse_iterator rend() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rend();
  }
  /// Returns a const reverse iterator to the last element.
  ///
  /// \returns A const reverse iterator to the last element.
  const_reverse_iterator rbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rbegin();
  }
  /// Returns a const reverse iterator before the first element.
  ///
  /// \returns A const reverse iterator before the first element.
  const_reverse_iterator rend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rend();
  }

  /// Removes all elements from the set.
  void clear() { return m_.cont_.clear(); }
  /// Returns the number of elements.
  ///
  /// \returns The number of elements.
  size_type size() const { return m_.cont_.size(); }
  /// Returns the maximum number of elements the set can hold.
  ///
  /// \returns The maximum number of elements.
  size_type max_size() const { return m_.cont_.max_size(); }
  /// Returns true if the set has no elements.
  ///
  /// \returns True if the set is empty.
  bool empty() const { return m_.cont_.empty(); }
  /// Reserves storage for at least s elements.
  ///
  /// \param s Minimum number of elements to reserve storage for.
  void reserve(size_type s) { return m_.cont_.reserve(s); }
  /// Releases unused capacity back to the allocator.
  void shrink_to_fit() { m_.cont_.shrink_to_fit(); }
  /// Returns the number of elements the set can hold without reallocating.
  ///
  /// \returns The current capacity.
  size_type capacity() const { return m_.cont_.capacity(); }

  /// Inserts value if it is absent; returns the position and success flag.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> insert(const value_type& value) {
    iterator it = lower_bound(value);
    if (it == end() || value_comp()(value, *it)) {
      it = get_growth_policy().increase_capacity(m_.cont_, it);
      return std::make_pair(m_.cont_.emplace(it, value), true);
    }
    return std::make_pair(it, false);
  }

  /// Inserts value if it is absent; returns the position and success flag.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> insert(value_type&& value) {
    iterator it = lower_bound(value);
    if (it == end() || value_comp()(value, *it)) {
      it = get_growth_policy().increase_capacity(m_.cont_, it);
      return std::make_pair(m_.cont_.emplace(it, std::move(value)), true);
    }
    return std::make_pair(it, false);
  }

  /// Inserts value using hint as a position guess; returns its position.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, const value_type& value) {
    return detail::insert_with_hint(
        *this, m_.cont_, hint, value, get_growth_policy());
  }

  /// Inserts value using hint as a position guess; returns its position.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, value_type&& value) {
    return detail::insert_with_hint(
        *this, m_.cont_, hint, std::move(value), get_growth_policy());
  }

  /// Inserts every element in the range [first, last).
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  template <class InputIterator>
  void insert(InputIterator first, InputIterator last) {
    detail::bulk_insert(*this, m_.cont_, first, last);
  }

  /// If [first, last) is known to be sorted and unique according to the
  /// comparator (for example if the range comes from a sorted container of the
  /// same type) this version can save unnecessary operations, especially if
  /// *this is empty.
  ///
  /// \param tag Tag indicating the input is already sorted and unique.
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  template <class InputIterator>
  void insert(sorted_unique_t tag, InputIterator first, InputIterator last) {
    detail::bulk_insert(
        *this, m_.cont_, first, last, /* range_is_sorted_unique */ true);
  }

  /// Inserts every element in the initializer list.
  ///
  /// \param ilist Elements to insert.
  void insert(std::initializer_list<value_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  /// emplace isn't better than insert for sorted_vector_set, but aids
  /// compatibility
  ///
  /// \param args Arguments used to construct the element.
  /// \returns A pair of the element position and whether it was inserted.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    folly::aligned_storage_for_t<value_type> b;
    value_type* p = static_cast<value_type*>(static_cast<void*>(&b));
    auto a = get_allocator();
    std::allocator_traits<allocator_type>::construct(
        a, p, std::forward<Args>(args)...);
    auto g = makeGuard([&]() {
      std::allocator_traits<allocator_type>::destroy(a, p);
    });
    return insert(std::move(*p));
  }

  /// Inserts value if it is absent; equivalent to insert.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> emplace(const value_type& value) {
    return insert(value);
  }

  /// Inserts value if it is absent; equivalent to insert.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> emplace(value_type&& value) {
    return insert(std::move(value));
  }

  /// emplace_hint isn't better than insert for sorted_vector_set, but aids
  /// compatibility
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param args Arguments used to construct the element.
  /// \returns An iterator to the inserted or existing element.
  template <typename... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    folly::aligned_storage_for_t<value_type> b;
    value_type* p = static_cast<value_type*>(static_cast<void*>(&b));
    auto a = get_allocator();
    std::allocator_traits<allocator_type>::construct(
        a, p, std::forward<Args>(args)...);
    auto g = makeGuard([&]() {
      std::allocator_traits<allocator_type>::destroy(a, p);
    });
    return insert(hint, std::move(*p));
  }

  /// Inserts value near hint if it is absent; equivalent to insert.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator emplace_hint(const_iterator hint, const value_type& value) {
    return insert(hint, value);
  }

  /// Inserts value near hint if it is absent; equivalent to insert.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator emplace_hint(const_iterator hint, value_type&& value) {
    return insert(hint, std::move(value));
  }

  /// Erases the element matching key and returns the number removed (0 or 1).
  ///
  /// \param key The key to erase.
  /// \returns The number of elements removed (0 or 1).
  size_type erase(const key_type& key) {
    iterator it = find(key);
    if (it == end()) {
      return 0;
    }
    m_.cont_.erase(it);
    return 1;
  }

  /// Erases the element at it and returns an iterator to the next element.
  ///
  /// \param it Iterator to the element to erase.
  /// \returns An iterator to the element after the erased one.
  iterator erase(const_iterator it) { return m_.cont_.erase(it); }

  /// Erases the range [first, last) and returns an iterator to the next element.
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  /// \returns An iterator to the element after the erased range.
  iterator erase(const_iterator first, const_iterator last) {
    return m_.cont_.erase(first, last);
  }

  /// Erases every element for which predicate returns true; returns the count.
  ///
  /// \param container The set to erase elements from.
  /// \param predicate Predicate selecting the elements to erase.
  /// \returns The number of elements removed.
  template <class Predicate>
  friend size_type erase_if(sorted_vector_set& container, Predicate predicate) {
    auto& c = container.m_.cont_;
    const auto preEraseSize = c.size();
    c.erase(std::remove_if(c.begin(), c.end(), std::ref(predicate)), c.end());
    return preEraseSize - c.size();
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  iterator find(const key_type& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  const_iterator find(const key_type& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  template <typename K>
  if_is_transparent<K, iterator> find(const K& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  template <typename K>
  if_is_transparent<K, const_iterator> find(const K& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns the number of elements matching key (0 or 1).
  ///
  /// \param key The key to count.
  /// \returns The number of matching elements (0 or 1).
  size_type count(const key_type& key) const {
    return find(key) == end() ? 0 : 1;
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  std::pair<iterator, iterator> find(
      const key_type& key1, const key_type& key2) {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  std::pair<const_iterator, const_iterator> find(
      const key_type& key1, const key_type& key2) const {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  template <typename K>
  std::pair<if_is_transparent<K, iterator>, if_is_transparent<K, iterator>>
  find(const K& key1, const K& key2) {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  template <typename K>
  std::pair<
      if_is_transparent<K, const_iterator>,
      if_is_transparent<K, const_iterator>>
  find(const K& key1, const K& key2) const {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns the number of elements matching key (0 or 1).
  ///
  /// \param key The key to count.
  /// \returns The number of matching elements (0 or 1).
  template <typename K>
  if_is_transparent<K, size_type> count(const K& key) const {
    return find(key) == end() ? 0 : 1;
  }

  /// Returns true if the set contains key.
  ///
  /// \param key The key to look for.
  /// \returns True if the set contains the key.
  bool contains(const key_type& key) const { return find(key) != end(); }

  /// Returns true if the set contains key.
  ///
  /// \param key The key to look for.
  /// \returns True if the set contains the key.
  template <typename K>
  if_is_transparent<K, bool> contains(const K& key) const {
    return find(key) != end();
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  iterator lower_bound(const key_type& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::lower_bound(begin(), end(), key, key_comp());
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  const_iterator lower_bound(const key_type& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::lower_bound(begin(), end(), key, key_comp());
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  template <typename K>
  if_is_transparent<K, iterator> lower_bound(const K& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::lower_bound(begin(), end(), key, key_comp());
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  template <typename K>
  if_is_transparent<K, const_iterator> lower_bound(const K& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::lower_bound(begin(), end(), key, key_comp());
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  iterator upper_bound(const key_type& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::upper_bound(begin(), end(), key, key_comp());
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  const_iterator upper_bound(const key_type& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::upper_bound(begin(), end(), key, key_comp());
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  template <typename K>
  if_is_transparent<K, iterator> upper_bound(const K& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::upper_bound(begin(), end(), key, key_comp());
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  template <typename K>
  if_is_transparent<K, const_iterator> upper_bound(const K& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return std::upper_bound(begin(), end(), key, key_comp());
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  std::pair<iterator, iterator> equal_range(const key_type& key) {
    return std::equal_range(begin(), end(), key, key_comp());
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  std::pair<const_iterator, const_iterator> equal_range(
      const key_type& key) const {
    return std::equal_range(begin(), end(), key, key_comp());
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  template <typename K>
  if_is_transparent<K, std::pair<iterator, iterator>> equal_range(
      const K& key) {
    return std::equal_range(begin(), end(), key, key_comp());
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  template <typename K>
  if_is_transparent<K, std::pair<const_iterator, const_iterator>> equal_range(
      const K& key) const {
    return std::equal_range(begin(), end(), key, key_comp());
  }

  /// Swaps the contents of this set with another.
  ///
  /// \param o The set to swap contents with.
  void swap(sorted_vector_set& o) noexcept(
      std::is_nothrow_swappable_v<Compare> &&
      noexcept(std::declval<Container&>().swap(o.m_.cont_))) {
    using std::swap; // Allow ADL for swap(); fall back to std::swap().
    Compare& a = m_;
    Compare& b = o.m_;
    swap(a, b);
    m_.cont_.swap(o.m_.cont_);
  }

  /// Returns true if both sets hold equal elements in the same order.
  ///
  /// \param other The set to compare against.
  /// \returns True if the sets are equal.
  bool operator==(const sorted_vector_set& other) const {
    return other.m_.cont_ == m_.cont_;
  }
  /// Returns true if the sets differ.
  ///
  /// \param other The set to compare against.
  /// \returns True if the sets differ.
  bool operator!=(const sorted_vector_set& other) const {
    return !operator==(other);
  }

  /// Compares two sets lexicographically.
  ///
  /// \param other The set to compare against.
  /// \returns True if the ordering relation holds.
  bool operator<(const sorted_vector_set& other) const {
    return m_.cont_ < other.m_.cont_;
  }
  /// Compares two sets lexicographically.
  ///
  /// \param other The set to compare against.
  /// \returns True if the ordering relation holds.
  bool operator>(const sorted_vector_set& other) const { return other < *this; }
  /// Compares two sets lexicographically.
  ///
  /// \param other The set to compare against.
  /// \returns True if the ordering relation holds.
  bool operator<=(const sorted_vector_set& other) const {
    return !operator>(other);
  }
  /// Compares two sets lexicographically.
  ///
  /// \param other The set to compare against.
  /// \returns True if the ordering relation holds.
  bool operator>=(const sorted_vector_set& other) const {
    return !operator<(other);
  }

#if FOLLY_CPLUSPLUS >= 202002L && defined(__cpp_impl_three_way_comparison)
  /// Compares two sets lexicographically by three-way comparison.
  ///
  /// \param lhs The left-hand set.
  /// \param rhs The right-hand set.
  /// \returns The result of the three-way comparison of the two sets.
  template <typename U = Container>
  friend auto operator<=>(
      const sorted_vector_set& lhs, const sorted_vector_set& rhs)
      -> decltype(std::declval<const U&>() <=> std::declval<const U&>()) {
    return lhs.m_.cont_ <=> rhs.m_.cont_;
  }
#endif // FOLLY_CPLUSPLUS >= 202002L && defined(__cpp_impl_three_way_comparison)

  /// Returns a pointer to the underlying contiguous storage.
  ///
  /// \returns A pointer to the underlying contiguous storage.
  const value_type* data() const noexcept { return m_.cont_.data(); }

 private:
  /*
   * This structure derives from the comparison object in order to
   * make use of the empty base class optimization if our comparison
   * functor is an empty class (usual case).
   *
   * Wrapping up this member like this is better than deriving from
   * the Compare object ourselves (there are some perverse edge cases
   * involving virtual functions).
   *
   * More info:  http://www.cantrip.org/emptyopt.html
   */
  struct EBO : Compare {
    explicit EBO(const Compare& c, const Allocator& alloc) noexcept(
        std::is_nothrow_default_constructible<Container>::value)
        : Compare(c), cont_(alloc) {}
    EBO(const EBO& other, const Allocator& alloc) noexcept(
        std::is_nothrow_constructible<
            Container,
            const Container&,
            const Allocator&>::value)
        : Compare(static_cast<const Compare&>(other)),
          cont_(other.cont_, alloc) {}
    EBO(EBO&& other, const Allocator& alloc) noexcept(
        std::is_nothrow_constructible<
            Container,
            Container&&,
            const Allocator&>::value)
        : Compare(static_cast<Compare&&>(other)),
          cont_(std::move(other.cont_), alloc) {}
    EBO(const Compare& c, Container&& cont) noexcept(
        std::is_nothrow_move_constructible<Container>::value)
        : Compare(c), cont_(std::move(cont)) {}
    Container cont_;
  } m_;

  template <typename Self>
  using self_iterator_t = _t<
      std::conditional<std::is_const<Self>::value, const_iterator, iterator>>;

  template <typename Self, typename K>
  static self_iterator_t<Self> find_(Self& self, K const& key) {
    auto end = self.end();
    auto it = self.lower_bound(key);
    if (it == end || !self.key_comp()(key, *it)) {
      return it;
    }
    return end;
  }
  template <typename Self, typename K>
  static std::pair<self_iterator_t<Self>, self_iterator_t<Self>> lower_bound2_(
      Self& self, K const& key1, K const& key2) {
    auto len = self.size();
    auto first = self.begin(), second = self.begin();
    auto c = self.key_comp();
    assert(!c(key2, key1));
    while (true) {
      if (len == 0) {
        return std::make_pair(first, first);
      }
      auto half = len / 2;
      auto middle = first + half;
      if (c(*middle, key1)) {
        first = middle + 1;
        half = len - half - 1;
      } else if (c(*middle, key2)) {
        second = middle + (len & 1);
        len = half;
        break;
      }
      len = half;
    }
    while (len) {
      auto half = len / 2;
      auto middle1 = first + half;
      auto middle2 = second + half;
      if (c(*middle1, key1)) {
        first = middle1 + (len & 1);
      }
      if (c(*middle2, key2)) {
        second = middle2 + (len & 1);
      }
      len = half;
    }
    return std::make_pair(first, second);
  }

  template <typename Self, typename K>
  static std::pair<self_iterator_t<Self>, self_iterator_t<Self>> find2_(
      Self& self, K const& key1, K const& key2) {
    auto end = self.end();
    auto its = lower_bound2_(self, key1, key2);
    if (its.second != end) {
      if (self.key_comp()(key1, *its.first)) {
        its.first = end;
      }
      if (self.key_comp()(key2, *its.second)) {
        its.second = end;
      }
    } else if (its.first != end && self.key_comp()(key1, *its.first)) {
      its.first = end;
    }
    return its;
  }
};

/// Swap function that can be found using ADL.
///
/// \param a The first container to swap.
/// \param b The second container to swap.
template <class T, class C, class A, class G>
inline void swap(
    sorted_vector_set<T, C, A, G>& a, sorted_vector_set<T, C, A, G>& b) {
  return a.swap(b);
}

/// True if T is a sorted_vector_set specialization.
template <typename T>
inline constexpr bool is_sorted_vector_set_v =
    is_instantiation_of_v<sorted_vector_set, T>;

/// Trait that detects a sorted_vector_set specialization.
template <typename T>
struct is_sorted_vector_set : std::bool_constant<is_sorted_vector_set_v<T>> {};

/// A sorted_vector_set backed by a small_vector with inline capacity N.
template <
    class T,
    size_t N = 1,
    class Compare = std::less<T>,
    class Allocator = std::allocator<T>,
    class GrowthPolicy = void,
    class SmallVectorPolicy = void>
using small_sorted_vector_set = sorted_vector_set<
    T,
    Compare,
    Allocator,
    GrowthPolicy,
    folly::small_vector<T, N, SmallVectorPolicy>>;

/// True if T is a sorted_vector_set backed by a small_vector.
template <typename T>
inline constexpr bool is_small_sorted_vector_set_v =
    is_sorted_vector_set_v<T> && is_small_vector_v<typename T::container_type>;

/// Trait that detects a small_sorted_vector_set specialization.
template <typename T>
struct is_small_sorted_vector_set
    : std::bool_constant<is_small_sorted_vector_set_v<T>> {};

#if FOLLY_HAS_MEMORY_RESOURCE

/// Aliases that use a polymorphic allocator.
namespace pmr {

/// A sorted_vector_set that uses a polymorphic allocator.
template <
    class T,
    class Compare = std::less<T>,
    class GrowthPolicy = void,
    class Container = std::vector<T, std::pmr::polymorphic_allocator<T>>>
using sorted_vector_set = folly::sorted_vector_set<
    T,
    Compare,
    std::pmr::polymorphic_allocator<T>,
    GrowthPolicy,
    Container>;

} // namespace pmr

#endif

//////////////////////////////////////////////////////////////////////

/**
 * A sorted_vector_map is similar to a sorted_vector_set but stores
 * <key,value> pairs instead of single elements.
 *
 * @tparam Key           Key type
 * @tparam Value         Value type
 * @tparam Compare       Function that can compare key types and impose
 *                            a strict weak ordering over them.
 * @tparam Allocator     allocation policy
 * @tparam GrowthPolicy  policy object to control growth
 */
template <
    class Key,
    class Value,
    class Compare = std::less<Key>,
    class Allocator = std::allocator<std::pair<Key, Value>>,
    class GrowthPolicy = void,
    class Container = std::vector<std::pair<Key, Value>, Allocator>>
class sorted_vector_map : detail::growth_policy_wrapper<GrowthPolicy> {
  detail::growth_policy_wrapper<GrowthPolicy>& get_growth_policy() {
    return *this;
  }

  template <typename K, typename V, typename C = Compare>
  using if_is_transparent =
      _t<detail::sorted_vector_enable_if_is_transparent<void, C, K, V>>;

  struct EBO;

 public:
  /// The key type of the map.
  using key_type = Key;
  /// The mapped value type of the map.
  using mapped_type = Value;
  /// The element type stored in the container (a key/value pair).
  using value_type = typename Container::value_type;
  /// The comparator type used to order keys.
  using key_compare = Compare;
  /// The allocator type of the underlying container.
  using allocator_type = Allocator;
  /// The underlying container type.
  using container_type = Container;

  /// Comparator that orders elements by their key.
  struct value_compare : private Compare {
    /// Returns true if element a is ordered before element b by key.
    ///
    /// \param a The left-hand element.
    /// \param b The right-hand element.
    /// \returns True if a is ordered before b by key.
    bool operator()(const value_type& a, const value_type& b) const {
      return Compare::operator()(a.first, b.first);
    }

   protected:
    friend class sorted_vector_map;
    /// Constructs a value_compare wrapping the given key comparator.
    ///
    /// \param c The key comparator to wrap.
    explicit value_compare(const Compare& c) : Compare(c) {}
  };

  /// Pointer to an element.
  using pointer = typename Container::pointer;
  /// Pointer to a const element.
  using const_pointer = typename Container::const_pointer;
  /// Reference to an element.
  using reference = typename Container::reference;
  /// Reference to a const element.
  using const_reference = typename Container::const_reference;
  /// Iterator over elements.
  using iterator = typename Container::iterator;
  /// Iterator over const elements.
  using const_iterator = typename Container::const_iterator;
  /// Signed difference type between two iterators.
  using difference_type = typename Container::difference_type;
  /// Unsigned size type.
  using size_type = typename Container::size_type;
  /// Reverse iterator over elements.
  using reverse_iterator = typename Container::reverse_iterator;
  /// Reverse iterator over const elements.
  using const_reverse_iterator = typename Container::const_reverse_iterator;
  /// Guard type for direct mutation of the underlying container.
  using direct_mutation_guard =
      detail::DirectMutationGuard<Container, value_compare>;

  /// Constructs an empty map.
  sorted_vector_map() noexcept(
      std::is_nothrow_constructible<EBO, value_compare, Allocator>::value)
      : m_(value_compare(Compare()), Allocator()) {}

  /// Copy-constructs from another map.
  ///
  /// \param other The map to copy from.
  sorted_vector_map(const sorted_vector_map& other) = default;

  /// Copy-constructs from another map using the given allocator.
  ///
  /// \param other The map to copy from.
  /// \param alloc Allocator to use for the new map.
  sorted_vector_map(const sorted_vector_map& other, const Allocator& alloc)
      : m_(other.m_, alloc) {}

  /// Move-constructs from another map.
  ///
  /// \param other The map to move from.
  sorted_vector_map(sorted_vector_map&& other) = default;

  /// Move-constructs from another map using the given allocator.
  ///
  /// \param other The map to move from.
  /// \param alloc Allocator to use for the new map.
  sorted_vector_map(sorted_vector_map&& other, const Allocator& alloc) noexcept(
      std::is_nothrow_constructible<EBO, EBO&&, const Allocator&>::value)
      : m_(std::move(other.m_), alloc) {}

  /// Constructs an empty map using the given allocator.
  ///
  /// \param alloc Allocator to use for the new map.
  explicit sorted_vector_map(const Allocator& alloc)
      : m_(value_compare(Compare()), alloc) {}

  /// Constructs an empty map with the given comparator and allocator.
  ///
  /// \param comp Comparator that orders the keys.
  /// \param alloc Allocator to use for the new map.
  explicit sorted_vector_map(
      const Compare& comp, const Allocator& alloc = Allocator())
      : m_(value_compare(comp), alloc) {}

  /// Constructs a map from the elements in the range [first, last).
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  /// \param comp Comparator that orders the keys.
  /// \param alloc Allocator to use for the new map.
  template <class InputIterator>
  explicit sorted_vector_map(
      InputIterator first,
      InputIterator last,
      const Compare& comp = Compare(),
      const Allocator& alloc = Allocator())
      : m_(value_compare(comp), alloc) {
    insert(first, last);
  }

  /// Constructs a map from the range [first, last) using the given allocator.
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  /// \param alloc Allocator to use for the new map.
  template <class InputIterator>
  sorted_vector_map(
      InputIterator first, InputIterator last, const Allocator& alloc)
      : m_(value_compare(Compare()), alloc) {
    insert(first, last);
  }

  /// Constructs a map from an initializer list.
  ///
  /// \param list Elements to insert.
  /// \param comp Comparator that orders the keys.
  /// \param alloc Allocator to use for the new map.
  /* implicit */ sorted_vector_map(
      std::initializer_list<value_type> list,
      const Compare& comp = Compare(),
      const Allocator& alloc = Allocator())
      : m_(value_compare(comp), alloc) {
    insert(list.begin(), list.end());
  }

  /// Constructs a map from an initializer list using the given allocator.
  ///
  /// \param list Elements to insert.
  /// \param alloc Allocator to use for the new map.
  sorted_vector_map(
      std::initializer_list<value_type> list, const Allocator& alloc)
      : m_(value_compare(Compare()), alloc) {
    insert(list.begin(), list.end());
  }

  /// Construct a sorted_vector_map by stealing the storage of a prefilled
  /// container. The container need not be sorted already. This supports
  /// bulk construction of sorted_vector_map with zero allocations, not counting
  /// those performed by the caller. (The iterator range constructor performs at
  /// least one allocation).
  ///
  /// Note that `sorted_vector_map(const Container& container)` is not provided,
  /// since the purpose of this constructor is to avoid an unnecessary copy.
  ///
  /// \param container Container whose storage is stolen.
  /// \param comp Comparator that orders the keys.
  explicit sorted_vector_map(
      Container&& container, const Compare& comp = Compare())
      : sorted_vector_map(
            sorted_unique,
            detail::as_sorted_unique(std::move(container), value_compare(comp)),
            comp) {}

  /// Construct a sorted_vector_map by stealing the storage of a prefilled
  /// container. Its elements must be sorted and unique, as sorted_unique_t
  /// hints. Supports bulk construction of sorted_vector_map with zero
  /// allocations, not counting those performed by the caller. (The iterator
  /// range constructor performs at least one allocation).
  ///
  /// Note that `sorted_vector_map(sorted_unique_t, const Container& container)`
  /// is not provided, since the purpose of this constructor is to avoid an extra
  /// copy.
  ///
  /// \param tag Tag indicating the input is already sorted and unique.
  /// \param container Container whose storage is stolen.
  /// \param comp Comparator that orders the keys.
  sorted_vector_map(
      sorted_unique_t tag,
      Container&& container,
      const Compare& comp =
          Compare()) noexcept(std::
                                  is_nothrow_constructible<
                                      EBO,
                                      value_compare,
                                      Container&&>::value)
      : m_(value_compare(comp), std::move(container)) {
    assert(
        detail::is_sorted_unique(
            m_.cont_.begin(), m_.cont_.end(), value_comp()));
  }

  /// Returns the allocator associated with the underlying container.
  ///
  /// \returns The allocator of the underlying container.
  Allocator get_allocator() const { return m_.cont_.get_allocator(); }

  /// Returns a const reference to the underlying container.
  ///
  /// \returns A const reference to the underlying container.
  const Container& get_container() const noexcept { return m_.cont_; }

  /**
   * Directly mutate the container.
   *
   * Get a guarded reference to the underlying container for direct mutation.
   * sorted_unique_t signals that user will make sure that after the
   * modification the container will have its values as sorted-unique
   * (conforming to container's value_comp). Violating this assumption will
   * result in undefined behavior.
   *
   * This function is not safe to use concurrently with other functions.
   *
   * \param tag Signals that the caller maintains the sorted-unique invariant.
   * \returns A guard granting direct access to the underlying container.
   */
  direct_mutation_guard get_container_for_direct_mutation(
      sorted_unique_t tag) noexcept {
    return direct_mutation_guard{
        m_.cont_, value_comp(), /* range_is_sorted_unique */ true};
  }

  /**
   * Directly mutate the container.
   *
   * Get a guarded reference to the underlying container for direct mutation.
   * The container will initially be sorted and unique. You are not required to
   * maintain the sorted-unique invariant while mutating. When the guard is
   * released, it will sort and unique-ify the container.
   *
   * This function is not safe to use concurrently with other functions.
   *
   * \returns A guard granting direct access to the underlying container.
   */
  direct_mutation_guard get_container_for_direct_mutation() noexcept {
    return direct_mutation_guard{
        m_.cont_, value_comp(), /* range_is_sorted_unique */ false};
  }

  /**
   * Directly swap the container. Similar to swap()
   *
   * \param newContainer The container to swap in; it is sorted and uniqued.
   */
  void swap_container(Container& newContainer) {
    detail::as_sorted_unique(newContainer, value_comp());
    using std::swap;
    swap(m_.cont_, newContainer);
  }
  /// Directly swaps in newContainer, which must already be sorted and unique.
  ///
  /// \param tag Tag indicating the input is already sorted and unique.
  /// \param newContainer The pre-sorted, unique container to swap in.
  void swap_container(sorted_unique_t tag, Container& newContainer) {
    assert(
        detail::is_sorted_unique(
            newContainer.begin(), newContainer.end(), value_comp()));
    using std::swap;
    swap(m_.cont_, newContainer);
  }

  /// Copy-assigns from another map.
  ///
  /// \param other The map to copy from.
  /// \returns A reference to this map.
  sorted_vector_map& operator=(const sorted_vector_map& other) = default;

  /// Move-assigns from another map.
  ///
  /// \param other The map to move from.
  /// \returns A reference to this map.
  sorted_vector_map& operator=(sorted_vector_map&& other) = default;

  /// Replaces the contents with the elements of the initializer list.
  ///
  /// \param ilist Elements to assign.
  /// \returns A reference to this map.
  sorted_vector_map& operator=(std::initializer_list<value_type> ilist) {
    clear();
    insert(ilist.begin(), ilist.end());
    return *this;
  }

  /// Returns the comparator used to order keys.
  ///
  /// \returns The key comparator.
  key_compare key_comp() const { return m_; }
  /// Returns the comparator used to order elements.
  ///
  /// \returns The element comparator.
  value_compare value_comp() const { return m_; }

  /// Returns an iterator to the first element.
  ///
  /// \returns An iterator to the first element.
  iterator begin() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.begin();
  }
  /// Returns an iterator past the last element.
  ///
  /// \returns An iterator past the last element.
  iterator end() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] { return m_.cont_.end(); }
  /// Returns a const iterator to the first element.
  ///
  /// \returns A const iterator to the first element.
  const_iterator cbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.cbegin();
  }
  /// Returns a const iterator to the first element.
  ///
  /// \returns A const iterator to the first element.
  const_iterator begin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.begin();
  }
  /// Returns a const iterator past the last element.
  ///
  /// \returns A const iterator past the last element.
  const_iterator cend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.cend();
  }
  /// Returns a const iterator past the last element.
  ///
  /// \returns A const iterator past the last element.
  const_iterator end() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.end();
  }
  /// Returns a reverse iterator to the last element.
  ///
  /// \returns A reverse iterator to the last element.
  reverse_iterator rbegin() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rbegin();
  }
  /// Returns a reverse iterator before the first element.
  ///
  /// \returns A reverse iterator before the first element.
  reverse_iterator rend() [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rend();
  }
  /// Returns a const reverse iterator to the last element.
  ///
  /// \returns A const reverse iterator to the last element.
  const_reverse_iterator crbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.crbegin();
  }
  /// Returns a const reverse iterator to the last element.
  ///
  /// \returns A const reverse iterator to the last element.
  const_reverse_iterator rbegin() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rbegin();
  }
  /// Returns a const reverse iterator before the first element.
  ///
  /// \returns A const reverse iterator before the first element.
  const_reverse_iterator crend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.crend();
  }
  /// Returns a const reverse iterator before the first element.
  ///
  /// \returns A const reverse iterator before the first element.
  const_reverse_iterator rend() const [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return m_.cont_.rend();
  }

  /// Removes all elements from the map.
  void clear() { return m_.cont_.clear(); }
  /// Returns the number of elements.
  ///
  /// \returns The number of elements.
  size_type size() const { return m_.cont_.size(); }
  /// Returns the maximum number of elements the map can hold.
  ///
  /// \returns The maximum number of elements.
  size_type max_size() const { return m_.cont_.max_size(); }
  /// Returns true if the map has no elements.
  ///
  /// \returns True if the map is empty.
  bool empty() const { return m_.cont_.empty(); }
  /// Reserves storage for at least s elements.
  ///
  /// \param s Minimum number of elements to reserve storage for.
  void reserve(size_type s) { return m_.cont_.reserve(s); }
  /// Releases unused capacity back to the allocator.
  void shrink_to_fit() { m_.cont_.shrink_to_fit(); }
  /// Returns the number of elements the map can hold without reallocating.
  ///
  /// \returns The current capacity.
  size_type capacity() const { return m_.cont_.capacity(); }

  /// Inserts value if its key is absent; returns the position and success flag.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> insert(const value_type& value) {
    iterator it = lower_bound(value.first);
    if (it == end() || value_comp()(value, *it)) {
      it = get_growth_policy().increase_capacity(m_.cont_, it);
      return std::make_pair(m_.cont_.emplace(it, value), true);
    }
    return std::make_pair(it, false);
  }

  /// Inserts value if its key is absent; returns the position and success flag.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> insert(value_type&& value) {
    iterator it = lower_bound(value.first);
    if (it == end() || value_comp()(value, *it)) {
      it = get_growth_policy().increase_capacity(m_.cont_, it);
      return std::make_pair(m_.cont_.emplace(it, std::move(value)), true);
    }
    return std::make_pair(it, false);
  }

  /// Inserts value using hint as a position guess; returns its position.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, const value_type& value) {
    return detail::insert_with_hint(
        *this, m_.cont_, hint, value, get_growth_policy());
  }

  /// Inserts value using hint as a position guess; returns its position.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator insert(const_iterator hint, value_type&& value) {
    return detail::insert_with_hint(
        *this, m_.cont_, hint, std::move(value), get_growth_policy());
  }

  /// Inserts every element in the range [first, last).
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  template <class InputIterator>
  void insert(InputIterator first, InputIterator last) {
    detail::bulk_insert(*this, m_.cont_, first, last);
  }

  /// If [first, last) is known to be sorted and unique according to the
  /// comparator (for example if the range comes from a sorted container of the
  /// same type) this version can save unnecessary operations, especially if
  /// *this is empty.
  ///
  /// \param tag Tag indicating the input is already sorted and unique.
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  template <class InputIterator>
  void insert(sorted_unique_t tag, InputIterator first, InputIterator last) {
    detail::bulk_insert(
        *this, m_.cont_, first, last, /* range_is_sorted_unique */ true);
  }

  /// Inserts every element in the initializer list.
  ///
  /// \param ilist Elements to insert.
  void insert(std::initializer_list<value_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  /// emplace isn't better than insert for sorted_vector_map, but aids
  /// compatibility
  ///
  /// \param args Arguments used to construct the element.
  /// \returns A pair of the element position and whether it was inserted.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    folly::aligned_storage_for_t<value_type> b;
    value_type* p = static_cast<value_type*>(static_cast<void*>(&b));
    auto a = get_allocator();
    std::allocator_traits<allocator_type>::construct(
        a, p, std::forward<Args>(args)...);
    auto g = makeGuard([&]() {
      std::allocator_traits<allocator_type>::destroy(a, p);
    });
    return insert(std::move(*p));
  }

  /// Inserts value if its key is absent; equivalent to insert.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> emplace(const value_type& value) {
    return insert(value);
  }

  /// Inserts value if its key is absent; equivalent to insert.
  ///
  /// \param value The element to insert.
  /// \returns A pair of the element position and whether it was inserted.
  std::pair<iterator, bool> emplace(value_type&& value) {
    return insert(std::move(value));
  }

  /// emplace_hint isn't better than insert for sorted_vector_set, but aids
  /// compatibility
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param args Arguments used to construct the element.
  /// \returns An iterator to the inserted or existing element.
  template <typename... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    folly::aligned_storage_for_t<value_type> b;
    value_type* p = static_cast<value_type*>(static_cast<void*>(&b));
    auto a = get_allocator();
    std::allocator_traits<allocator_type>::construct(
        a, p, std::forward<Args>(args)...);
    auto g = makeGuard([&]() {
      std::allocator_traits<allocator_type>::destroy(a, p);
    });
    return insert(hint, std::move(*p));
  }

  /// Inserts value near hint if its key is absent; equivalent to insert.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator emplace_hint(const_iterator hint, const value_type& value) {
    return insert(hint, value);
  }

  /// Inserts value near hint if its key is absent; equivalent to insert.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param value The element to insert.
  /// \returns An iterator to the inserted or existing element.
  iterator emplace_hint(const_iterator hint, value_type&& value) {
    return insert(hint, std::move(value));
  }

  /// Inserts a value constructed in place from args if key k is absent.
  ///
  /// \param k The key to insert under.
  /// \param args Arguments used to construct the mapped value.
  /// \returns A pair of the element position and whether it was inserted.
  template <typename... Args>
  std::pair<iterator, bool> try_emplace(key_type&& k, Args&&... args) {
    return try_emplace_impl(std::move(k), std::forward<Args>(args)...);
  }

  /// Inserts a value constructed in place from args if key k is absent.
  ///
  /// \param k The key to insert under.
  /// \param args Arguments used to construct the mapped value.
  /// \returns A pair of the element position and whether it was inserted.
  template <typename... Args>
  std::pair<iterator, bool> try_emplace(const key_type& k, Args&&... args) {
    return try_emplace_impl(k, std::forward<Args>(args)...);
  }

  /// Inserts obj for key k, or assigns it if k already exists.
  ///
  /// \param k The key to insert or assign under.
  /// \param obj The mapped value to insert or assign.
  /// \returns A pair of the element position and whether it was inserted.
  template <typename M>
  std::pair<iterator, bool> insert_or_assign(const key_type& k, M&& obj) {
    auto itAndInserted = try_emplace(k, std::forward<M>(obj));
    if (!itAndInserted.second) {
      itAndInserted.first->second = std::forward<M>(obj);
    }
    return itAndInserted;
  }

  /// Inserts obj for key k, or assigns it if k already exists.
  ///
  /// \param k The key to insert or assign under.
  /// \param obj The mapped value to insert or assign.
  /// \returns A pair of the element position and whether it was inserted.
  template <typename M>
  std::pair<iterator, bool> insert_or_assign(key_type&& k, M&& obj) {
    auto itAndInserted = try_emplace(std::move(k), std::forward<M>(obj));
    if (!itAndInserted.second) {
      itAndInserted.first->second = std::forward<M>(obj);
    }
    return itAndInserted;
  }

  /// Inserts obj for key k near hint, or assigns it if k already exists.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param k The key to insert or assign under.
  /// \param obj The mapped value to insert or assign.
  /// \returns An iterator to the inserted or assigned element.
  template <class M>
  iterator insert_or_assign(const_iterator hint, const key_type& k, M&& obj) {
    return insert_or_assign_impl(hint, k, std::forward<M>(obj));
  }

  /// Inserts obj for key k near hint, or assigns it if k already exists.
  ///
  /// \param hint Position guess for where the element belongs.
  /// \param k The key to insert or assign under.
  /// \param obj The mapped value to insert or assign.
  /// \returns An iterator to the inserted or assigned element.
  template <class M>
  iterator insert_or_assign(const_iterator hint, key_type&& k, M&& obj) {
    return insert_or_assign_impl(hint, std::move(k), std::forward<M>(obj));
  }

  /// Erases the element matching key and returns the number removed (0 or 1).
  ///
  /// \param key The key to erase.
  /// \returns The number of elements removed (0 or 1).
  size_type erase(const key_type& key) {
    iterator it = find(key);
    if (it == end()) {
      return 0;
    }
    m_.cont_.erase(it);
    return 1;
  }

  /// Erases the element at it and returns an iterator to the next element.
  ///
  /// \param it Iterator to the element to erase.
  /// \returns An iterator to the element after the erased one.
  iterator erase(const_iterator it) { return m_.cont_.erase(it); }

  /// Erases the range [first, last) and returns an iterator to the next element.
  ///
  /// \param first Iterator to the first element of the range.
  /// \param last Iterator past the last element of the range.
  /// \returns An iterator to the element after the erased range.
  iterator erase(const_iterator first, const_iterator last) {
    return m_.cont_.erase(first, last);
  }

  /// Erases every element for which predicate returns true; returns the count.
  ///
  /// \param container The map to erase elements from.
  /// \param predicate Predicate selecting the elements to erase.
  /// \returns The number of elements removed.
  template <class Predicate>
  friend size_type erase_if(sorted_vector_map& container, Predicate predicate) {
    auto& c = container.m_.cont_;
    const auto preEraseSize = c.size();
    c.erase(std::remove_if(c.begin(), c.end(), std::ref(predicate)), c.end());
    return preEraseSize - c.size();
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  iterator find(const key_type& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  const_iterator find(const key_type& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  template <typename K>
  if_is_transparent<K, iterator> find(const K& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns an iterator to the element matching key, or end() if absent.
  ///
  /// \param key The key to look for.
  /// \returns An iterator to the matching element, or end() if absent.
  template <typename K>
  if_is_transparent<K, const_iterator> find(const K& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return find_(*this, key);
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  std::pair<iterator, iterator> find(
      const key_type& key1, const key_type& key2) {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  std::pair<const_iterator, const_iterator> find(
      const key_type& key1, const key_type& key2) const {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  template <typename K>
  std::pair<if_is_transparent<K, iterator>, if_is_transparent<K, iterator>>
  find(const K& key1, const K& key2) {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns iterators to the elements matching key1 and key2 in one pass.
  ///
  /// \param key1 The first key to find.
  /// \param key2 The second key to find.
  /// \returns A pair of iterators to the matching elements, or end() for a missing one.
  template <typename K>
  std::pair<
      if_is_transparent<K, const_iterator>,
      if_is_transparent<K, const_iterator>>
  find(const K& key1, const K& key2) const {
    if (key_comp()(key2, key1)) {
      auto iterators = find2_(*this, key2, key1);
      access::swap(iterators.first, iterators.second);
      return iterators;
    } else {
      return find2_(*this, key1, key2);
    }
  }

  /// Returns the value for key, or throws std::out_of_range if absent.
  ///
  /// \param key The key to look up.
  /// \returns A reference to the mapped value for key.
  mapped_type& at(const key_type& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    iterator it = find(key);
    if (it != end()) {
      return it->second;
    }
    throw_exception<std::out_of_range>("sorted_vector_map::at");
  }

  /// Returns the value for key, or throws std::out_of_range if absent.
  ///
  /// \param key The key to look up.
  /// \returns A reference to the mapped value for key.
  const mapped_type& at(const key_type& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    const_iterator it = find(key);
    if (it != end()) {
      return it->second;
    }
    throw_exception<std::out_of_range>("sorted_vector_map::at");
  }

  /// Returns the number of elements matching key (0 or 1).
  ///
  /// \param key The key to count.
  /// \returns The number of matching elements (0 or 1).
  size_type count(const key_type& key) const {
    return find(key) == end() ? 0 : 1;
  }

  /// Returns the number of elements matching key (0 or 1).
  ///
  /// \param key The key to count.
  /// \returns The number of matching elements (0 or 1).
  template <typename K>
  if_is_transparent<K, size_type> count(const K& key) const {
    return find(key) == end() ? 0 : 1;
  }

  /// Returns true if the map contains key.
  ///
  /// \param key The key to look for.
  /// \returns True if the map contains the key.
  bool contains(const key_type& key) const { return find(key) != end(); }

  /// Returns true if the map contains key.
  ///
  /// \param key The key to look for.
  /// \returns True if the map contains the key.
  template <typename K>
  if_is_transparent<K, bool> contains(const K& key) const {
    return find(key) != end();
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  iterator lower_bound(const key_type& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return lower_bound(*this, key);
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  const_iterator lower_bound(const key_type& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return lower_bound(*this, key);
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  template <typename K>
  if_is_transparent<K, iterator> lower_bound(const K& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return lower_bound(*this, key);
  }

  /// Returns an iterator to the first element not ordered before key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element not ordered before key.
  template <typename K>
  if_is_transparent<K, const_iterator> lower_bound(const K& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return lower_bound(*this, key);
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  iterator upper_bound(const key_type& key) [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return upper_bound(*this, key);
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  const_iterator upper_bound(const key_type& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return upper_bound(*this, key);
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  template <typename K>
  if_is_transparent<K, iterator> upper_bound(const K& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return upper_bound(*this, key);
  }

  /// Returns an iterator to the first element ordered after key.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the first element ordered after key.
  template <typename K>
  if_is_transparent<K, const_iterator> upper_bound(const K& key) const
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    return upper_bound(*this, key);
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  std::pair<iterator, iterator> equal_range(const key_type& key) {
    return equal_range(*this, key);
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  std::pair<const_iterator, const_iterator> equal_range(
      const key_type& key) const {
    return equal_range(*this, key);
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  template <typename K>
  if_is_transparent<K, std::pair<iterator, iterator>> equal_range(
      const K& key) {
    return equal_range(*this, key);
  }

  /// Returns the range of elements matching key.
  ///
  /// \param key The key to search for.
  /// \returns A pair of iterators bounding the elements matching key.
  template <typename K>
  if_is_transparent<K, std::pair<const_iterator, const_iterator>> equal_range(
      const K& key) const {
    return equal_range(*this, key);
  }

  /// Swaps the contents of this map with another.
  ///
  /// Nothrow as long as swap() on the Compare type is nothrow.
  ///
  /// \param o The map to swap contents with.
  void swap(sorted_vector_map& o) {
    using std::swap; // Allow ADL for swap(); fall back to std::swap().
    Compare& a = m_;
    Compare& b = o.m_;
    swap(a, b);
    m_.cont_.swap(o.m_.cont_);
  }

  /// Returns a reference to the value for key, inserting a default if absent.
  ///
  /// \param key The key to look up or insert.
  /// \returns A reference to the mapped value for key.
  mapped_type& operator[](const key_type& key)
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    iterator it = lower_bound(key);
    if (it == end() || key_comp()(key, it->first)) {
      return insert(it, value_type(key, mapped_type()))->second;
    }
    return it->second;
  }

  /// Returns true if both maps hold equal elements in the same order.
  ///
  /// \param other The map to compare against.
  /// \returns True if the maps are equal.
  bool operator==(const sorted_vector_map& other) const {
    return m_.cont_ == other.m_.cont_;
  }
  /// Returns true if the maps differ.
  ///
  /// \param other The map to compare against.
  /// \returns True if the maps differ.
  bool operator!=(const sorted_vector_map& other) const {
    return !operator==(other);
  }

  /// Compares two maps lexicographically.
  ///
  /// \param other The map to compare against.
  /// \returns True if the ordering relation holds.
  bool operator<(const sorted_vector_map& other) const {
    return m_.cont_ < other.m_.cont_;
  }
  /// Compares two maps lexicographically.
  ///
  /// \param other The map to compare against.
  /// \returns True if the ordering relation holds.
  bool operator>(const sorted_vector_map& other) const { return other < *this; }
  /// Compares two maps lexicographically.
  ///
  /// \param other The map to compare against.
  /// \returns True if the ordering relation holds.
  bool operator<=(const sorted_vector_map& other) const {
    return !operator>(other);
  }
  /// Compares two maps lexicographically.
  ///
  /// \param other The map to compare against.
  /// \returns True if the ordering relation holds.
  bool operator>=(const sorted_vector_map& other) const {
    return !operator<(other);
  }

#if FOLLY_CPLUSPLUS >= 202002L && defined(__cpp_impl_three_way_comparison)
  /// Compares two maps lexicographically by three-way comparison.
  ///
  /// \param lhs The left-hand map.
  /// \param rhs The right-hand map.
  /// \returns The result of the three-way comparison of the two maps.
  template <typename U = Container>
  friend auto operator<=>(
      const sorted_vector_map& lhs, const sorted_vector_map& rhs)
      -> decltype(std::declval<const U&>() <=> std::declval<const U&>()) {
    return lhs.m_.cont_ <=> rhs.m_.cont_;
  }
#endif // FOLLY_CPLUSPLUS >= 202002L && defined(__cpp_impl_three_way_comparison)

  /// Returns a pointer to the underlying contiguous storage.
  ///
  /// \returns A pointer to the underlying contiguous storage.
  const value_type* data() const noexcept { return m_.cont_.data(); }

 private:
  // This is to get the empty base optimization; see the comment in
  // sorted_vector_set.
  struct EBO : value_compare {
    explicit EBO(const value_compare& c, const Allocator& alloc) noexcept(
        std::is_nothrow_default_constructible<Container>::value)
        : value_compare(c), cont_(alloc) {}
    EBO(const EBO& other, const Allocator& alloc) noexcept(
        std::is_nothrow_constructible<
            Container,
            const Container&,
            const Allocator&>::value)
        : value_compare(static_cast<const value_compare&>(other)),
          cont_(other.cont_, alloc) {}
    EBO(EBO&& other, const Allocator& alloc) noexcept(
        std::is_nothrow_constructible<
            Container,
            Container&&,
            const Allocator&>::value)
        : value_compare(static_cast<value_compare&&>(other)),
          cont_(std::move(other.cont_), alloc) {}
    EBO(const Compare& c, Container&& cont) noexcept(
        std::is_nothrow_move_constructible<Container>::value)
        : value_compare(c), cont_(std::move(cont)) {}
    Container cont_;
  } m_;

  template <typename Self>
  using self_iterator_t = _t<
      std::conditional<std::is_const<Self>::value, const_iterator, iterator>>;

  template <typename Self, typename K>
  static self_iterator_t<Self> find_(Self& self, K const& key) {
    auto end = self.end();
    auto it = self.lower_bound(key);
    if (it == end || !self.key_comp()(key, it->first)) {
      return it;
    }
    return end;
  }

  template <typename Self, typename K>
  static self_iterator_t<Self> lower_bound(Self& self, K const& key) {
    auto f = [c = self.key_comp()](auto const& a, K const& b) {
      return c(a.first, b);
    };
    return std::lower_bound(self.begin(), self.end(), key, f);
  }

  template <typename Self, typename K>
  static self_iterator_t<Self> upper_bound(Self& self, K const& key) {
    auto f = [c = self.key_comp()](K const& a, auto const& b) {
      return c(a, b.first);
    };
    return std::upper_bound(self.begin(), self.end(), key, f);
  }

  template <typename Self, typename K>
  static std::pair<self_iterator_t<Self>, self_iterator_t<Self>> equal_range(
      Self& self, K const& key) {
    // Note: std::equal_range can't be passed a functor that takes
    // argument types different from the iterator value_type, so we
    // have to do this.
    return {lower_bound(self, key), upper_bound(self, key)};
  }

  template <typename Self, typename K>
  static std::pair<self_iterator_t<Self>, self_iterator_t<Self>> lower_bound2_(
      Self& self, K const& key1, K const& key2) {
    auto len = self.size();
    auto first = self.begin(), second = self.begin();
    auto c = self.key_comp();
    assert(!c(key2, key1));
    while (true) {
      if (len == 0) {
        return std::make_pair(first, first);
      }
      auto half = len / 2;
      auto middle = first + half;
      if (c(middle->first, key1)) {
        first = middle + 1;
        half = len - half - 1;
      } else if (c(middle->first, key2)) {
        second = middle + (len & 1);
        len = half;
        break;
      }
      len = half;
    }
    while (len) {
      auto half = len / 2;
      auto middle1 = first + half;
      auto middle2 = second + half;
      if (c(middle1->first, key1)) {
        first = middle1 + (len & 1);
      }
      if (c(middle2->first, key2)) {
        second = middle2 + (len & 1);
      }
      len = half;
    }
    return std::make_pair(first, second);
  }

  template <typename Self, typename K>
  static std::pair<self_iterator_t<Self>, self_iterator_t<Self>> find2_(
      Self& self, K const& key1, K const& key2) {
    auto end = self.end();
    auto its = lower_bound2_(self, key1, key2);
    if (its.second != end) {
      if (self.key_comp()(key1, its.first->first)) {
        its.first = end;
      }
      if (self.key_comp()(key2, its.second->first)) {
        its.second = end;
      }
    } else if (its.first != end && self.key_comp()(key1, its.first->first)) {
      its.first = end;
    }
    return its;
  }

  template <typename K, typename... Args>
  std::pair<iterator, bool> try_emplace_impl(K&& key, Args&&... args) {
    iterator it = lower_bound(key);
    if (it == end() || key_comp()(key, it->first)) {
      return std::make_pair(
          emplace_hint(
              it,
              std::piecewise_construct,
              std::forward_as_tuple(std::forward<K>(key)),
              std::forward_as_tuple(std::forward<Args>(args)...)),
          true);
    }
    return std::make_pair(it, false);
  }

  template <class K, class M>
  iterator insert_or_assign_impl(const_iterator hint, K&& k, M&& obj) {
    if (hint == end() || key_comp()(k, hint->first)) {
      if (hint == begin() || key_comp()((hint - 1)->first, k)) {
        auto it = get_growth_policy().increase_capacity(m_.cont_, hint);
        return m_.cont_.emplace(
            it, std::make_pair(std::forward<K>(k), std::forward<M>(obj)));
      } else {
        return insert_or_assign(std::forward<K>(k), std::forward<M>(obj)).first;
      }
    }

    if (key_comp()(hint->first, k)) {
      if (hint + 1 == end() || key_comp()(k, (hint + 1)->first)) {
        auto it = get_growth_policy().increase_capacity(m_.cont_, hint + 1);
        return m_.cont_.emplace(
            it, std::make_pair(std::forward<K>(k), std::forward<M>(obj)));
      } else {
        return insert_or_assign(std::forward<K>(k), std::forward<M>(obj)).first;
      }
    }

    // Value and *hint did not compare, so they are equal keys.
    auto it = begin() + std::distance(cbegin(), hint);
    it->second = std::forward<M>(obj);
    return it;
  }
};

/// Swap function that can be found using ADL.
///
/// \param a The first container to swap.
/// \param b The second container to swap.
template <class K, class V, class C, class A, class G>
inline void swap(
    sorted_vector_map<K, V, C, A, G>& a, sorted_vector_map<K, V, C, A, G>& b) {
  return a.swap(b);
}

/// True if T is a sorted_vector_map specialization.
template <typename T>
inline constexpr bool is_sorted_vector_map_v =
    is_instantiation_of_v<sorted_vector_map, T>;

/// Trait that detects a sorted_vector_map specialization.
template <typename T>
struct is_sorted_vector_map : std::bool_constant<is_sorted_vector_map_v<T>> {};

/// A sorted_vector_map backed by a small_vector with inline capacity N.
template <
    class Key,
    class Value,
    size_t N = 1,
    class Compare = std::less<Key>,
    class Allocator = std::allocator<std::pair<Key, Value>>,
    class GrowthPolicy = void,
    class SmallVectorPolicy = void>
using small_sorted_vector_map = sorted_vector_map<
    Key,
    Value,
    Compare,
    Allocator,
    GrowthPolicy,
    folly::small_vector<std::pair<Key, Value>, N, SmallVectorPolicy>>;

/// True if T is a sorted_vector_map backed by a small_vector.
template <typename T>
inline constexpr bool is_small_sorted_vector_map_v =
    is_sorted_vector_map_v<T> && is_small_vector_v<typename T::container_type>;

/// Trait that detects a small_sorted_vector_map specialization.
template <typename T>
struct is_small_sorted_vector_map
    : std::bool_constant<is_small_sorted_vector_map_v<T>> {};

#if FOLLY_HAS_MEMORY_RESOURCE

/// Aliases that use a polymorphic allocator.
namespace pmr {

/// A sorted_vector_map that uses a polymorphic allocator.
template <
    class Key,
    class Value,
    class Compare = std::less<Key>,
    class GrowthPolicy = void,
    class Container = std::vector<
        std::pair<Key, Value>,
        std::pmr::polymorphic_allocator<std::pair<Key, Value>>>>
using sorted_vector_map = folly::sorted_vector_map<
    Key,
    Value,
    Compare,
    std::pmr::polymorphic_allocator<std::pair<Key, Value>>,
    GrowthPolicy,
    Container>;

} // namespace pmr

#endif

//////////////////////////////////////////////////////////////////////

} // namespace folly
