// Minimal glog stand-in for MrDocs parsing of Folly.
//
// This is NOT real glog. It defines the logging macros (LOG, CHECK, VLOG,
// DCHECK, CHECK_EQ, ...) as stream-eating no-ops and the few google:: symbols
// Folly references (google::LogSink, google::LogSeverity), so Folly headers
// that use these in inline function bodies parse under MrDocs in isolation.
#ifndef GLOG_LOGGING_H_
#define GLOG_LOGGING_H_

#include <cstddef>
#include <cstdint>
#include <ostream>

namespace google {

using LogSeverity = int;
const LogSeverity GLOG_INFO = 0;
const LogSeverity GLOG_WARNING = 1;
const LogSeverity GLOG_ERROR = 2;
const LogSeverity GLOG_FATAL = 3;
const LogSeverity INFO = 0;
const LogSeverity WARNING = 1;
const LogSeverity ERROR = 2;
const LogSeverity FATAL = 3;

// Sink base class that Folly's BridgeFromGoogleLogging derives from.
class LogSink {
public:
    virtual ~LogSink() {}
    virtual void send(
        LogSeverity severity,
        const char* full_filename,
        const char* base_filename,
        int line,
        const struct ::tm* tm_time,
        const char* message,
        size_t message_len);
};

// Stream stand-in that swallows every `<< value`.
class NullStream {
public:
    template <class T>
    NullStream& operator<<(const T&) { return *this; }
    NullStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
    NullStream& operator<<(std::ios_base& (*)(std::ios_base&)) { return *this; }
};

// Turns a NullStream expression into a void, for the ternary macro idiom.
class NullStreamVoidify {
public:
    void operator&(const NullStream&) {}
};

} // namespace google

#define FOLLY_GLOG_NULLSTREAM ::google::NullStream()
#define FOLLY_GLOG_VOIDIFY(cond) \
    !(cond) ? (void)0 : ::google::NullStreamVoidify() & FOLLY_GLOG_NULLSTREAM

// Logging macros.
#define LOG(severity) FOLLY_GLOG_NULLSTREAM
#define VLOG(verboselevel) FOLLY_GLOG_NULLSTREAM
#define DLOG(severity) FOLLY_GLOG_NULLSTREAM
#define DVLOG(verboselevel) FOLLY_GLOG_NULLSTREAM
#define SYSLOG(severity) FOLLY_GLOG_NULLSTREAM
#define PLOG(severity) FOLLY_GLOG_NULLSTREAM
#define RAW_LOG(severity, ...) FOLLY_GLOG_NULLSTREAM

#define LOG_IF(severity, condition) FOLLY_GLOG_VOIDIFY(condition)
#define VLOG_IF(verboselevel, condition) FOLLY_GLOG_VOIDIFY(condition)
#define DLOG_IF(severity, condition) FOLLY_GLOG_VOIDIFY(condition)
#define LOG_EVERY_N(severity, n) FOLLY_GLOG_NULLSTREAM
#define VLOG_EVERY_N(verboselevel, n) FOLLY_GLOG_NULLSTREAM
#define LOG_FIRST_N(severity, n) FOLLY_GLOG_NULLSTREAM
#define LOG_EVERY_T(severity, seconds) FOLLY_GLOG_NULLSTREAM

#define VLOG_IS_ON(verboselevel) (false)

// Check macros.
#define CHECK(condition) FOLLY_GLOG_VOIDIFY(condition)
#define CHECK_EQ(a, b) FOLLY_GLOG_VOIDIFY((a) == (b))
#define CHECK_NE(a, b) FOLLY_GLOG_VOIDIFY((a) != (b))
#define CHECK_LT(a, b) FOLLY_GLOG_VOIDIFY((a) < (b))
#define CHECK_LE(a, b) FOLLY_GLOG_VOIDIFY((a) <= (b))
#define CHECK_GT(a, b) FOLLY_GLOG_VOIDIFY((a) > (b))
#define CHECK_GE(a, b) FOLLY_GLOG_VOIDIFY((a) >= (b))
#define CHECK_NOTNULL(val) (val)
#define CHECK_STREQ(a, b) FOLLY_GLOG_NULLSTREAM
#define CHECK_STRNE(a, b) FOLLY_GLOG_NULLSTREAM

#define DCHECK(condition) FOLLY_GLOG_VOIDIFY(condition)
#define DCHECK_EQ(a, b) FOLLY_GLOG_VOIDIFY((a) == (b))
#define DCHECK_NE(a, b) FOLLY_GLOG_VOIDIFY((a) != (b))
#define DCHECK_LT(a, b) FOLLY_GLOG_VOIDIFY((a) < (b))
#define DCHECK_LE(a, b) FOLLY_GLOG_VOIDIFY((a) <= (b))
#define DCHECK_GT(a, b) FOLLY_GLOG_VOIDIFY((a) > (b))
#define DCHECK_GE(a, b) FOLLY_GLOG_VOIDIFY((a) >= (b))
#define DCHECK_NOTNULL(val) (val)

#endif // GLOG_LOGGING_H_
