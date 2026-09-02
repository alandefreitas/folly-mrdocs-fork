// Minimal {fmt} stand-in for MrDocs parsing of Folly.
//
// This is NOT the real {fmt}. It declares just enough of the public API that
// Folly's headers reference (fmt::format, fmt::formatter<T> and its
// parse/format members, fmt::appender, fmt::format_context, format_string,
// etc.) so those headers parse under MrDocs in isolation. None of it is meant
// to compile into working code.
#ifndef FMT_CORE_H_
#define FMT_CORE_H_

#include <cstddef>
#include <string>
#include <type_traits>

// Report a modern {fmt} so Folly takes its current-API code paths
// (many headers gate on `#if FMT_VERSION >= N`).
#define FMT_VERSION 100200

namespace fmt {

template <class Char>
struct basic_string_view {
    const Char* data_ = nullptr;
    std::size_t size_ = 0;
    constexpr basic_string_view() = default;
    constexpr basic_string_view(const Char* d, std::size_t s) : data_(d), size_(s) {}
    basic_string_view(const Char* d) : data_(d) {}
    template <class T>
    basic_string_view(const T& s) : data_(s.data()), size_(s.size()) {}
    constexpr const Char* data() const { return data_; }
    constexpr std::size_t size() const { return size_; }
};
using string_view = basic_string_view<char>;
using wstring_view = basic_string_view<wchar_t>;

// Output-iterator stand-in used as the default context iterator.
struct appender {
    using iterator_category = void;
    template <class T>
    appender& operator=(const T&) { return *this; }
    appender& operator*() { return *this; }
    appender& operator++() { return *this; }
    appender operator++(int) { return *this; }
};

template <class Char>
struct basic_format_parse_context {
    using char_type = Char;
    using iterator = const Char*;
    iterator begin_ = nullptr;
    iterator end_ = nullptr;
    constexpr iterator begin() const { return begin_; }
    constexpr iterator end() const { return end_; }
    constexpr void advance_to(iterator) {}
    constexpr int next_arg_id() { return 0; }
    constexpr void check_arg_id(int) {}
};
using format_parse_context = basic_format_parse_context<char>;

template <class OutputIt, class Char>
struct basic_format_context {
    using char_type = Char;
    using iterator = OutputIt;
    iterator out() { return iterator{}; }
    void advance_to(iterator) {}
};
using format_context = basic_format_context<appender, char>;
using wformat_context = basic_format_context<appender, wchar_t>;

// Primary formatter template. Real {fmt} leaves this incomplete and relies on
// specializations, but Folly derives from concrete specializations such as
// formatter<string_view> and calls their parse()/format(), so the primary
// template carries usable members here.
template <class T, class Char = char>
struct formatter {
    template <class ParseContext>
    constexpr typename ParseContext::iterator parse(ParseContext& ctx) {
        return ctx.begin();
    }
    template <class FormatContext>
    typename FormatContext::iterator format(const T&, FormatContext& ctx) const {
        return ctx.out();
    }
};

// Format-string wrappers. Kept permissive: constructible from anything
// string-like so both compile-time and runtime call sites parse.
template <class... Args>
struct basic_format_string {
    string_view str_;
    template <class S>
    basic_format_string(const S&) {}
    basic_format_string() = default;
};
template <class... Args>
using format_string = basic_format_string<std::type_identity_t<Args>...>;

template <class T>
struct runtime_format_string {
    T value;
};
template <class S>
runtime_format_string<S> runtime(const S& s) { return {s}; }

// Type-erased argument stores.
struct format_args {};
struct format_arg {};

template <class... Args>
format_args make_format_args(Args&&...) { return {}; }

template <class T>
const void* ptr(const T* p) { return p; }
template <class T>
const void* ptr(T) { return nullptr; }

template <class T>
format_arg arg(const char*, const T&) { return {}; }

template <class Context = format_context>
struct dynamic_format_arg_store {
    void reserve(std::size_t, std::size_t) {}
    template <class T>
    void push_back(const T&) {}
    void clear() {}
};

// is_formattable trait: report every type as formattable for parsing purposes.
template <class T, class Char = char>
struct is_formattable : std::true_type {};

// nested_formatter helper (referenced by some Folly headers).
template <class T, class Char = char>
struct nested_formatter {
    formatter<T, Char> inner;
    template <class ParseContext>
    constexpr typename ParseContext::iterator parse(ParseContext& ctx) { return ctx.begin(); }
};

// format_to_n result.
template <class OutputIt>
struct format_to_n_result {
    OutputIt out;
    std::size_t size;
};

// Free functions. Deliberately permissive variadic signatures so any Folly
// call site (with format_string, runtime(...), etc.) parses.
template <class S, class... Args>
std::string format(const S&, Args&&...) { return std::string(); }

template <class S, class... Args>
std::string vformat(const S&, const Args&...) { return std::string(); }

template <class OutputIt, class S, class... Args>
OutputIt format_to(OutputIt out, const S&, Args&&...) { return out; }

template <class OutputIt, class S, class... Args>
OutputIt vformat_to(OutputIt out, const S&, const Args&...) { return out; }

template <class OutputIt, class S, class... Args>
format_to_n_result<OutputIt> format_to_n(OutputIt out, std::size_t, const S&, Args&&...) {
    return {out, 0};
}

template <class S, class... Args>
std::size_t formatted_size(const S&, Args&&...) { return 0; }

} // namespace fmt

#endif // FMT_CORE_H_
