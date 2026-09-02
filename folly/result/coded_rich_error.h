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

#include <folly/result/rich_error.h>
#include <folly/result/rich_error_base.h>
#include <folly/result/rich_error_code.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/rich_msg.h>

#if FOLLY_HAS_RESULT

/// The Folly library.
namespace folly {

/// Start with the user docs in `docs/rich_error_code.md`.
///
/// Error type for storing 1 or more application-specific error codes.  Loosely
/// parallel to `std::system_error` -- though, if you actually have an OS
/// error, please read `errc_rich_error.h` next.
///
/// Compared to `system_error`, this has all the advantages of rich errors --
/// RTTI-free access on the happy path, automatic source location capture,
/// cheaper message support, `fmt` and `<<` support, etc.
///
/// If you specifically need an analog of `std::nested_exception`, where the
/// new error shadows the old (partly or completely), then check out the two
/// templates in `nestable_coded_rich_error.h`.
template <typename... Codes>
class coded_rich_error : public rich_error_base {
 private:
  template <typename Code>
  struct code_entry {
    [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] Code code_value_;
  };

  template <typename... Cs>
  struct code_storage : code_entry<Cs>... {};

  code_storage<Codes...> codes_;
  rich_msg msg_;

 public:
  /// See `make_coded_rich_error()`, which deduces the code types. This is
  /// useful as `errc_rich_error::make`, and in similar contexts.
  ///
  /// NB: It is fine to add a a variant taking `rich_msg` if needed.
  ///
  /// \param codes The error codes to store.
  /// \param snl The format string and source location.
  /// \param args Format arguments for the message.
  /// \returns A `rich_error<coded_rich_error<Codes...>>`.
  template <typename... As>
  static rich_error<coded_rich_error<Codes...>> make(
      Codes... codes,
      ext::format_string_and_location<std::type_identity_t<As>...> snl = "",
      As const&... args) {
    return rich_error<coded_rich_error<Codes...>>(
        std::move(codes)..., rich_msg{std::move(snl), args...});
  }

  /// Constructs from the codes and an optional message.
  ///
  /// Private. For ergonomics, prefer `make_coded_rich_error()` or `::make()`
  /// to construct `rich_error<coded_rich_error>{...}`.
  ///
  /// \param codes The error codes to store.
  /// \param msg The rich message (defaults to empty for immortal errors).
  explicit constexpr coded_rich_error(
      Codes... codes,
      // This default is for `immortal_rich_error` usage.
      rich_msg msg = vtag<literal_string{""}>)
      : codes_{code_entry<Codes>{codes}...}, msg_{std::move(msg)} {}

  /// Returns the source location captured for this error.
  ///
  /// \returns The originating source location.
  folly::source_location source_location() const noexcept override {
    return msg_.location();
  }

  /// Returns the partial message for this error.
  ///
  /// \returns A NUL-terminated partial description of the error.
  constexpr const char* partial_message() const noexcept override {
    return msg_.message();
  }

  /// This will rarely be used by end-users, since `get_rich_error_code<Code>`
  /// is the canonical accessor for codes.
  ///
  /// \returns A reference to the stored code of type `Code`.
  template <typename Code>
  constexpr const Code& code_of_type() const {
    return static_cast<const code_entry<Code>&>(codes_).code_value_;
  }

  /// Easy code access for the common case of "just one code".
  ///
  /// \returns A reference to the single stored code.
  template <size_t N = sizeof...(Codes)>
    requires(N == 1)
  constexpr const auto& code() const {
    return code_of_type<Codes...>();
  }

  /// `get_rich_error_code` protocol support.
  using folly_rich_error_codes_t = rich_error_bases_and_own_codes<
      coded_rich_error,
      tag_t<>,
      &coded_rich_error::template code_of_type<Codes>...>;
  /// Populates a code query for the `get_rich_error_code` protocol.
  ///
  /// \param c The code query to inspect and populate.
  constexpr void retrieve_code(rich_error_code_query& c) const override {
    return folly_rich_error_codes_t::retrieve_code(*this, c);
  }

  /// Fast exception-lookup hints for this error type.
  using folly_get_exception_hint_types = rich_error_hints<coded_rich_error>;
};

/// Callable that creates `...coded_rich_error` factory functions.
///
/// `ExtraArgs` specifies additional required arguments between the code(s) and
/// message. For example, nestable errors require `rich_exception_ptr`.
template <template <typename...> class ErrorT, typename... ExtraArgs>
struct make_coded_rich_error_fn {
  /// Builds an error from a single code and a format message.
  ///
  /// Common case -- single code:
  ///   ErrorT(MyCode::VAL, [extra_args,] "info={}", info)
  ///
  /// \param code The single error code.
  /// \param extra_args Extra required arguments between the code and message.
  /// \param snl The format string and source location.
  /// \param args Format arguments for the message.
  /// \returns A `rich_error<ErrorT<Code>>` holding the code and message.
  template <typename Code, typename... As>
  constexpr rich_error<ErrorT<Code>> operator()(
      Code code,
      ExtraArgs... extra_args,
      ext::format_string_and_location<std::type_identity_t<As>...> snl = "",
      As const&... args) const {
    return rich_error<ErrorT<Code>>{
        std::move(code),
        std::move(extra_args)...,
        rich_msg{std::move(snl), args...}};
  }
  /// Builds an error from a message and multiple codes.
  ///
  /// Power users -- multiple codes:
  ///   ErrorT(rich_msg{"msg"}, [extra_args,] Code1::A, Code2::B)
  ///
  /// \param msg The rich message.
  /// \param extra_args Extra required arguments between the message and codes.
  /// \param codes The error codes.
  /// \returns A `rich_error<ErrorT<Codes...>>` holding the codes and message.
  template <typename... Codes>
  constexpr rich_error<ErrorT<Codes...>> operator()(
      rich_msg msg, ExtraArgs... extra_args, Codes... codes) const {
    return rich_error<ErrorT<Codes...>>{
        std::move(codes)..., std::move(extra_args)..., std::move(msg)};
  }
};

/// Make `rich_error<coded_rich_error<Codes...>>` with deduced code types.
///
/// Common case -- single code:
///   make_coded_rich_error(MyCode::VAL, "more={}, info={}", more, info)
/// Power users -- multiple codes:
///   make_coded_rich_error(rich_msg{"a={}", a}, Code1::A, Code2::B)
inline constexpr make_coded_rich_error_fn<coded_rich_error>
    make_coded_rich_error{};

} // namespace folly

#endif // FOLLY_HAS_RESULT
