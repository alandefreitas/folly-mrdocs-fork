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

//
// Docs: https://fburl.com/fbcref_dynamic
// 1-minute video primer: https://www.youtube.com/watch?v=3XubaLCDYOM
//

/**
 * @brief A runtime dynamically typed value.
 *
 * dynamic is a runtime dynamically typed value.  It holds types from a specific
 * predetermined set of types: int, double, bool, nullptr_t, string, array,
 * map. In particular, it can be used as a convenient in-memory representation
 * for complete JSON objects.
 *
 * In general, dynamic can be used as if it were the type it represents
 * (although in some cases with a slightly less complete interface than the raw
 * type). If there is a runtime type mismatch, then dynamic will throw a
 * TypeError.
 *
 * See folly/json.h for serialization and deserialization functions for JSON.
 *
 * Additional documentation is in
 * https://github.com/facebook/folly/blob/main/folly/docs/Dynamic.md
 *
 * @refcode folly/docs/examples/folly/dynamic.cpp
 * @struct folly::dynamic
 */
/*
 * Some examples:
 *
 *   dynamic twelve = 12;
 *   dynamic str = "string";
 *   dynamic map = dynamic::object;
 *   map[str] = twelve;
 *   map[str + "another_str"] = dynamic::array("array", "of", 4, "elements");
 *   map.insert("null_element", nullptr);
 *   ++map[str];
 *   assert(map[str] == 13);
 *
 *   // Building a complex object with a sub array inline:
 *   dynamic d = dynamic::object
 *     ("key", "value")
 *     ("key2", dynamic::array("a", "array"))
 *     ;
 */

#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/CppAttributes.h>
#include <folly/Expected.h>
#include <folly/Range.h>
#include <folly/Traits.h>
#include <folly/container/Access.h>
#include <folly/container/F14Map.h>
#include <folly/json_pointer.h>

/// The Folly library namespace.
namespace folly {

//////////////////////////////////////////////////////////////////////

struct const_dynamic_view;
struct dynamic;
struct dynamic_view;
/// Exception thrown when a dynamic is accessed as the wrong type.
struct TypeError;

//////////////////////////////////////////////////////////////////////

/// Implementation details for dynamic.
namespace dynamic_detail {
/// Detects whether a std::string can be constructed from a T's data() and
/// size().
template <typename T>
using detect_construct_string = decltype(std::string(
    FOLLY_DECLVAL(T const&).data(), FOLLY_DECLVAL(T const&).size()));
}

/// A runtime dynamically typed value.
struct dynamic {
  /// The set of types a dynamic can hold.
  enum Type {
    NULLT, ///< A null value.
    ARRAY, ///< An array of dynamics.
    BOOL, ///< A boolean.
    DOUBLE, ///< A double-precision floating-point number.
    INT64, ///< A 64-bit signed integer.
    OBJECT, ///< A map from dynamic keys to dynamic values.
    STRING, ///< A string.
  };
  /// Trait mapping an arithmetic type to the dynamic member it is stored as.
  template <class T, class Enable = void>
  struct NumericTypeHelper;

  /*
   * We support direct iteration of arrays, and indirect iteration of objects.
   * See begin(), end(), keys(), values(), and items() for more.
   *
   * Array iterators dereference as the elements in the array.
   * Object key iterators dereference as the keys in the object.
   * Object value iterators dereference as the values in the object.
   * Object item iterators dereference as pairs of (key, value).
   */
 private:
  using Array = std::vector<dynamic>;

  /*
   * Violating spec, std::vector<bool>::const_reference is not bool in libcpp:
   * http://howardhinnant.github.io/onvectorbool.html
   *
   * This is used to add a public ctor which is only enabled under libcpp taking
   * std::vector<bool>::const_reference without using the preprocessor.
   */
  struct VectorBoolConstRefFake : std::false_type {};
  using VectorBoolConstRefCtorType = std::conditional_t<
      std::is_same<std::vector<bool>::const_reference, bool>::value,
      VectorBoolConstRefFake,
      std::vector<bool>::const_reference>;

 public:
  /// Mutable iterator over array elements.
  using iterator = Array::iterator;
  /// Const iterator over array elements.
  using const_iterator = Array::const_iterator;
  /// The element type held by a dynamic array.
  using value_type = dynamic;

  /// Const iterator over the keys of an object.
  struct const_key_iterator;
  /// Const iterator over the values of an object.
  struct const_value_iterator;
  /// Const iterator over the key-value items of an object.
  struct const_item_iterator;

  /// Mutable iterator over the values of an object.
  struct value_iterator;
  /// Mutable iterator over the key-value items of an object.
  struct item_iterator;

  /*
   * Creation routines for making dynamic objects and arrays.  Objects
   * are maps from key to value (so named due to json-related origins
   * here).
   *
   * Example:
   *
   *   // Make a fairly complex dynamic:
   *   dynamic d = dynamic::object("key", "value1")
   *                              ("key2", dynamic::array("value",
   *                                                      "with",
   *                                                      4,
   *                                                      "words"));
   *
   *   // Build an object in a few steps:
   *   dynamic d = dynamic::object;
   *   d["key"] = 12;
   *   d["something_else"] = dynamic::array(1, 2, 3, nullptr);
   */
 private:
  struct EmptyArrayTag {};
  struct ObjectMaker;

 public:
  /**
   * @brief Used with the array-range ctor.
   */
  struct array_range_construct_t {
   private: // forbid implicit construction with {}
    friend dynamic;
    constexpr array_range_construct_t() = default;
  };
  /// Tag value selecting the array-range constructor.
  static inline constexpr array_range_construct_t array_range_construct{};

  /**
   * Do not use.
   *
   * @param tag Unused empty-array tag.
   * @methodset Array
   */
  static void array(EmptyArrayTag tag);

  /**
   * @brief Construct a dynamic array.
   *
   * Special syntax because `dynamic d = { ... }` dispatches to the copy
   * constructor.
   * See D3013423 and
   * [DR95](http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#1467).
   *
   * @refcode folly/docs/examples/folly/dynamic/array.cpp
   * @methodset Array
   *
   * \param args The elements of the new array.
   * \returns A dynamic array holding the given elements.
   */
  template <class... Args>
  static dynamic array(Args&&... args);

  /**
   * Construct a dynamic object.
   *
   * @refcode folly/docs/examples/folly/dynamic/object.cpp
   * @methodset Object
   *
   * \returns An ObjectMaker building an empty object.
   */
  static ObjectMaker object();

  /// Construct a dynamic object with a single key-value pair.
  ///
  /// \param key The initial key.
  /// \param value The initial value.
  /// \returns An ObjectMaker building the object.
  static ObjectMaker object(dynamic key, dynamic value);

  /**
   * Default constructor, initializes with nullptr.
   */
  dynamic();

  /*
   * String compatibility constructors.
   */
  /// Initializes as an empty string.
  /// \param s A null pointer.
  /* implicit */ dynamic(std::nullptr_t s);
  /// Initializes with strcpy.
  /// \param s A null-terminated C string.
  /* implicit */ dynamic(char const* s);
  /// Initializes as a string.
  /// \param s The string value.
  /* implicit */ dynamic(std::string s);
  /// Initializes as a string.
  /// \param s A string-like value convertible to std::string.
  template <
      typename Stringish,
      typename = std::enable_if_t<
          is_detected_v<dynamic_detail::detect_construct_string, Stringish>>>
  /* implicit */ dynamic(Stringish&& s);

  /*
   * This is part of the plumbing for array() and object(), above.
   * Used to create a new array or object dynamic.
   */
  /// Plumbing for array() construction.
  /// \param fn The array-tag function returned by dynamic::array plumbing.
  /* implicit */ dynamic(void (*fn)(EmptyArrayTag));
  /// Plumbing for object() construction.
  /// \param fn The object-maker factory function.
  /* implicit */ dynamic(ObjectMaker (*fn)());
  /// Plumbing for object() construction.
  /// \param maker The ObjectMaker to build from (deleted lvalue overload).
  /* implicit */ dynamic(ObjectMaker const& maker) = delete;
  /// Plumbing for object() construction.
  /// \param maker The ObjectMaker to build from.
  /* implicit */ dynamic(ObjectMaker&& maker);

  /**
   * Constructor for integral and float types.
   * Other types are SFINAEd out with NumericTypeHelper.
   *
   * \param t The arithmetic value to store.
   */
  template <class T, class NumericType = typename NumericTypeHelper<T>::type>
  /* implicit */ dynamic(T t);

  /**
   * Special handling for vector<bool>.
   *
   * If v is vector<bool>, v[idx] is a proxy object implicitly convertible to
   * bool. Calling a function f(dynamic) with f(v[idx]) would require a double
   * implicit conversion (reference -> bool -> dynamic) which is not allowed,
   * hence we explicitly accept the reference proxy.
   *
   * \param b The vector<bool> element proxy to store as a boolean.
   */
  /* implicit */ dynamic(std::vector<bool>::reference b);
  /// Special handling for vector<bool>::const_reference.
  /// \param b The vector<bool> const element proxy to store as a boolean.
  /* implicit */ dynamic(VectorBoolConstRefCtorType b);

  /**
   * Create a dynamic that is an array of the values from the supplied
   * iterator range. Used to construct a dynamic of array type from a supplied
   * pair of iterators or from a range-like object. Four equivalent forms - see
   * examples.
   *
   * Examples:
   *
   *   auto arr1 = dynamic(
   *       dynamic::array_range_construct, rng.begin(), rng.end());
   *   auto arr2 = dynamic(dynamic::array_range_construct, rng);
   *   auto arr3 = dynamic::array_range(rng.begin(), rng.end());
   *   auto arr4 = dynamic::array_range(rng);
   *
   * \param tag The array-range construction tag.
   * \param first The iterator to the first element of the range.
   * \param last The iterator past the last element of the range.
   */
  template <class Iterator>
  dynamic(array_range_construct_t tag, Iterator first, Iterator last);
  /// Constructs an array dynamic from the values of a range-like object.
  ///
  /// \param tag The array-range construction tag.
  /// \param range The range whose elements populate the array.
  template <class Range>
  dynamic(array_range_construct_t tag, Range&& range)
      : dynamic(tag, access::begin(range), access::end(range)) {}
  /// Constructs an array dynamic from an iterator range.
  ///
  /// \param first The iterator to the first element of the range.
  /// \param last The iterator past the last element of the range.
  /// \returns A dynamic array holding the range's elements.
  template <class Iterator>
  static dynamic array_range(Iterator first, Iterator last) {
    return dynamic(array_range_construct, first, last);
  }
  /// Constructs an array dynamic from a range-like object.
  ///
  /// \param range The range whose elements populate the array.
  /// \returns A dynamic array holding the range's elements.
  template <class Range>
  static dynamic array_range(Range&& range) {
    return dynamic(array_range_construct, std::forward<Range>(range));
  }

  /// Copy constructor.
  /// \param other The dynamic to copy.
  dynamic(dynamic const& other);
  /// Move constructor.
  /// \param other The dynamic to move from.
  dynamic(dynamic&& other) noexcept;
  /// Destroys the dynamic and any value it holds.
  ~dynamic() noexcept;

  /**
   * Deep equality comparison.  This will compare all the way down
   * an object or array, and is potentially expensive.
   *
   * NOTE: Implicit conversion will be done between ints and doubles, so numeric
   * equality will apply between those cases. Other dynamic value comparisons of
   * different types will always return false.
   *
   * \param a The left-hand dynamic.
   * \param b The right-hand dynamic.
   * \returns True if the two dynamics are deeply equal.
   */
  friend bool operator==(dynamic const& a, dynamic const& b);
  /// Deep inequality comparison.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns True if the two dynamics are not deeply equal.
  friend bool operator!=(dynamic const& a, dynamic const& b) {
    return !(a == b);
  }

  /**
   * Orders two dynamics.
   *
   * For all types except object this returns the natural ordering on
   * those types.  For objects, we throw TypeError.
   *
   * NOTE: Implicit conversion will be done between ints and doubles, so numeric
   * ordering will apply between those cases. Other dynamic value comparisons of
   * different types will maintain consistent ordering within a binary run.
   *
   * \param a The left-hand dynamic.
   * \param b The right-hand dynamic.
   * \returns True if a orders before b.
   */
  friend bool operator<(dynamic const& a, dynamic const& b);
  /// Greater-than comparison.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns True if a orders after b.
  friend bool operator>(dynamic const& a, dynamic const& b) { return b < a; }
  /// Less-than-or-equal comparison.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns True if a does not order after b.
  friend bool operator<=(dynamic const& a, dynamic const& b) {
    return !(b < a);
  }
  /// Greater-than-or-equal comparison.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns True if a does not order before b.
  friend bool operator>=(dynamic const& a, dynamic const& b) {
    return !(a < b);
  }

  /*
   * General operators.
   *
   * These throw TypeError when used with types or type combinations
   * that don't support them.
   *
   * These functions may also throw if you use 64-bit integers with
   * doubles when the integers are too big to fit in a double.
   */
  /// Adds another dynamic into this one.
  /// @methodset Op
  /// \param other The dynamic to add.
  /// \returns A reference to this dynamic.
  dynamic& operator+=(dynamic const& other);
  /// Subtracts another dynamic from this one.
  /// @methodset Op
  /// \param other The dynamic to subtract.
  /// \returns A reference to this dynamic.
  dynamic& operator-=(dynamic const& other);
  /// Multiplies this dynamic by another.
  /// @methodset Op
  /// \param other The dynamic to multiply by.
  /// \returns A reference to this dynamic.
  dynamic& operator*=(dynamic const& other);
  /// Divides this dynamic by another.
  /// @methodset Op
  /// \param other The dynamic to divide by.
  /// \returns A reference to this dynamic.
  dynamic& operator/=(dynamic const& other);
  /// Assigns the remainder of dividing this dynamic by another.
  /// @methodset Op
  /// \param other The dynamic to take the remainder against.
  /// \returns A reference to this dynamic.
  dynamic& operator%=(dynamic const& other);
  /// Bitwise-ORs another dynamic into this one.
  /// @methodset Op
  /// \param other The dynamic to OR in.
  /// \returns A reference to this dynamic.
  dynamic& operator|=(dynamic const& other);
  /// Bitwise-ANDs another dynamic into this one.
  /// @methodset Op
  /// \param other The dynamic to AND in.
  /// \returns A reference to this dynamic.
  dynamic& operator&=(dynamic const& other);
  /// Bitwise-XORs another dynamic into this one.
  /// @methodset Op
  /// \param other The dynamic to XOR in.
  /// \returns A reference to this dynamic.
  dynamic& operator^=(dynamic const& other);
  /// Pre-increments an integer dynamic.
  /// @methodset Op
  /// \returns A reference to this dynamic.
  dynamic& operator++();
  /// Pre-decrements an integer dynamic.
  /// @methodset Op
  /// \returns A reference to this dynamic.
  dynamic& operator--();

  /// Adds two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The sum.
  friend dynamic operator+(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) += b);
  }
  /// Subtracts two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The difference.
  friend dynamic operator-(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) -= b);
  }
  /// Multiplies two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The product.
  friend dynamic operator*(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) *= b);
  }
  /// Divides two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The quotient.
  friend dynamic operator/(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) /= b);
  }
  /// Computes the remainder of dividing two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The remainder.
  friend dynamic operator%(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) %= b);
  }
  /// Bitwise-ORs two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The bitwise OR.
  friend dynamic operator|(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) |= b);
  }
  /// Bitwise-ANDs two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The bitwise AND.
  friend dynamic operator&(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) &= b);
  }
  /// Bitwise-XORs two dynamics.
  /// \param a The left-hand dynamic.
  /// \param b The right-hand dynamic.
  /// \returns The bitwise XOR.
  friend dynamic operator^(dynamic const& a, dynamic const& b) {
    return std::move(copy(a) ^= b);
  }

  /// Adds two dynamics, reusing the left-hand operand's storage.
  /// \param a The left-hand dynamic, consumed by the operation.
  /// \param b The right-hand dynamic.
  /// \returns The sum.
  friend dynamic operator+(dynamic&& a, dynamic const& b) {
    return std::move(a += b);
  }

  /// Post-increments an integer dynamic.
  /// @methodset Op
  /// \param dummy Unused tag selecting the post-increment overload.
  /// \returns A copy of the dynamic before incrementing.
  dynamic operator++(int dummy) {
    auto self = *this;
    return ++*this, self;
  }
  /// Post-decrements an integer dynamic.
  /// @methodset Op
  /// \param dummy Unused tag selecting the post-decrement overload.
  /// \returns A copy of the dynamic before decrementing.
  dynamic operator--(int dummy) {
    auto self = *this;
    return --*this, self;
  }

  /**
   * Copy-assignment from another dynamic.
   *
   * Because of the implicit conversion to dynamic from its potential types, you
   * can use this to change the type pretty intuitively.
   *
   * Basic guarantee only.
   *
   * \param other The dynamic to copy.
   * \returns A reference to this dynamic.
   */
  dynamic& operator=(dynamic const& other);
  /// Move-assignment from another dynamic.
  /// \param other The dynamic to move from.
  /// \returns A reference to this dynamic.
  dynamic& operator=(dynamic&& other) noexcept;

  /**
   * Assigns a cheap primitive value without creating a temporary dynamic.
   *
   * \param t The arithmetic value to assign.
   * \returns A reference to this dynamic.
   */
  template <class T, class NumericType = typename NumericTypeHelper<T>::type>
  dynamic& operator=(T t);

  /// Assigns null, making this an empty string dynamic.
  /// \param nullValue A null pointer.
  /// \returns A reference to this dynamic.
  dynamic& operator=(std::nullptr_t nullValue);

  /**
   * Streams a dynamic to an output stream.
   *
   * For simple dynamics (not arrays or objects), this prints the
   * value to an std::ostream in the expected way.  Respects the
   * formatting manipulators that have been sent to the stream
   * already.
   *
   * If the dynamic holds an object or array, this prints them in a
   * format very similar to JSON.  (It will in fact actually be JSON
   * as long as the dynamic validly represents a JSON object---i.e. it
   * can't have non-string keys.)
   *
   * \param os The output stream to write to.
   * \param d The dynamic to print.
   * \returns A reference to the output stream.
   */
  friend std::ostream& operator<<(std::ostream& os, dynamic const& d);

  /**
   * @brief Type test.
   *
   * Returns true if this dynamic is of the specified type.
   *
   * @returns True if this dynamic is of the specified type.
   * @methodset Typing
   */
  bool isString() const;
  /// @copydoc isString
  bool isObject() const;
  /// @copydoc isString
  bool isBool() const;
  /// @copydoc isString
  bool isNull() const;
  /// @copydoc isString
  bool isArray() const;
  /// @copydoc isString
  bool isDouble() const;
  /// @copydoc isString
  bool isInt() const;

  /**
   * @copydoc isString
   *
   * @return `isInt() || isDouble()`.
   */
  bool isNumber() const;

  /**
   * The type of this dynamic.
   *
   * @returns The Type enumerator for the currently held value.
   * @methodset Typing
   */
  Type type() const;

  /**
   * The type of this dynamic as a printable string.
   *
   * @returns The name of the currently held type.
   * @methodset Typing
   */
  const char* typeName() const;

  /**
   * Type conversion.
   *
   * Extract a value while trying to convert to the specified type.
   * Throws exceptions if we cannot convert from the real type to the
   * requested type.
   *
   * C++ will implicitly convert between bools, ints, and doubles; these
   * conversion functions also try to convert between arithmetic types and
   * strings. E.g. dynamic d = "12"; d.asDouble() -> 12.0.
   *
   * Note: you can only use this to access integral types or strings,
   * since arrays and objects are generally best dealt with as a
   * dynamic.
   *
   * @returns The value converted to the requested type.
   * @methodset Conversion
   */
  std::string asString() const;
  /// @copydoc asString
  double asDouble() const;
  /// @copydoc asString
  int64_t asInt() const;
  /// @copydoc asString
  bool asBool() const;

  /**
   * Type extraction.
   *
   * Extract the value stored in this dynamic without type conversion.
   *
   * These will throw a TypeError if the dynamic has a different type.
   *
   * @returns The value stored in this dynamic.
   * @methodset Extraction
   */
  const std::string& getString() const& [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// @copydoc getString
  double getDouble() const&;
  /// @copydoc getString
  int64_t getInt() const&;
  /// @copydoc getString
  bool getBool() const&;
  /// Extracts the value stored in this dynamic without type conversion.
  ///
  /// \returns The value stored in this dynamic.
  std::string& getString() & [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// @copydoc getString
  double& getDouble() &;
  /// @copydoc getString
  int64_t& getInt() &;
  /// @copydoc getString
  bool& getBool() &;
  /// Extracts the value stored in this dynamic without type conversion.
  ///
  /// \returns The value stored in this dynamic.
  std::string&& getString() &&;
  /// @copydoc getString
  double getDouble() &&;
  /// @copydoc getString
  int64_t getInt() &&;
  /// @copydoc getString
  bool getBool() &&;

  /**
   * Get the c_str pointer.
   *
   * It is occasionally useful to access a string's internal pointer
   * directly, without the type conversion of `asString()`.
   *
   * These will throw a TypeError if the dynamic is not a string.
   *
   * @returns A pointer to the string's internal character buffer.
   * @methodset Extraction
   */
  const char* c_str() const&;
  /// Deleted to prevent returning a pointer into a temporary dynamic.
  ///
  /// \returns Never returns; this overload is deleted.
  const char* c_str() && = delete;
  /// @copydoc c_str
  StringPiece stringPiece() const;

  /**
   * Tests for emptiness.
   *
   * Returns: true if this dynamic is null, an empty array, an empty
   * object, or an empty string.
   *
   * @returns True if this dynamic is null or an empty container or string.
   * @methodset Container
   */
  bool empty() const;

  /**
   * Get size.
   *
   * If this is an array or an object, returns the number of elements
   * contained.  If it is a string, returns the length.  Otherwise
   * throws TypeError.
   *
   * @returns The number of elements, or the string length.
   * @methodset Container
   */
  std::size_t size() const;

  /**
   * Array iteration.
   *
   * You can iterate over the values of the array.  Calling these on
   * non-arrays will throw a TypeError.
   *
   * @returns An iterator to the corresponding position in the array.
   * @methodset Iteration
   */
  const_iterator begin() const;
  /// @copydoc begin
  const_iterator end() const;
  /// Array iteration.
  ///
  /// \returns An iterator to the corresponding position in the array.
  iterator begin();
  /// @copydoc begin
  iterator end();

 private:
  /*
   * Helper object returned by keys(), values(), and items().
   */
  template <class T>
  struct IterableProxy;

  /*
   * Helper for heterogeneous lookup and mutation on objects: at(), find(),
   * count(), erase(), operator[]
   */
  template <typename K, typename T>
  using IfIsNonStringDynamicConvertible = std::enable_if_t<
      !std::is_convertible<K, StringPiece>::value &&
          std::is_convertible<K, dynamic>::value,
      T>;

  template <typename K, typename T>
  using IfNotIterator =
      std::enable_if_t<!std::is_convertible<K, iterator>::value, T>;

 public:
  /*
   * You can iterate over the keys, values, or items (std::pair of key and
   * value) in an object.  Calling these on non-objects will throw a TypeError.
   */
  /**
   * Get the keys of an object.
   *
   * Return an iterable interface for the object's keys.
   *
   * @returns An iterable interface over the object's keys.
   * @methodset Iteration
   */
  IterableProxy<const_key_iterator> keys() const;
  /**
   * Get the values of an object.
   *
   * Return an iterable interface for the object's values, without their
   * associated key.
   *
   * @returns An iterable interface over the object's values.
   * @methodset Iteration
   */
  IterableProxy<const_value_iterator> values() const;
  /**
   * Get key-value items of an object.
   *
   * Return an iterable interface for the object's key-value pairs.
   * The type of this iterable is `(const dynamic&, dynamic&)`.
   *
   * @returns An iterable interface over the object's key-value pairs.
   * @methodset Iteration
   */
  IterableProxy<const_item_iterator> items() const;
  /// Get the values of an object.
  ///
  /// \returns An iterable interface over the object's values.
  IterableProxy<value_iterator> values();
  /// Get key-value items of an object.
  ///
  /// \returns An iterable interface over the object's key-value pairs.
  IterableProxy<item_iterator> items();

  /**
   * Find by key.
   *
   * AssociativeContainer-style find interface for objects.  Throws if
   * this is not an object.
   *
   * @param key The key to search for.
   * @return items().end() if the key is not present, or a
   * const_item_iterator pointing to the item.
   *
   * @methodset Object
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, const_item_iterator> find(K&& key) const;
  /// AssociativeContainer-style find interface for objects.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the item, or items().end() if not present.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, item_iterator> find(K&& key);
  /// AssociativeContainer-style find interface for objects.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the item, or items().end() if not present.
  const_item_iterator find(StringPiece key) const;
  /// AssociativeContainer-style find interface for objects.
  ///
  /// \param key The key to search for.
  /// \returns An iterator to the item, or items().end() if not present.
  item_iterator find(StringPiece key);

  /**
   * Count by key.
   *
   * If this is an object, returns whether it contains a field with
   * the given name.
   * If this is an array, returns the number of elements matching
   * the supplied value.
   * Otherwise throws TypeError.
   *
   * @param key The key or value to count.
   * @return The number of matching fields or elements.
   * @methodset Object
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, std::size_t> count(K&& key) const;
  /// Count by key.
  ///
  /// \param key The key to count.
  /// \returns The number of matching fields.
  std::size_t count(StringPiece key) const;

  /**
   * Check if key exists.
   *
   * If this is an object, returns whether it contains a field with
   * the given name.
   * If this is an array, returns whether it contains a value matching
   * the supplied value.
   * Otherwise throws TypeError.
   *
   * @param key The key or value to look for.
   * @return True if a matching field or element exists.
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, bool> contains(K&& key) const;
  /// Check if key exists.
  ///
  /// \param key The key to look for.
  /// \returns True if a matching field exists.
  bool contains(StringPiece key) const;

 private:
  dynamic const& atImpl(dynamic const&) const&;

 public:
  /**
   * Access sub-field or index.
   *
   * For objects or arrays, provides access to sub-fields by index or
   * field name.
   *
   * Using these with dynamic objects that are not arrays or objects
   * will throw a TypeError.  Using an index that is out of range or
   * object-element that's not present throws std::out_of_range.
   *
   * @param key The index or field name to access.
   * @return A reference to the accessed sub-field or element.
   * @methodset Element access
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic const&> at(
      K&& key) const& [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Access sub-field or index.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed sub-field or element.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic&> at(
      K&& key) & [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Access sub-field or index.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed sub-field or element.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic&&> at(K&& key) &&;

  /// Access sub-field or index.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed sub-field or element.
  dynamic const& at(StringPiece key) const& [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Access sub-field or index.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed sub-field or element.
  dynamic& at(StringPiece key) & [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Access sub-field or index.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed sub-field or element.
  dynamic&& at(StringPiece key) &&;

  /*
   * Locate element using JSON pointer, per RFC 6901
   */

  /// Reason a JSON pointer failed to resolve against a dynamic.
  enum class json_pointer_resolution_error_code : uint8_t {
    other = 0, ///< An unspecified error.
    key_not_found, ///< Key not found in object.
    index_out_of_bounds, ///< Array index out of bounds.
    append_requested, ///< Special index "-" requesting append.
    index_not_numeric, ///< Indexes in arrays must be numeric.
    index_has_leading_zero, ///< Indexes in arrays must not have a leading zero.
    element_not_object_or_array, ///< Element is neither an object nor an array.
    json_pointer_out_of_bounds, ///< Hit document boundary before pointer end.
  };

  /// Error describing why a JSON pointer failed to resolve.
  template <typename Dynamic>
  struct json_pointer_resolution_error {
    /// Error code encountered while resolving the JSON pointer.
    json_pointer_resolution_error_code error_code{};
    /// Index of the JSON pointer's token that caused the error.
    size_t index{0};
    /// Last correctly resolved element; useful, for example, to append the
    /// last element to the array.
    Dynamic* context{nullptr};
  };

  /// A value located by resolving a JSON pointer, with its parent context.
  template <typename Dynamic>
  struct json_pointer_resolved_value {
    /// Parent element of the value in the dynamic, if it exists.
    Dynamic* parent{nullptr};
    /// Pointer to the value itself.
    Dynamic* value{nullptr};
    /// If the parent is an object, the key used to get the value.
    StringPiece parent_key;
    /// If the parent is an array, the index used to get the value.
    size_t parent_index{0};
  };

  // clang-format off
  /// The result of resolving a JSON pointer: either the resolved value or a
  /// resolution error.
  template <typename Dynamic>
  using resolved_json_pointer = Expected<
      json_pointer_resolved_value<Dynamic>,
      json_pointer_resolution_error<Dynamic>>;

  /**
   * Get JSON Pointer.
   *
   * See [Relative JSON Pointers RFC 6901](https://json-schema.org/draft/2019-09/relative-json-pointer.html)
   *
   * @param ptr The JSON pointer locating the element.
   * @return The resolved value with its parent context, or a resolution error.
   * @methodset Element access
   */
  resolved_json_pointer<dynamic const>
  try_get_ptr(json_pointer const& ptr) const&;
  /// Resolves a JSON pointer against this dynamic.
  ///
  /// \param ptr The JSON pointer locating the element.
  /// \returns The resolved value with its parent context, or a resolution
  ///     error.
  resolved_json_pointer<dynamic>
  try_get_ptr(json_pointer const& ptr) &;
  /// Resolves a JSON pointer against this dynamic.
  ///
  /// \param ptr The JSON pointer locating the element.
  /// \returns The resolved value with its parent context, or a resolution
  ///     error.
  resolved_json_pointer<dynamic const>
  try_get_ptr(json_pointer const& ptr) const&& = delete;
  /// Resolves a JSON pointer against this dynamic.
  ///
  /// \param ptr The JSON pointer locating the element.
  /// \returns The resolved value with its parent context, or a resolution
  ///     error.
  resolved_json_pointer<dynamic const>
  try_get_ptr(json_pointer const& ptr) && = delete;
  // clang-format on

  /**
   * Nullable access.
   *
   * Like `at` (for objects and arrays) and `try_get_ptr` (for json_pointer
   * lookup), but returns nullptr if the element cannot be found instead of
   * throwing a TypeError.
   *
   * For the JSON Pointer overloads, throws if pointer does not match the shape
   * of the document, e.g. uses string to index in array.
   *
   * @param ptr The JSON pointer locating the element.
   * @return A pointer to the located element, or nullptr if absent.
   * @methodset Element access
   */
  const dynamic* get_ptr(json_pointer const& ptr) const&;
  /// Nullable JSON pointer access.
  ///
  /// \param ptr The JSON pointer locating the element.
  /// \returns A pointer to the located element, or nullptr if absent.
  dynamic* get_ptr(json_pointer const& ptr) &;
  /// Nullable JSON pointer access.
  ///
  /// \param ptr The JSON pointer locating the element.
  /// \returns A pointer to the located element, or nullptr if absent.
  const dynamic* get_ptr(json_pointer const& ptr) const&& = delete;
  /// Nullable JSON pointer access.
  ///
  /// \param ptr The JSON pointer locating the element.
  /// \returns A pointer to the located element, or nullptr if absent.
  dynamic* get_ptr(json_pointer const& ptr) && = delete;

 private:
  const dynamic* get_ptrImpl(dynamic const&) const&;

 public:
  /**
   * Access a sub-field or index by key, returning nullptr if absent.
   *
   * Like 'at', above, except it returns either a pointer to the contained
   * object or nullptr if it wasn't found. This allows a key to be tested for
   * containment and retrieved in one operation. Example:
   *
   *   if (auto* found = d.get_ptr(key))
   *     // use *found;
   *
   * Using these with dynamic objects that are not arrays or objects
   * will throw a TypeError.
   *
   * @param key The index or field name to access.
   * @return A pointer to the element, or nullptr if absent.
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, const dynamic*> get_ptr(K&& key) const&;
  /// Nullable access to a sub-field or index by key.
  ///
  /// \param key The index or field name to access.
  /// \returns A pointer to the element, or nullptr if absent.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic*> get_ptr(K&& key) &;
  /// Nullable access to a sub-field or index by key.
  ///
  /// \param key The index or field name to access.
  /// \returns A pointer to the element, or nullptr if absent.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic*> get_ptr(K&& key) && = delete;

  /// Nullable access to a sub-field or index by key.
  ///
  /// \param key The index or field name to access.
  /// \returns A pointer to the element, or nullptr if absent.
  const dynamic* get_ptr(StringPiece key) const&;
  /// Nullable access to a sub-field or index by key.
  ///
  /// \param key The index or field name to access.
  /// \returns A pointer to the element, or nullptr if absent.
  dynamic* get_ptr(StringPiece key) &;
  /// Nullable access to a sub-field or index by key.
  ///
  /// \param key The index or field name to access.
  /// \returns A pointer to the element, or nullptr if absent.
  dynamic* get_ptr(StringPiece key) && = delete;

  /**
   * Element lookup.
   *
   * This works for access to both objects and arrays.
   *
   * In the case of an array, the index must be an integer, and this
   * will throw std::out_of_range if it is less than zero or greater
   * than size().
   *
   * In the case of an object, the non-const overload inserts a null
   * value if the key isn't present.  The const overload will throw
   * std::out_of_range if the key is not present.
   *
   * These functions do not invalidate iterators except when a null value
   * is inserted into an object as described above.
   *
   * @param key The index or field name to access.
   * @return A reference to the accessed element.
   * @methodset Element access
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic&> operator[](
      K&& key) & [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Element lookup for objects and arrays.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed element.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic const&> operator[](
      K&& key) const& [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Element lookup for objects and arrays.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed element.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic&&> operator[](K&& key) &&;

  /// Element lookup for objects and arrays.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed element.
  dynamic& operator[](StringPiece key) & [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Element lookup for objects and arrays.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed element.
  dynamic const& operator[](
      StringPiece key) const& [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]];
  /// Element lookup for objects and arrays.
  ///
  /// \param key The index or field name to access.
  /// \returns A reference to the accessed element.
  dynamic&& operator[](StringPiece key) &&;

  /**
   * Defaulted lookup.
   *
   * getDefault will return the value associated with the supplied key, the
   * supplied default otherwise.
   *
   * Only defined for objects, throws TypeError otherwise.
   *
   * @methodset Object
   *
   * \param k The key to look up.
   * \param v The default value returned when the key is absent.
   * \returns The value for the key, or the supplied default.
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic> getDefault(
      K&& k, const dynamic& v = dynamic::object) const&;
  /// Defaulted lookup: returns the value for the key, or the default.
  ///
  /// \param k The key to look up.
  /// \param v The default value returned when the key is absent.
  /// \returns The value for the key, or the supplied default.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic> getDefault(
      K&& k, dynamic&& v) const&;
  /// Defaulted lookup: returns the value for the key, or the default.
  ///
  /// \param k The key to look up.
  /// \param v The default value returned when the key is absent.
  /// \returns The value for the key, or the supplied default.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic> getDefault(
      K&& k, const dynamic& v = dynamic::object) &&;
  /// Defaulted lookup: returns the value for the key, or the default.
  ///
  /// \param k The key to look up.
  /// \param v The default value returned when the key is absent.
  /// \returns The value for the key, or the supplied default.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic> getDefault(K&& k, dynamic&& v) &&;

  /// Defaulted lookup: returns the value for the key, or the default.
  ///
  /// \param k The key to look up.
  /// \param v The default value returned when the key is absent.
  /// \returns The value for the key, or the supplied default.
  dynamic getDefault(StringPiece k, const dynamic& v = dynamic::object) const&;
  /// Defaulted lookup: returns the value for the key, or the default.
  ///
  /// \param k The key to look up.
  /// \param v The default value returned when the key is absent.
  /// \returns The value for the key, or the supplied default.
  dynamic getDefault(StringPiece k, dynamic&& v) const&;
  /// Defaulted lookup: returns the value for the key, or the default.
  ///
  /// \param k The key to look up.
  /// \param v The default value returned when the key is absent.
  /// \returns The value for the key, or the supplied default.
  dynamic getDefault(StringPiece k, const dynamic& v = dynamic::object) &&;
  /// Defaulted lookup: returns the value for the key, or the default.
  ///
  /// \param k The key to look up.
  /// \param v The default value returned when the key is absent.
  /// \returns The value for the key, or the supplied default.
  dynamic getDefault(StringPiece k, dynamic&& v) &&;

  /**
   * Maybe set.
   *
   * setDefault will set the key to the supplied default if it is not yet set,
   * otherwise leaving it. setDefault returns a reference to the existing value
   * if present, the new value otherwise.
   *
   * Only defined for objects, throws TypeError otherwise.
   *
   * @methodset Object
   *
   * \param k The key to set if absent.
   * \param v The value to insert when the key is absent.
   * \returns A reference to the existing or newly inserted value.
   */
  template <typename K, typename V>
  IfIsNonStringDynamicConvertible<K, dynamic&> setDefault(K&& k, V&& v);
  /// Maybe set: assigns the default only if the key is absent.
  ///
  /// \param k The key to set if absent.
  /// \param v The value to insert when the key is absent.
  /// \returns A reference to the existing or newly inserted value.
  template <typename V>
  dynamic& setDefault(StringPiece k, V&& v);
  // MSVC 2015 Update 3 needs these extra overloads because if V were a
  // defaulted template parameter, it causes MSVC to consider v an rvalue
  // reference rather than a universal reference, resulting in it not being
  // able to find the correct overload to construct a dynamic with.
  /// Maybe set: assigns the default only if the key is absent.
  ///
  /// \param k The key to set if absent.
  /// \param v The value to insert when the key is absent.
  /// \returns A reference to the existing or newly inserted value.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic&> setDefault(K&& k, dynamic&& v);
  /// Maybe set: assigns the default only if the key is absent.
  ///
  /// \param k The key to set if absent.
  /// \param v The value to insert when the key is absent.
  /// \returns A reference to the existing or newly inserted value.
  template <typename K>
  IfIsNonStringDynamicConvertible<K, dynamic&> setDefault(
      K&& k, const dynamic& v = dynamic::object);

  /// Maybe set: assigns the default only if the key is absent.
  ///
  /// \param k The key to set if absent.
  /// \param v The value to insert when the key is absent.
  /// \returns A reference to the existing or newly inserted value.
  dynamic& setDefault(StringPiece k, dynamic&& v);
  /// Maybe set: assigns the default only if the key is absent.
  ///
  /// \param k The key to set if absent.
  /// \param v The value to insert when the key is absent.
  /// \returns A reference to the existing or newly inserted value.
  dynamic& setDefault(StringPiece k, const dynamic& v = dynamic::object);

  /**
   * Change size.
   *
   * Resizes an array so it has at n elements, using the supplied
   * default to fill new elements.  Throws TypeError if this dynamic
   * is not an array.
   *
   * May invalidate iterators.
   *
   * Post: size() == n
   *
   * @methodset Array
   *
   * \param sz The new number of elements.
   * \param defaultVal The value used to fill any newly added elements.
   */
  void resize(std::size_t sz, dynamic const& defaultVal = nullptr);

  /**
   * Pre-allocate size.
   *
   * If this is an array, an object, or a string, reserves the requested
   * capacity in the underlying container.  Otherwise throws TypeError.
   *
   * May invalidate iterators, and does not give any additional guarantees on
   * iterator invalidation on subsequent insertions; the only purpose is for
   * optimization.
   *
   * @methodset Container
   *
   * \param capacity The number of elements to reserve capacity for.
   */
  void reserve(std::size_t capacity);

  /**
   * @brief Overwriting insertion.
   *
   * Inserts the supplied key-value pair to an object, or throws if
   * it's not an object. If the key already exists, insert will overwrite the
   * value, i.e., similar to insert_or_assign.
   *
   * Invalidates iterators.
   *
   * @methodset Container
   *
   * \param key The key to insert or overwrite.
   * \param val The value to associate with the key.
   */
  template <class K, class V>
  IfNotIterator<K, void> insert(K&& key, V&& val);

  /**
   * Overwriting emplacement.
   *
   * Inserts an element into an object constructed in-place with the given args
   * if there is no existing element with the key, or throws if it's not an
   * object. Returns a pair consisting of an iterator to the inserted element,
   * or the already existing element if no insertion happened, and a bool
   * denoting whether the insertion took place.
   *
   * Invalidates iterators.
   *
   * @methodset Object
   *
   * \param args The arguments forwarded to construct the element in place.
   * \returns A pair of an iterator to the element and whether it was inserted.
   */
  template <class... Args>
  std::pair<item_iterator, bool> emplace(Args&&... args);

  /**
   * Non-overwriting emplacement.
   *
   * Inserts an element into an object with the given key and value constructed
   * in-place with the given args if there is no existing element with the key,
   * or throws if it's not an object. Returns a pair consisting of an iterator
   * to the inserted element, or the already existing element if no insertion
   * happened, and a bool denoting whether the insertion took place.
   *
   * Invalidates iterators.
   *
   * @methodset Object
   *
   * \param key The key to insert if absent.
   * \param args The arguments forwarded to construct the value in place.
   * \returns A pair of an iterator to the element and whether it was inserted.
   */
  template <class K, class... Args>
  std::pair<item_iterator, bool> try_emplace(K&& key, Args&&... args);

  /**
   * Inserts the supplied value into array, or throw if not array.
   * Shifts existing values in the array to the right.
   *
   * Invalidates iterators.
   *
   * \param pos The position to insert before.
   * \param value The value to insert.
   * \returns An iterator to the inserted element.
   */
  template <class T>
  iterator insert(const_iterator pos, T&& value);

  /**
   * Inserts elements from range [first, last) before pos into an array.
   * Throws if the type is not an array.
   *
   * Invalidates iterators.
   *
   * \param pos The position to insert before.
   * \param first The iterator to the first element to insert.
   * \param last The iterator past the last element to insert.
   * \returns An iterator to the first inserted element.
   */
  template <class InputIt>
  iterator insert(const_iterator pos, InputIt first, InputIt last);

  /**
   * Merge objects.
   *
   * Merge two folly dynamic objects.
   * The "update" and "update_missing" functions extend the object by
   *  inserting the key/value pairs of mergeObj into the current object.
   *  For update, if key is duplicated between the two objects, it
   *  will overwrite with the value of the object being inserted (mergeObj).
   *  For "update_missing", it will prefer the value in the original object
   *
   * The "merge" function creates a new object consisting of the key/value
   * pairs of both mergeObj1 and mergeObj2
   * If the key is duplicated between the two objects,
   *  it will prefer value in the second object (mergeObj2)
   *
   * @methodset Object
   *
   * \param mergeObj The object whose key/value pairs are inserted, overwriting
   *     on duplicate keys.
   */
  void update(const dynamic& mergeObj);
  /// Extends this object with mergeObj1, keeping this object's value on
  /// duplicate keys.
  /// \param mergeObj1 The object whose key/value pairs are inserted when the
  ///     key is missing.
  void update_missing(const dynamic& mergeObj1);
  /// Creates a new object with the key/value pairs of both objects, preferring
  /// the second object's value on duplicate keys.
  /// \param mergeObj1 The first object.
  /// \param mergeObj2 The second object, whose values win on duplicate keys.
  /// \returns The merged object.
  static dynamic merge(const dynamic& mergeObj1, const dynamic& mergeObj2);

  /**
   * Merge JSON Patch.
   *
   * Implement recursive version of RFC7386: JSON merge patch. This modifies
   * the current object.
   *
   * @methodset Object
   *
   * \param patch The merge patch to apply to this object.
   */
  void merge_patch(const dynamic& patch);

  /**
   * Compute patch.
   *
   * Computes JSON merge patch (RFC7386) needed to mutate from source to target
   *
   * @methodset Object
   *
   * \param source The source object.
   * \param target The target object.
   * \returns The merge patch that transforms source into target.
   */
  static dynamic merge_diff(const dynamic& source, const dynamic& target);

  /**
   * @brief Erases elements
   *
   * Erase an element from a dynamic object, by key.
   *
   * Invalidates iterators to the element being erased.
   *
   * Returns the number of elements erased (i.e. 1 or 0).
   *
   * @methodset Container
   *
   * \param key The key of the element to erase.
   * \returns The number of elements erased (0 or 1).
   */
  template <typename K>
  IfIsNonStringDynamicConvertible<K, std::size_t> erase(K&& key);

  /// @copydoc erase
  std::size_t erase(StringPiece key);

  /**
   * @brief Erases elements
   *
   * Erase an element from a dynamic object or array, using an
   * iterator or an iterator range.
   *
   * In arrays, invalidates iterators to elements after the element
   * being erased.  In objects, invalidates iterators to the elements
   * being erased.
   *
   * Returns a new iterator to the first element beyond any elements
   * removed, or end() if there are none.  (The iteration order does
   * not change.)
   *
   * @methodset Container
   *
   * \param it The iterator to the element to erase.
   * \returns An iterator to the first element beyond those removed.
   */
  iterator erase(const_iterator it);
  /// Erases the array or object elements in the iterator range [first, last).
  /// \param first The iterator to the first element to erase.
  /// \param last The iterator past the last element to erase.
  /// \returns An iterator to the first element beyond those removed.
  iterator erase(const_iterator first, const_iterator last);

  /// Erases the object item referenced by a key iterator.
  /// \param it The key iterator to the element to erase.
  /// \returns A key iterator to the first element beyond the one removed.
  const_key_iterator erase(const_key_iterator it);
  /// Erases the object items in the key-iterator range [first, last).
  /// \param first The key iterator to the first element to erase.
  /// \param last The key iterator past the last element to erase.
  /// \returns A key iterator to the first element beyond those removed.
  const_key_iterator erase(const_key_iterator first, const_key_iterator last);

  /// Erases the object item referenced by a value iterator.
  /// \param it The value iterator to the element to erase.
  /// \returns A value iterator to the first element beyond the one removed.
  value_iterator erase(const_value_iterator it);
  /// Erases the object items in the value-iterator range [first, last).
  /// \param first The value iterator to the first element to erase.
  /// \param last The value iterator past the last element to erase.
  /// \returns A value iterator to the first element beyond those removed.
  value_iterator erase(const_value_iterator first, const_value_iterator last);

  /// Erases the object item referenced by an item iterator.
  /// \param it The item iterator to the element to erase.
  /// \returns An item iterator to the first element beyond the one removed.
  item_iterator erase(const_item_iterator it);
  /// Erases the object items in the item-iterator range [first, last).
  /// \param first The item iterator to the first element to erase.
  /// \param last The item iterator past the last element to erase.
  /// \returns An item iterator to the first element beyond those removed.
  item_iterator erase(const_item_iterator first, const_item_iterator last);

  /**
   * Erase one object item and move its key and value into a callback.
   *
   * Use this when transforming an object by repeatedly removing entries:
   *
   *   for (auto it = obj.items().begin(); it != obj.items().end();) {
   *     it = obj.eraseInto(it, [&](dynamic&& key, dynamic&& value) {
   *       out.insert(std::move(key), std::move(value));
   *     });
   *   }
   *
   * Invalidates iterators to the element being erased.
   *
   * Returns a new iterator to the first element beyond the removed item, or
   * end() if there is none. The callback is invoked as
   * beforeDestroy(dynamic&& key, dynamic&& value).
   *
   * The entry is extracted before the callback runs. eraseInto makes the next
   * iterator, removes the item from the backing table, and only then exposes a
   * mutable key and value. This avoids mutating a resident table key through
   * items().
   *
   * @methodset Object
   *
   * \param it The item iterator to the element to erase.
   * \param beforeDestroy Callback invoked as beforeDestroy(dynamic&& key,
   *     dynamic&& value) with the extracted entry.
   * \returns An item iterator to the first element beyond the one removed.
   */
  template <typename BeforeDestroy>
  item_iterator eraseInto(
      const_item_iterator it, BeforeDestroy&& beforeDestroy);

  /**
   * Append elements to an array.
   *
   * If this is not an array, throws TypeError.
   *
   * Invalidates iterators.
   *
   * @methodset Array
   *
   * \param value The value to append to the array.
   */
  /// Appends a value to the end of this array.
  ///
  /// \param value The value to append.
  void push_back(dynamic const& value);
  /// Appends a value to the end of this array by moving it.
  ///
  /// \param value The value to append.
  void push_back(dynamic&& value);

  /**
   * Remove an element from the back of an array.
   *
   * If this is not an array, throws TypeError.
   *
   * Does not invalidate iterators.
   *
   * @methodset Array
   */
  void pop_back();

  /**
   * Return reference to the last element in an array.
   *
   * If this is not an array, throws TypeError.
   *
   * @methodset Array
   *
   * \returns A reference to the last element of the array.
   */
  const dynamic& back() const;

  /**
   * Hash self.
   *
   * Get a hash code.  This function is called by a std::hash<>
   * specialization.
   *
   * Note: an int64_t and double will both produce the same hash if they are
   * numerically equal before rounding. So the int64_t 2 will have the same hash
   * as the double 2.0. But no double will intentionally hash to the hash of a
   * value that only when rounded will compare as equal. E.g. No double will
   * intentionally hash to the hash of INT64_MAX (2^63 - 1) given that a double
   * cannot represent this value.
   *
   * \returns The hash code of this dynamic.
   */
  std::size_t hash() const;

 private:
  friend struct const_dynamic_view;
  friend struct dynamic_view;
  friend struct TypeError;
  struct ObjectImpl;
  template <class T>
  struct TypeInfo;
  template <class T>
  struct CompareOp;
  template <class T>
  struct GetAddrImpl;
  template <class T>
  struct PrintImpl;

  explicit dynamic(Array&& r);

  template <class T>
  T const& get() const;
  template <class T>
  T& get();

  template <class T>
  T* get_nothrow() & noexcept;
  template <class T>
  T const* get_nothrow() const& noexcept;
  template <class T>
  T* get_nothrow() && noexcept = delete;
  template <class T>
  T* getAddress() noexcept;
  template <class T>
  T const* getAddress() const noexcept;

  template <class T>
  T asImpl() const;

  static char const* typeName(Type);
  // NOTE: like ~dynamic, destroy() leaves type_ and u_ in an invalid state.
  void destroy() noexcept;
  void print(std::ostream&) const;
  void print_as_pseudo_json(std::ostream&) const; // see json.cpp

 private:
  Type type_;
  union Data {
    explicit Data() : nul(nullptr) {}
    ~Data() {}

    std::nullptr_t nul;
    Array array;
    bool boolean;
    double doubl;
    int64_t integer;
    std::string string;

    /*
     * Objects are placement new'd here.  We have to use a char buffer
     * because we don't know the type here (F14NodeMap<> with
     * dynamic would be parameterizing a std:: template with an
     * incomplete type right now).  (Note that in contrast we know it
     * is ok to do this with fbvector because we own it.)
     */
    aligned_storage_for_t<F14NodeMap<int, int>> objectBuffer;
  } u_;
};

/**
 * Like erase(vector), since C++20.
 *
 * \param dyn The array dynamic to erase from.
 * \param val The value to erase.
 * \returns The number of elements erased.
 */
template <typename Val>
size_t erase(folly::dynamic& dyn, Val const& val);

/**
 * Like erase_if(vector), erase_if(unordered_map), since C++20.
 *
 * If the predicate `pred` is formally invocable with `dynamic const&` then it
 * must type-check when so invoked. If the predicate is formally invocable with
 * `std::pair<dynamic const, dynamic> const&` then it must type-check when so
 * invoked. A lambda taking `const auto&` may be at risk of violating this rule,
 * so consider a lambda taking one of the two types explicitly, sans deduction.
 *
 * \param dyn The array or object dynamic to erase from.
 * \param pred The predicate selecting which elements to erase.
 * \returns The number of elements erased.
 */
template <typename Pred>
size_t erase_if(folly::dynamic& dyn, Pred pred);

//////////////////////////////////////////////////////////////////////

/**
 * This is a helper class for traversing an instance of dynamic and accessing
 * the values within without risking throwing an exception. The primary use case
 * is to help write cleaner code when using dynamic instances without strict
 * schemas - eg. where keys may be missing, or present but with null values,
 * when expecting non-null values.
 *
 * Some examples:
 *
 *   dynamic twelve = 12;
 *   dynamic str = "string";
 *   dynamic map = dynamic::object("str", str)("twelve", 12);
 *
 *   dynamic_view view{map};
 *   assert(view.descend("str").string_or("bad") == "string");
 *   assert(view.descend("twelve").int_or(-1) == 12);
 *   assert(view.descend("zzz").string_or("aaa") == "aaa");
 *
 *   dynamic wrapper = dynamic::object("child", map);
 *   dynamic_view wrapper_view{wrapper};
 *
 *   assert(wrapper_view.descend("child", "str").string_or("bad") == "string");
 *   assert(wrapper_view.descend("wrong", 0, "huh").value_or(nullptr).isNull());
 */
struct const_dynamic_view {
  /// Constructs an empty view not backed by any dynamic.
  const_dynamic_view() noexcept = default;

  /// Creates a view of the referenced dynamic.
  /// \param d The dynamic to view.
  /* implicit */ const_dynamic_view(dynamic const& d) noexcept;

  /// Copy constructor.
  /// \param other The view to copy.
  const_dynamic_view(const_dynamic_view const& other) noexcept = default;
  /// Copy-assignment operator.
  /// \param other The view to copy.
  /// \returns A reference to this view.
  const_dynamic_view& operator=(
      const_dynamic_view const& other) noexcept = default;

  /// Converts a mutable view to an immutable view.
  /// \param view The mutable view to convert.
  /* implicit */ const_dynamic_view(dynamic_view& view) noexcept;
  /// Assigns from a mutable view.
  /// \param view The mutable view to convert.
  /// \returns A reference to this view.
  /* implicit */ const_dynamic_view& operator=(dynamic_view& view) noexcept;

  /// Deleted constructor that prevents viewing a temporary dynamic.
  /// \param temporary The temporary dynamic (rejected).
  explicit const_dynamic_view(dynamic&& temporary) = delete;

  /// Tests whether this view is backed by a dynamic.
  /// \returns True if this view is backed by a valid dynamic.
  explicit operator bool() const noexcept;

  /// Tests whether this view is not backed by a dynamic.
  /// \returns True if this view is not backed by a dynamic.
  bool empty() const noexcept;

  /// Resets the view to a default-constructed state not backed by any dynamic.
  void reset() noexcept;

  /// Traverses a dynamic by repeatedly applying operator[].
  ///
  /// If all keys are valid, the returned view is backed by the accessed
  /// dynamic, otherwise it is empty.
  ///
  /// \param key The first key to descend into.
  /// \param keys The remaining keys to descend into.
  /// \returns A view of the accessed dynamic, or an empty view.
  template <typename Key, typename... Keys>
  const_dynamic_view descend(
      Key const& key, Keys const&... keys) const noexcept;

  /// Returns a copy of the viewed dynamic, or a default if the view is empty.
  /// \param val The default value returned when the view is empty.
  /// \returns A copy of the viewed dynamic, or the default value.
  dynamic value_or(dynamic&& val = nullptr) const;

  // The following accessors provide a read-only exception-safe API for
  // accessing the underlying viewed dynamic. Unlike the main dynamic APIs,
  // these follow a stricter contract, which also requires a caller-provided
  // default argument.
  //  - TypeError will not be thrown. primitive accessors further are marked
  //    noexcept.
  //  - No type conversions are performed. If the viewed dynamic does not match
  //    the requested type, the default argument is returned instead.
  //  - If the view is empty, the default argument is returned instead.
  /// Returns the viewed string, or the default if absent or not a string.
  /// \param val The default value returned when no string is viewed.
  /// \returns The viewed string, or the default value.
  std::string string_or(char const* val) const;
  /// Returns the viewed string, or the default if absent or not a string.
  ///
  /// \param val The default value returned when no string is viewed.
  /// \returns The viewed string, or the default value.
  std::string string_or(std::string val) const;
  /// Returns the viewed string, or the default if absent or not a string.
  ///
  /// \param val The default value returned when no string is viewed.
  /// \returns The viewed string, or the default value.
  template <
      typename Stringish,
      typename = std::enable_if_t<
          is_detected_v<dynamic_detail::detect_construct_string, Stringish>>>
  std::string string_or(Stringish&& val) const;

  /// Returns the viewed double, or the default if absent or not a double.
  /// \param val The default value returned when no double is viewed.
  /// \returns The viewed double, or the default value.
  double double_or(double val) const noexcept;

  /// Returns the viewed integer, or the default if absent or not an integer.
  /// \param val The default value returned when no integer is viewed.
  /// \returns The viewed integer, or the default value.
  int64_t int_or(int64_t val) const noexcept;

  /// Returns the viewed boolean, or the default if absent or not a boolean.
  /// \param val The default value returned when no boolean is viewed.
  /// \returns The viewed boolean, or the default value.
  bool bool_or(bool val) const noexcept;

 protected:
  /// Constructs a view from a dynamic pointer.
  /// \param d The dynamic to view, or nullptr for an empty view.
  /* implicit */ const_dynamic_view(dynamic const* d) noexcept;

  /// Descends through several keys, returning a pointer to the accessed
  /// dynamic or nullptr.
  /// \param key1 The first key to descend into.
  /// \param key2 The second key to descend into.
  /// \param keys The remaining keys to descend into.
  /// \returns A pointer to the accessed dynamic, or nullptr.
  template <typename Key1, typename Key2, typename... Keys>
  dynamic const* descend_(
      Key1 const& key1, Key2 const& key2, Keys const&... keys) const noexcept;
  /// Descends through a single key, returning a pointer to the accessed
  /// dynamic or nullptr.
  /// \param key The key to descend into.
  /// \returns A pointer to the accessed dynamic, or nullptr.
  template <typename Key>
  dynamic const* descend_(Key const& key) const noexcept;
  /// Descends through a single non-string key without validating the view.
  /// \param key The key to descend into.
  /// \returns A pointer to the accessed dynamic, or nullptr.
  template <typename Key>
  dynamic::IfIsNonStringDynamicConvertible<Key, dynamic const*>
  descend_unchecked_(Key const& key) const noexcept;
  /// Descends through a single string key without validating the view.
  /// \param key The key to descend into.
  /// \returns A pointer to the accessed dynamic, or nullptr.
  dynamic const* descend_unchecked_(StringPiece key) const noexcept;

  /// Pointer to the viewed dynamic, or nullptr when the view is empty.
  dynamic const* d_ = nullptr;

  /// Accesses a value by type, returning a copy.
  /// \param args The arguments forwarded to the accessor.
  /// \returns A copy of the accessed value.
  template <typename T, typename... Args>
  T get_copy(Args&&... args) const;
};

/// A mutable view of a dynamic that can extract values without copying.
struct dynamic_view : public const_dynamic_view {
  /// Constructs an empty view not backed by any dynamic.
  dynamic_view() noexcept = default;

  /// Creates a view of a non-const dynamic.
  /// \param d The dynamic to view.
  /* implicit */ dynamic_view(dynamic& d) noexcept;

  /// Copy constructor.
  /// \param other The view to copy.
  dynamic_view(dynamic_view const& other) noexcept = default;
  /// Copy-assignment operator.
  /// \param other The view to copy.
  /// \returns A reference to this view.
  dynamic_view& operator=(dynamic_view const& other) noexcept = default;

  /// Deleted constructor: a dynamic_view cannot view a const dynamic.
  /// \param d The const dynamic (rejected).
  explicit dynamic_view(dynamic const& d) = delete;
  /// Deleted constructor: a dynamic_view cannot be built from a
  /// const_dynamic_view.
  /// \param view The const view (rejected).
  explicit dynamic_view(const_dynamic_view const& view) = delete;

  /// Like const_dynamic_view::descend, but returns a dynamic_view.
  /// \param key The first key to descend into.
  /// \param keys The remaining keys to descend into.
  /// \returns A mutable view of the accessed dynamic, or an empty view.
  template <typename Key, typename... Keys>
  dynamic_view descend(Key const& key, Keys const&... keys) const noexcept;

  // dynamic_view provides APIs which can mutably access the backed dynamic.
  // 'mutably access' in this case means extracting the viewed dynamic or
  // value to omit unnecessary copies. It does not mean writing through to
  // the backed dynamic - this is still just a view, not a mutator.

  /// Moves the viewed dynamic out via std::move, or returns a default.
  ///
  /// Postconditions for the backed dynamic are the same as for any dynamic
  /// that is moved-from.
  ///
  /// \param val The default value returned when the view is empty.
  /// \returns The moved-out dynamic, or the default value.
  dynamic move_value_or(dynamic&& val = nullptr) noexcept;

  /// Moves the viewed string out, or returns the default value.
  ///
  /// Specific optimization for strings which can allocate, unlike the other
  /// scalar types. If the viewed dynamic is a string, the string value is
  /// std::move'd to initialize a new instance which is returned.
  ///
  /// \param val The default value returned when no string is viewed.
  /// \returns The moved-out string, or the default value.
  std::string move_string_or(std::string val) noexcept;
  /// Moves the viewed string out, or returns the default value.
  ///
  /// \param val The default value returned when no string is viewed.
  /// \returns The moved-out string, or the default value.
  std::string move_string_or(char const* val);
  /// Moves the viewed string out, or returns the default value.
  ///
  /// \param val The default value returned when no string is viewed.
  /// \returns The moved-out string, or the default value.
  template <
      typename Stringish,
      typename = std::enable_if_t<
          is_detected_v<dynamic_detail::detect_construct_string, Stringish>>>
  std::string move_string_or(Stringish&& val);

 private:
  template <typename T, typename... Args>
  T get_move(Args&&... args);
};

/// Returns a contextually-correct view for the given dynamic.
///
/// If passed a `dynamic const&`, returns a const_dynamic_view, and if passed
/// a `dynamic&`, returns a dynamic_view.
///
/// \param d The dynamic to view.
/// \returns A const_dynamic_view of the given dynamic.
inline auto make_dynamic_view(dynamic const& d) {
  return const_dynamic_view{d};
}

/// Returns a contextually-correct view for the given dynamic.
///
/// \param d The dynamic to view.
/// \returns A dynamic_view of the given dynamic.
inline auto make_dynamic_view(dynamic& d) {
  return dynamic_view{d};
}

/// Deleted to prevent viewing a temporary dynamic.
///
/// \param other The rvalue dynamic (rejected).
/// \returns Never returns; this overload is deleted.
auto make_dynamic_view(dynamic&& other) = delete;

//////////////////////////////////////////////////////////////////////

} // namespace folly

/// Standard library namespace, reopened to specialize std::hash.
namespace std {

/// Standard library hash specialization for folly::dynamic.
template <>
struct hash<::folly::dynamic> {
  /// Marks this hash as having avalanching output quality.
  using folly_is_avalanching = std::true_type;

  /// Computes the hash of a dynamic.
  ///
  /// \param d The dynamic to hash.
  /// \returns The hash value of the dynamic.
  size_t operator()(::folly::dynamic const& d) const { return d.hash(); }
};

} // namespace std

#include <folly/json/dynamic-inl.h>
