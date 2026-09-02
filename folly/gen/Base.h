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
#define FOLLY_GEN_BASE_H_

#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/Conv.h>
#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/Utility.h>
#include <folly/container/Access.h>
#include <folly/gen/Core.h>

/**
 * Generator-based Sequence Comprehensions in C++, akin to C#'s LINQ
 *
 * This library makes it possible to write declarative comprehensions for
 * processing sequences of values efficiently in C++. The operators should be
 * familiar to those with experience in functional programming, and the
 * performance will be virtually identical to the equivalent, boilerplate C++
 * implementations.
 *
 * Generator objects may be created from either an stl-like container (anything
 * supporting begin() and end()), from sequences of values, or from another
 * generator (see below). To create a generator that pulls values from a vector,
 * for example, one could write:
 *
 *   vector<string> names { "Jack", "Jill", "Sara", "Tom" };
 *   auto gen = from(names);
 *
 * Generators are composed by building new generators out of old ones through
 * the use of operators. These are reminiscent of shell pipelines, and afford
 * similar composition. Lambda functions are used liberally to describe how to
 * handle individual values:
 *
 *   auto lengths = gen
 *                | mapped([](const fbstring& name) { return name.size(); });
 *
 * Generators are lazy; they don't actually perform any work until they need to.
 * As an example, the 'lengths' generator (above) won't actually invoke the
 * provided lambda until values are needed:
 *
 *   auto lengthVector = lengths | as<std::vector>();
 *   auto totalLength = lengths | sum;
 *
 * 'auto' is useful in here because the actual types of the generators objects
 * are usually complicated and implementation-sensitive.
 *
 * If a simpler type is desired (for returning, as an example), VirtualGen<T>
 * may be used to wrap the generator in a polymorphic wrapper:
 *
 *  VirtualGen<float> powersOfE() {
 *    return seq(1) | mapped(&expf);
 *  }
 *
 * To learn more about this library, including the use of infinite generators,
 * see the examples in the comments, or the docs (coming soon).
 */

namespace folly {
namespace gen {

/// Function object that compares two values with `operator<`.
class Less {
 public:
  /// Compares two values with `operator<`.
  ///
  /// \param first The left-hand operand.
  /// \param second The right-hand operand.
  /// \returns True if `first` is less than `second`.
  template <class First, class Second>
  auto operator()(const First& first, const Second& second) const
      -> decltype(first < second) {
    return first < second;
  }
};

/// Function object that compares two values with `operator>`.
class Greater {
 public:
  /// Compares two values with `operator>`.
  ///
  /// \param first The left-hand operand.
  /// \param second The right-hand operand.
  /// \returns True if `first` is greater than `second`.
  template <class First, class Second>
  auto operator()(const First& first, const Second& second) const
      -> decltype(first > second) {
    return first > second;
  }
};

/// Function object that extracts the nth element of a tuple-like value.
template <int n>
class Get {
 public:
  /// Extracts the nth element of a tuple-like value.
  ///
  /// \param value The tuple-like value to read from.
  /// \returns The nth element of `value`.
  template <class Value>
  auto operator()(Value&& value) const
      -> decltype(std::get<n>(std::forward<Value>(value))) {
    return std::get<n>(std::forward<Value>(value));
  }
};

/// Function object that calls a non-const member function of a class.
template <class Class, class Result>
class MemberFunction {
 public:
  /// Pointer-to-member type for the called member function.
  using MemberPtr = Result (Class::*)();

 private:
  MemberPtr member_;

 public:
  /// Constructs the accessor from a member function pointer.
  ///
  /// \param member The member function to call.
  explicit MemberFunction(MemberPtr member) : member_(member) {}

  /// Calls the member function on an rvalue object.
  ///
  /// \param x The object to call the member function on.
  /// \returns The result of the member function call.
  Result operator()(Class&& x) const { return (x.*member_)(); }

  /// Calls the member function on a mutable object.
  ///
  /// \param x The object to call the member function on.
  /// \returns The result of the member function call.
  Result operator()(Class& x) const { return (x.*member_)(); }

  /// Calls the member function on a mutable object given a pointer.
  ///
  /// \param x The object to call the member function on.
  /// \returns The result of the member function call.
  Result operator()(Class* x) const { return (x->*member_)(); }
};

/// Function object that calls a const member function of a class.
template <class Class, class Result>
class ConstMemberFunction {
 public:
  /// Pointer-to-member type for the called const member function.
  using MemberPtr = Result (Class::*)() const;

 private:
  MemberPtr member_;

 public:
  /// Constructs the accessor from a const member function pointer.
  ///
  /// \param member The const member function to call.
  explicit ConstMemberFunction(MemberPtr member) : member_(member) {}

  /// Calls the member function on a const object.
  ///
  /// \param x The object to call the member function on.
  /// \returns The result of the member function call.
  Result operator()(const Class& x) const { return (x.*member_)(); }

  /// Calls the member function on a const object given a pointer.
  ///
  /// \param x The object to call the member function on.
  /// \returns The result of the member function call.
  Result operator()(const Class* x) const { return (x->*member_)(); }
};

/// Function object that accesses a data member of a class.
template <class Class, class FieldType>
class Field {
 public:
  /// Pointer-to-member type for the accessed field.
  using FieldPtr = FieldType Class::*;

 private:
  FieldPtr field_;

 public:
  /// Constructs a field accessor from a member pointer.
  ///
  /// \param field The pointer to the member to access.
  explicit Field(FieldPtr field) : field_(field) {}

  /// Returns the field of a const object.
  ///
  /// \param x The object to read the field from.
  /// \returns A const reference to the field.
  const FieldType& operator()(const Class& x) const { return x.*field_; }

  /// Returns the field of a const object given a pointer.
  ///
  /// \param x The object to read the field from.
  /// \returns A const reference to the field.
  const FieldType& operator()(const Class* x) const { return x->*field_; }

  /// Returns the field of a mutable object.
  ///
  /// \param x The object to read the field from.
  /// \returns A mutable reference to the field.
  FieldType& operator()(Class& x) const { return x.*field_; }

  /// Returns the field of a mutable object given a pointer.
  ///
  /// \param x The object to read the field from.
  /// \returns A mutable reference to the field.
  FieldType& operator()(Class* x) const { return x->*field_; }

  /// Returns the field of an rvalue object.
  ///
  /// \param x The object to read the field from.
  /// \returns An rvalue reference to the field.
  FieldType&& operator()(Class&& x) const { return std::move(x.*field_); }
};

/// Function object that casts its argument to an rvalue reference.
class Move {
 public:
  /// Moves the given value.
  ///
  /// \param value The value to move.
  /// \returns An rvalue reference to `value`.
  template <class Value>
  auto operator()(Value&& value) const
      -> decltype(std::move(std::forward<Value>(value))) {
    return std::move(std::forward<Value>(value));
  }
};

/**
 * Class and helper function for negating a boolean Predicate
 */
template <class Predicate>
class Negate {
  Predicate pred_;

 public:
  /// Constructs a negation of a default-constructed predicate.
  Negate() = default;

  /// Constructs a negation of the given predicate.
  ///
  /// \param pred The predicate to negate.
  explicit Negate(Predicate pred) : pred_(std::move(pred)) {}

  /// Evaluates the negated predicate.
  ///
  /// \param arg The argument passed to the wrapped predicate.
  /// \returns The logical negation of the wrapped predicate's result.
  template <class Arg>
  bool operator()(Arg&& arg) const {
    return !pred_(std::forward<Arg>(arg));
  }
};
/// Creates a predicate that negates the result of `pred`.
///
/// \param pred The predicate to negate.
/// \returns A predicate returning the logical negation of `pred`.
template <class Predicate>
Negate<Predicate> negate(Predicate pred) {
  return Negate<Predicate>(std::move(pred));
}

/// Function object that constructs a `Dest` from a value.
template <class Dest>
class Cast {
 public:
  /// Constructs a `Dest` from a value.
  ///
  /// \param value The value to convert.
  /// \returns The constructed `Dest`.
  template <class Value>
  Dest operator()(Value&& value) const {
    return Dest(std::forward<Value>(value));
  }
};

/// Function object that converts values to `Dest` via folly::to.
template <class Dest>
class To {
 public:
  /// Converts a value to `Dest`.
  ///
  /// \param value The value to convert.
  /// \returns The converted value.
  template <class Value>
  Dest operator()(Value&& value) const {
    return ::folly::to<Dest>(std::forward<Value>(value));
  }
};

/// Function object that converts values to `Dest`, returning an Expected.
template <class Dest>
class TryTo {
 public:
  /// Attempts to convert a value to `Dest`.
  ///
  /// \param value The value to convert.
  /// \returns The converted value, or a conversion error.
  template <class Value>
  Expected<Dest, ConversionCode> operator()(Value&& value) const {
    return ::folly::tryTo<Dest>(std::forward<Value>(value));
  }
};

// Specialization to allow String->StringPiece conversion
template <>
class To<StringPiece> {
 public:
  StringPiece operator()(StringPiece src) const { return src; }
};

/// Groups the values of a sequence by a computed key.
template <class Key, class Value>
class Group;

namespace detail {

template <class Self>
struct FBounded;

/*
 * Type Traits
 */
template <class Container>
struct ValueTypeOfRange {
 public:
  using RefType = decltype(*access::begin(std::declval<Container&>()));
  using StorageType = typename std::decay<RefType>::type;
};

/*
 * Sources
 */
template <
    class Container,
    class Value = typename ValueTypeOfRange<Container>::RefType>
class ReferencedSource;

template <
    class Value,
    class Container = std::vector<typename std::decay<Value>::type>>
class CopiedSource;

template <class Value, class SequenceImpl>
class Sequence;

template <class Value>
class RangeImpl;

template <class Value, class Distance>
class RangeWithStepImpl;

template <class Value>
class SeqImpl;

template <class Value, class Distance>
class SeqWithStepImpl;

template <class Value>
class InfiniteImpl;

template <class Value, class Source>
class Yield;

template <class Value>
class Empty;

template <class Value>
class SingleReference;

template <class Value>
class SingleCopy;

/*
 * Operators
 */
template <class Predicate>
class Map;

template <class Predicate>
class Filter;

template <class Predicate>
class Until;

class Take;

class Stride;

template <class Rand>
class Sample;

class Skip;

template <class Visitor>
class Visit;

template <class Selector, class Comparer = Less>
class Order;

template <class Selector>
class GroupBy;

template <class Selector>
class GroupByAdjacent;

template <class Selector>
class Distinct;

template <class Operators>
class Composer;

template <class Expected>
class TypeAssertion;

class Concat;

class RangeConcat;

template <bool forever>
class Cycle;

class Batch;

class Window;

class Dereference;

class Indirect;

/*
 * Sinks
 */
template <class Seed, class Fold>
class FoldLeft;

class First;

template <bool result>
class IsEmpty;

template <class Reducer>
class Reduce;

class Sum;

template <class Selector, class Comparer>
class Min;

template <class Container>
class Collect;

template <
    template <class, class> class Collection = std::vector,
    template <class> class Allocator = std::allocator>
class CollectTemplate;

template <class Collection>
class Append;

template <class Value>
struct GeneratorBuilder;

template <class Needle>
class Contains;

template <class Exception, class ErrorHandler>
class GuardImpl;

template <class T>
class UnwrapOr;

class Unwrap;

} // namespace detail

/**
 * Polymorphic wrapper
 **/
template <class Value>
class VirtualGen;

/// Move-only polymorphic wrapper for a generator yielding `Value`.
template <class Value>
class VirtualGenMoveOnly;

/*
 * Source Factories
 */
/// Produces a sequence referencing the elements of a const container.
///
/// \param source The container whose elements are referenced.
/// \returns A source generator over const references to the elements.
template <
    class Container,
    class From = detail::ReferencedSource<const Container>>
From fromConst(const Container& source) {
  return From(&source);
}

/// Produces a sequence referencing the elements of a mutable container.
///
/// \param source The container whose elements are referenced.
/// \returns A source generator over references to the elements.
template <class Container, class From = detail::ReferencedSource<Container>>
From from(Container& source) {
  return From(&source);
}

/// Produces a sequence holding copies of the elements of a container.
///
/// \param source The container whose elements are copied into the sequence.
/// \returns A source generator over copies of the elements.
template <
    class Container,
    class Value = typename detail::ValueTypeOfRange<Container>::StorageType,
    class CopyOf = detail::CopiedSource<Value>>
CopyOf fromCopy(Container&& source) {
  return CopyOf(std::forward<Container>(source));
}

/// Produces a sequence from the elements of an initializer list.
///
/// \param source The initializer list whose elements are copied.
/// \returns A source generator over copies of the elements.
template <class Value, class From = detail::CopiedSource<Value>>
From from(std::initializer_list<Value> source) {
  return From(source);
}

/// Produces a sequence by moving the elements out of a temporary container.
///
/// \param source The container whose elements are moved into the sequence.
/// \returns A source generator over the moved elements.
template <
    class Container,
    class From =
        detail::CopiedSource<typename Container::value_type, Container>>
From from(Container&& source) {
  return From(std::move(source));
}

/// Produces a half-open sequence from `begin` to `end`.
///
/// \param begin The first value of the sequence.
/// \param end The exclusive upper bound of the sequence.
/// \returns A source generator yielding the range.
template <
    class Value,
    class Impl = detail::RangeImpl<Value>,
    class Gen = detail::Sequence<Value, Impl>>
Gen range(Value begin, Value end) {
  return Gen{std::move(begin), Impl{std::move(end)}};
}

/// Produces a half-open sequence from `begin` to `end` by `step`.
///
/// \param begin The first value of the sequence.
/// \param end The exclusive upper bound of the sequence.
/// \param step The increment between consecutive values.
/// \returns A source generator yielding the stepped range.
template <
    class Value,
    class Distance,
    class Impl = detail::RangeWithStepImpl<Value, Distance>,
    class Gen = detail::Sequence<Value, Impl>>
Gen range(Value begin, Value end, Distance step) {
  return Gen{std::move(begin), Impl{std::move(end), std::move(step)}};
}

/// Produces an inclusive sequence from `first` to `last`.
///
/// \param first The first value of the sequence.
/// \param last The last value of the sequence, inclusive.
/// \returns A source generator yielding the sequence.
template <
    class Value,
    class Impl = detail::SeqImpl<Value>,
    class Gen = detail::Sequence<Value, Impl>>
Gen seq(Value first, Value last) {
  return Gen{std::move(first), Impl{std::move(last)}};
}

/// Produces an inclusive sequence from `first` to `last` by `step`.
///
/// \param first The first value of the sequence.
/// \param last The last value of the sequence, inclusive.
/// \param step The increment between consecutive values.
/// \returns A source generator yielding the stepped sequence.
template <
    class Value,
    class Distance,
    class Impl = detail::SeqWithStepImpl<Value, Distance>,
    class Gen = detail::Sequence<Value, Impl>>
Gen seq(Value first, Value last, Distance step) {
  return Gen{std::move(first), Impl{std::move(last), std::move(step)}};
}

/// Produces an infinite increasing sequence starting at `first`.
///
/// \param first The first value of the sequence.
/// \returns A source generator yielding an unbounded sequence.
template <
    class Value,
    class Impl = detail::InfiniteImpl<Value>,
    class Gen = detail::Sequence<Value, Impl>>
Gen seq(Value first) {
  return Gen{std::move(first), Impl{}};
}

/// Wraps a source callable as a generator yielding `Value`.
///
/// \param source The callable that emits values through a yield function.
/// \returns A source generator driven by `source`.
template <class Value, class Source, class Yield = detail::Yield<Value, Source>>
Yield generator(Source&& source) {
  return Yield(std::forward<Source>(source));
}

/*
 * Create inline generator, used like:
 *
 *  auto gen = GENERATOR(int) { yield(1); yield(2); };
 *
 * GENERATOR_REF can be useful for creating a generator that doesn't
 * leave its original scope.
 */
#define GENERATOR(TYPE) \
  ::folly::gen::detail::GeneratorBuilder<TYPE>() + [=](auto&& yield)
#define GENERATOR_WITH_THIS(TYPE) \
  ::folly::gen::detail::GeneratorBuilder<TYPE>() + [ =, this ](auto&& yield)
#define GENERATOR_REF(TYPE) \
  ::folly::gen::detail::GeneratorBuilder<TYPE>() + [&](auto&& yield)

/*
 * empty() - for producing empty sequences.
 */
/// Produces an empty sequence of the given value type.
///
/// \returns A source generator that yields no values.
template <class Value>
detail::Empty<Value> empty() {
  return {};
}

/// Produces a sequence containing a single value.
///
/// \param value The single value to yield.
/// \returns A source generator that yields `value` once.
template <
    class Value,
    class Just = typename std::conditional<
        std::is_reference<Value>::value,
        detail::SingleReference<typename std::remove_reference<Value>::type>,
        detail::SingleCopy<Value>>::type>
Just just(Value&& value) {
  return Just(std::forward<Value>(value));
}

/*
 * Operator Factories
 */
/// Applies a function to each value, yielding the results.
///
/// \param pred The function applied to each value.
/// \returns An operator yielding `pred` applied to each value.
template <class Predicate, class Map = detail::Map<Predicate>>
Map mapped(Predicate pred = Predicate()) {
  return Map(std::move(pred));
}

/// Applies a function to each value, yielding the results.
///
/// \param pred The function applied to each value.
/// \returns An operator yielding `pred` applied to each value.
template <class Predicate, class Map = detail::Map<Predicate>>
Map map(Predicate pred = Predicate()) {
  return Map(std::move(pred));
}

/**
 * mapOp - Given a generator of generators, maps the application of the given
 * operator on to each inner gen. Especially useful in aggregating nested data
 * structures:
 *
 *   chunked(samples, 256)
 *     | mapOp(filter(sampleTest) | count)
 *     | sum;
 *
 * \param op The operator applied to each inner generator.
 * \returns An operator that applies `op` to every inner generator.
 */
template <class Operator, class Map = detail::Map<detail::Composer<Operator>>>
Map mapOp(Operator op) {
  return Map(detail::Composer<Operator>(std::move(op)));
}

/*
 * member(...) - For extracting a member from each value.
 *
 *  vector<string> strings = ...;
 *  auto sizes = from(strings) | member(&string::size);
 *
 * If a member is const overridden (like 'front()'), pass template parameter
 * 'Const' to select the const version, or 'Mutable' to select the non-const
 * version:
 *
 *  auto heads = from(strings) | member<Const>(&string::front);
 */
/// Selects the const or non-const overload of a member accessor.
enum MemberType {
  Const, ///< Select the const overload.
  Mutable, ///< Select the non-const overload.
};

/**
 * These exist because MSVC has problems with expression SFINAE in templates
 * assignment and comparisons don't work properly without being pulled out
 * of the template declaration
 */
template <MemberType Constness>
struct ExprIsConst {
  /// Holds the constness test result as a compile-time constant.
  enum {
    value = Constness == Const, ///< True when `Constness` is `Const`.
  };
};

/// SFINAE helper exposing whether `Constness` selects the mutable overload.
template <MemberType Constness>
struct ExprIsMutable {
  /// Holds the constness test result as a compile-time constant.
  enum {
    value = Constness == Mutable, ///< True when `Constness` is `Mutable`.
  };
};

/// Calls a const member function on each value.
///
/// \param member Pointer to the const member function to call.
/// \returns An operator yielding the result of the call on each value.
template <
    MemberType Constness = Const,
    class Class,
    class Return,
    class Mem = ConstMemberFunction<Class, Return>,
    class Map = detail::Map<Mem>>
typename std::enable_if<ExprIsConst<Constness>::value, Map>::type member(
    Return (Class::*member)() const) {
  return Map(Mem(member));
}

/// Calls a non-const member function on each value.
///
/// \param member Pointer to the member function to call.
/// \returns An operator yielding the result of the call on each value.
template <
    MemberType Constness = Mutable,
    class Class,
    class Return,
    class Mem = MemberFunction<Class, Return>,
    class Map = detail::Map<Mem>>
typename std::enable_if<ExprIsMutable<Constness>::value, Map>::type member(
    Return (Class::*member)()) {
  return Map(Mem(member));
}

/*
 * field(...) - For extracting a field from each value.
 *
 *  vector<Item> items = ...;
 *  auto names = from(items) | field(&Item::name);
 *
 * Note that if the values of the generator are rvalues, any non-reference
 * fields will be rvalues as well. As an example, the code below does not copy
 * any strings, only moves them:
 *
 *  auto namesVector = from(items)
 *                   | move
 *                   | field(&Item::name)
 *                   | as<vector>();
 */
/// Extracts a data member from each value.
///
/// \param field Pointer to the member to extract from each value.
/// \returns An operator yielding the selected member of each value.
template <
    class Class,
    class FieldType,
    class Field = Field<Class, FieldType>,
    class Map = detail::Map<Field>>
Map field(FieldType Class::* field) {
  return Map(Field(field));
}

/// Keeps only values that satisfy the predicate.
///
/// \param pred The predicate tested against each value.
/// \returns An operator yielding values that satisfy `pred`.
template <class Predicate = Identity, class Filter = detail::Filter<Predicate>>
Filter filter(Predicate pred = Predicate()) {
  return Filter(std::move(pred));
}

/// Applies a visitor to each value, passing values through unchanged.
///
/// \param visitor The callable invoked for each value.
/// \returns An operator that visits each value and forwards it.
template <class Visitor = Ignore, class Visit = detail::Visit<Visitor>>
Visit visit(Visitor visitor = Visitor()) {
  return Visit(std::move(visitor));
}

/// Yields values until the predicate holds, then stops.
///
/// \param pred The predicate tested against each value.
/// \returns An operator yielding values up to the first satisfying `pred`.
template <class Predicate = Identity, class Until = detail::Until<Predicate>>
Until until(Predicate pred = Predicate()) {
  return Until(std::move(pred));
}

/// Yields values while the predicate holds, then stops.
///
/// \param pred The predicate tested against each value.
/// \returns An operator yielding a prefix of values satisfying `pred`.
template <
    class Predicate = Identity,
    class TakeWhile = detail::Until<Negate<Predicate>>>
TakeWhile takeWhile(Predicate pred = Predicate()) {
  return TakeWhile(Negate<Predicate>(std::move(pred)));
}

/// Orders values by their selected key using the given comparer.
///
/// \param selector The projection producing the sort key.
/// \param comparer The comparison used to order keys.
/// \returns An operator yielding values sorted by key.
template <
    class Selector = Identity,
    class Comparer = Less,
    class Order = detail::Order<Selector, Comparer>>
Order orderBy(Selector selector = Selector(), Comparer comparer = Comparer()) {
  return Order(std::move(selector), std::move(comparer));
}

/// Orders values in descending order of their selected key.
///
/// \param selector The projection producing the sort key.
/// \returns An operator yielding values sorted by descending key.
template <
    class Selector = Identity,
    class Order = detail::Order<Selector, Greater>>
Order orderByDescending(Selector selector = Selector()) {
  return Order(std::move(selector));
}

/// Groups all values by their selected key.
///
/// \param selector The projection producing the grouping key.
/// \returns An operator yielding groups of values sharing a key.
template <class Selector = Identity, class GroupBy = detail::GroupBy<Selector>>
GroupBy groupBy(Selector selector = Selector()) {
  return GroupBy(std::move(selector));
}

/// Groups consecutive values that share the same selected key.
///
/// \param selector The projection producing the grouping key.
/// \returns An operator yielding groups of adjacent values.
template <
    class Selector = Identity,
    class GroupByAdjacent = detail::GroupByAdjacent<Selector>>
GroupByAdjacent groupByAdjacent(Selector selector = Selector()) {
  return GroupByAdjacent(std::move(selector));
}

/// Keeps only the first value for each distinct selected key.
///
/// \param selector The projection producing the key used for distinctness.
/// \returns An operator yielding one value per distinct key.
template <
    class Selector = Identity,
    class Distinct = detail::Distinct<Selector>>
Distinct distinctBy(Selector selector = Selector()) {
  return Distinct(std::move(selector));
}

/// Extracts the nth element of each tuple-like value.
///
/// \returns An operator yielding the nth element of each value.
template <int n, class Get = detail::Map<Get<n>>>
Get get() {
  return Get();
}

/// Constructs a `Dest` from each value.
///
/// \returns An operator that constructs a `Dest` from each value.
template <class Dest, class Cast = detail::Map<Cast<Dest>>>
Cast eachAs() {
  return Cast();
}

/// Calls folly::to on each value, converting it to `Dest`.
///
/// \returns An operator that converts each value to `Dest`.
template <class Dest, class EachTo = detail::Map<To<Dest>>>
EachTo eachTo() {
  return EachTo();
}

/// Calls folly::tryTo on each value, mapping to an Expected of `Dest`.
///
/// \returns An operator that attempts conversion of each value to `Dest`.
template <class Dest, class EachTryTo = detail::Map<TryTo<Dest>>>
EachTryTo eachTryTo() {
  return EachTryTo();
}

/// Asserts at compile time that the sequence yields the given value type.
///
/// \returns An operator that passes values through unchanged.
template <class Value>
detail::TypeAssertion<Value> assert_type() {
  return {};
}

/*
 * Sink Factories
 */

/**
 * any() - For determining if any value in a sequence satisfies a predicate.
 *
 * The following is an example for checking if any computer is broken:
 *
 *   bool schrepIsMad = from(computers) | any(isBroken);
 *
 * (because everyone knows Schrep hates broken computers).
 *
 * Note that if no predicate is provided, 'any()' checks if any of the values
 * are true when cased to bool. To check if any of the scores are nonZero:
 *
 *   bool somebodyScored = from(scores) | any();
 *
 * Note: Passing an empty sequence through 'any()' will always return false. In
 * fact, 'any()' is equivilent to the composition of 'filter()' and 'notEmpty'.
 *
 *   from(source) | any(pred) == from(source) | filter(pred) | notEmpty
 *
 * \param pred The predicate tested against each value.
 * \returns A sink returning true if any value satisfies `pred`.
 */

template <
    class Predicate = Identity,
    class Filter = detail::Filter<Predicate>,
    class NotEmpty = detail::IsEmpty<false>,
    class Composed = detail::Composed<Filter, NotEmpty>>
Composed any(Predicate pred = Predicate()) {
  return Composed(Filter(std::move(pred)), NotEmpty());
}

/**
 * all() - For determining whether all values in a sequence satisfy a predicate.
 *
 * The following is an example for checking if all members of a team are cool:
 *
 *   bool isAwesomeTeam = from(team) | all(isCool);
 *
 * Note that if no predicate is provided, 'all()'' checks if all of the values
 * are true when cased to bool.
 * The following makes sure none of 'pointers' are nullptr:
 *
 *   bool allNonNull = from(pointers) | all();
 *
 * Note: Passing an empty sequence through 'all()' will always return true. In
 * fact, 'all()' is equivilent to the composition of 'filter()' with the
 * reversed predicate and 'isEmpty'.
 *
 *   from(source) | all(pred) == from(source) | filter(negate(pred)) | isEmpty
 *
 * \param pred The predicate tested against each value.
 * \returns A sink returning true if every value satisfies `pred`.
 */
template <
    class Predicate = Identity,
    class Filter = detail::Filter<Negate<Predicate>>,
    class IsEmpty = detail::IsEmpty<true>,
    class Composed = detail::Composed<Filter, IsEmpty>>
Composed all(Predicate pred = Predicate()) {
  return Composed(Filter(negate(pred)), IsEmpty());
}

/// Left-folds a sequence into a single value from an initial seed.
///
/// \param seed The initial accumulator value.
/// \param fold The binary operation combining the accumulator and each value.
/// \returns A sink yielding the folded value.
template <class Seed, class Fold, class FoldLeft = detail::FoldLeft<Seed, Fold>>
FoldLeft foldl(Seed seed = Seed(), Fold fold = Fold()) {
  return FoldLeft(std::move(seed), std::move(fold));
}

/// Reduces a sequence to a single value using a binary reducer.
///
/// \param reducer The binary operation combining accumulated values.
/// \returns A sink yielding the reduced value.
template <class Reducer, class Reduce = detail::Reduce<Reducer>>
Reduce reduce(Reducer reducer = Reducer()) {
  return Reduce(std::move(reducer));
}

/// Finds the value with the minimum selected key.
///
/// \param selector The projection used to compare values.
/// \returns A sink yielding the value with the smallest selected key.
template <class Selector = Identity, class Min = detail::Min<Selector, Less>>
Min minBy(Selector selector = Selector()) {
  return Min(std::move(selector));
}

/// Finds the value with the maximum selected key.
///
/// \param selector The projection used to compare values.
/// \returns A sink yielding the value with the largest selected key.
template <class Selector, class MaxBy = detail::Min<Selector, Greater>>
MaxBy maxBy(Selector selector = Selector()) {
  return MaxBy(std::move(selector));
}

/// Collects a sequence into a container of the given type.
///
/// \returns A sink that materializes the sequence into the container.
template <class Collection, class Collect = detail::Collect<Collection>>
Collect as() {
  return Collect();
}

/// Collects a sequence into a container template with the given allocator.
///
/// \returns A sink that materializes the sequence into the container.
template <
    template <class, class> class Container = std::vector,
    template <class> class Allocator = std::allocator,
    class Collect = detail::CollectTemplate<Container, Allocator>>
Collect as() {
  return Collect();
}

/// Appends each value of a sequence to an existing collection.
///
/// \param collection The collection that values are appended to.
/// \returns A sink that appends the sequence to `collection`.
template <class Collection, class Append = detail::Append<Collection>>
Append appendTo(Collection& collection) {
  return Append(&collection);
}

/// Determines whether a sequence contains a given value.
///
/// \param needle The value to search for.
/// \returns A sink returning true if the sequence contains `needle`.
template <
    class Needle,
    class Contains = detail::Contains<typename std::decay<Needle>::type>>
Contains contains(Needle&& needle) {
  return Contains(std::forward<Needle>(needle));
}

/// Wraps a pipeline so exceptions of a given type are passed to a handler.
///
/// \param handler The error handler invoked for a caught exception.
/// \returns An operator that guards the pipeline against `Exception`.
template <
    class Exception,
    class ErrorHandler,
    class GuardImpl =
        detail::GuardImpl<Exception, typename std::decay<ErrorHandler>::type>>
GuardImpl guard(ErrorHandler&& handler) {
  return GuardImpl(std::forward<ErrorHandler>(handler));
}

/// Unwraps an optional-like value, using a fallback when it is empty.
///
/// \param fallback The value to use when the source value is empty.
/// \returns A sink yielding the unwrapped value or `fallback`.
template <
    class Fallback,
    class UnwrapOr = detail::UnwrapOr<typename std::decay<Fallback>::type>>
UnwrapOr unwrapOr(Fallback&& fallback) {
  return UnwrapOr(std::forward<Fallback>(fallback));
}

} // namespace gen
} // namespace folly

#include <folly/gen/Base-inl.h>
