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

#include <folly/ssl/PasswordCollector.h>

namespace folly {

/// Password collector that reads a passphrase from a file on construction.
class PasswordInFile : public ssl::PasswordCollector {
 public:
  /// Constructs the collector by reading the password from \p file.
  /// \param file Path of the file holding the password.
  explicit PasswordInFile(const std::string& file);
  /// Destroys the collector, wiping the cached password.
  ~PasswordInFile() override;

  /// Copies the cached password into \p password.
  /// \param password Output string that receives the password.
  /// \param size Unused buffer-size hint from the base interface.
  void getPassword(std::string& password, int size) const override {
    password = password_;
  }

  /// Returns the cached password as a C string.
  /// \returns A null-terminated C string pointing to the cached password.
  const char* getPasswordStr() const { return password_.c_str(); }

  /// Returns the path of the file the password was read from.
  /// \returns The path of the file the password was read from.
  const std::string& describe() const override { return fileName_; }

 protected:
  std::string fileName_; ///< Path of the file the password was read from.
  std::string password_; ///< Cached password contents.
};

} // namespace folly
