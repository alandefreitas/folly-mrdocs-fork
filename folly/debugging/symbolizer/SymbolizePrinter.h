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

#include <cstdint>

#include <folly/FBString.h>
#include <folly/Range.h>
#include <folly/debugging/symbolizer/SymbolizedFrame.h>

namespace folly {
/// A reference-counted, chained memory buffer.
class IOBuf;

namespace symbolizer {

/**
 * Format one address in the way it's usually printed by SymbolizePrinter.
 * Async-signal-safe.
 */
class AddressFormatter {
 public:
  /// Construct an address formatter.
  AddressFormatter();

  /**
   * Format the address. Returns an internal buffer.
   *
   * \param address The instruction address to format.
   * \returns A view of the internal buffer holding the formatted address.
   */
  StringPiece format(uintptr_t address);

 private:
  static constexpr char bufTemplate[] = "    @ 0000000000000000";
  char buf_[sizeof(bufTemplate)];
};

/**
 * Print a list of symbolized addresses. Base class.
 */
class SymbolizePrinter {
 public:
  /**
   * Print one frame, no ending newline.
   *
   * \param frame The symbolized frame to print.
   */
  void print(const SymbolizedFrame& frame);

  /**
   * Print one frame with ending newline.
   *
   * \param frame The symbolized frame to print.
   */
  void println(const SymbolizedFrame& frame);

  /**
   * Print multiple frames on separate lines.
   *
   * \param frames The array of frames to print.
   * \param frameCount The number of frames in frames.
   */
  void println(const SymbolizedFrame* frames, size_t frameCount);

  /**
   * Print a string, no endling newline.
   *
   * \param sp The string to print.
   */
  void print(StringPiece sp) { doPrint(sp); }

  /**
   * Print multiple frames on separate lines, skipping the first
   * skip addresses.
   *
   * \param fa The frame array to print.
   * \param skip The number of leading frames to skip.
   */
  template <size_t N>
  void println(const FrameArray<N>& fa, size_t skip = 0) {
    if (skip < fa.frameCount) {
      println(fa.frames + skip, fa.frameCount - skip);
    }
  }

  /**
   * If output buffered inside this class, send it to the output stream, so that
   * any output done in other ways appears after this.
   */
  virtual void flush() {}

  /// Virtual destructor.
  virtual ~SymbolizePrinter() {}

  /// Bit flags controlling how frames are printed.
  enum Options {
    NO_FILE_AND_LINE = 1 << 0, ///< Skip file and line information.

    TERSE = 1 << 1, ///< As terse as it gets: function name if found, address otherwise.

    COLOR = 1 << 2, ///< Always colorize output (ANSI escape code).

    COLOR_IF_TTY = 1 << 3, ///< Colorize output only if printed to a TTY (ANSI escape code).

    NO_FRAME_ADDRESS = 1 << 4, ///< Skip frame address information.

    TERSE_FILE_AND_LINE = 1 << 5, ///< Simple file and line output.
  };

  // NOTE: enum values used as indexes in kColorMap.
  /// ANSI colors used to colorize printed output.
  enum Color {
    Default, ///< Default terminal color.
    Red, ///< Red.
    Green, ///< Green.
    Yellow, ///< Yellow.
    Blue, ///< Blue.
    Cyan, ///< Cyan.
    White, ///< White.
    Purple, ///< Purple.
    Num ///< Number of colors; not a color itself.
  };
  /// Emit the ANSI escape code that selects color c.
  ///
  /// \param c The color to switch to.
  void color(Color c);

 protected:
  /// Construct the base printer.
  ///
  /// \param options The printing options bitmask.
  /// \param isTty Whether the output destination is a TTY.
  explicit SymbolizePrinter(int options, bool isTty = false)
      : options_(options), isTty_(isTty) {}

  /// The printing options bitmask.
  const int options_;
  /// Whether the output destination is a TTY.
  const bool isTty_;

 private:
  void printTerse(const SymbolizedFrame& frame);
  virtual void doPrint(StringPiece sp) = 0;

  static constexpr std::array<const char*, Color::Num> kColorMap = {{
      "\x1B[0m",
      "\x1B[31m",
      "\x1B[32m",
      "\x1B[33m",
      "\x1B[34m",
      "\x1B[36m",
      "\x1B[37m",
      "\x1B[35m",
  }};
};

/**
 * Print a list of symbolized addresses to a stream.
 * Not reentrant. Do not use from signal handling code.
 */
class OStreamSymbolizePrinter : public SymbolizePrinter {
 public:
  /// Construct a printer that writes to an output stream.
  ///
  /// \param out The output stream to write to.
  /// \param options The printing options bitmask.
  explicit OStreamSymbolizePrinter(std::ostream& out, int options = 0);

 private:
  void doPrint(StringPiece sp) override;
  std::ostream& out_;
};

/**
 * Print a list of symbolized addresses to a file descriptor.
 * Ignores errors. Async-signal-safe.
 */
class FDSymbolizePrinter : public SymbolizePrinter {
 public:
  /// Construct a printer that writes to a file descriptor.
  ///
  /// \param fd The file descriptor to write to.
  /// \param options The printing options bitmask.
  /// \param bufferSize The size of the internal output buffer, in bytes.
  explicit FDSymbolizePrinter(int fd, int options = 0, size_t bufferSize = 0);
  /// Flush any buffered output and destroy the printer.
  ~FDSymbolizePrinter() override;
  /// Send any buffered output to the file descriptor.
  virtual void flush() override;

 private:
  void doPrint(StringPiece sp) override;

  const int fd_;
  std::unique_ptr<IOBuf> buffer_;
};

/**
 * Print a list of symbolized addresses to a FILE*.
 * Ignores errors. Not reentrant. Do not use from signal handling code.
 */
class FILESymbolizePrinter : public SymbolizePrinter {
 public:
  /// Construct a printer that writes to a FILE*.
  ///
  /// \param file The FILE stream to write to.
  /// \param options The printing options bitmask.
  explicit FILESymbolizePrinter(FILE* file, int options = 0);

 private:
  void doPrint(StringPiece sp) override;
  FILE* const file_ = nullptr;
};

/**
 * Print a list of symbolized addresses to a std::string.
 * Not reentrant. Do not use from signal handling code.
 */
class StringSymbolizePrinter : public SymbolizePrinter {
 public:
  /// Construct a printer that accumulates output into a string.
  ///
  /// \param options The printing options bitmask.
  explicit StringSymbolizePrinter(int options = 0)
      : SymbolizePrinter(options) {}

  /// Get the accumulated output as a std::string.
  ///
  /// \returns The accumulated output as a std::string.
  std::string str() const { return buf_.toStdString(); }
  /// Get the accumulated output as an fbstring.
  ///
  /// \returns The accumulated output as an fbstring.
  const fbstring& fbstr() const { return buf_; }
  /// Move the accumulated output out as an fbstring.
  ///
  /// \returns The accumulated output, moved out as an fbstring.
  fbstring moveFbString() { return std::move(buf_); }

 private:
  void doPrint(StringPiece sp) override;
  fbstring buf_;
};

} // namespace symbolizer
} // namespace folly
