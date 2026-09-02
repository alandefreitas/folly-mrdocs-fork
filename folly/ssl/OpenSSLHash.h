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
#include <stdexcept>
#include <utility>

#include <folly/Exception.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

namespace folly {
namespace ssl {

/// OpenSSL-backed message digest and HMAC helpers.
class OpenSSLHash {
 public:
  /// Incremental message digest over one or more input chunks.
  class Digest {
   public:
    /// Constructs an uninitialized digest; call hash_init() before use.
    Digest() noexcept {} // = default;

    /// Copy constructor; duplicates the other digest's internal state.
    ///
    /// \param that The digest to copy from.
    Digest(const Digest& that) { copy_impl(that); }

    /// Move constructor.
    ///
    /// \param that The digest to move from.
    Digest(Digest&& that) noexcept { move_impl(std::move(that)); }

    /// Copy assignment operator; duplicates the other digest's state.
    ///
    /// \param that The digest to copy from.
    /// \returns A reference to this digest.
    Digest& operator=(const Digest& that) {
      if (this != &that) {
        copy_impl(that);
      }
      return *this;
    }

    /// Move assignment operator.
    ///
    /// \param that The digest to move from.
    /// \returns A reference to this digest.
    Digest& operator=(Digest&& that) noexcept {
      if (this != &that) {
        move_impl(std::move(that));
        that.hash_reset();
      }
      return *this;
    }

    /// Initializes the digest with a message-digest algorithm.
    ///
    /// \param md The OpenSSL message-digest algorithm to use.
    void hash_init(const EVP_MD* md) {
      if (nullptr == ctx_) {
        ctx_.reset(EVP_MD_CTX_new());
        if (nullptr == ctx_) {
          throw_exception<std::runtime_error>(
              "EVP_MD_CTX_new() returned nullptr");
        }
      }
      check_libssl_result(1, EVP_DigestInit_ex(ctx_.get(), md, nullptr));
      md_ = md;
    }

    /// Feeds more input bytes into the digest.
    ///
    /// \param data The bytes to hash.
    void hash_update(ByteRange data) {
      if (nullptr == ctx_) {
        throw_exception<std::runtime_error>(
            "hash_update() called without hash_init()");
      }
      check_libssl_result(
          1, EVP_DigestUpdate(ctx_.get(), data.data(), data.size()));
    }

    /// Feeds the contents of an IOBuf chain into the digest.
    ///
    /// \param data The buffer chain to hash.
    void hash_update(const IOBuf& data) {
      for (auto r : data) {
        hash_update(r);
      }
    }

    /// Computes the final digest value and resets the object.
    ///
    /// \param out Destination buffer; its size must equal the digest length.
    void hash_final(MutableByteRange out) {
      if (nullptr == ctx_) {
        throw_exception<std::runtime_error>(
            "hash_final() called without hash_init()");
      }
      const auto size = EVP_MD_size(md_);
      check_out_size(size_t(size), out);
      unsigned int len = 0;
      check_libssl_result(1, EVP_DigestFinal_ex(ctx_.get(), out.data(), &len));
      check_libssl_result(size, int(len));
      hash_reset();
    }

    /// Returns the digest output size in bytes.
    ///
    /// \returns The digest length in bytes.
    size_t hash_size() const {
      if (nullptr == ctx_) {
        throw_exception<std::runtime_error>(
            "hash_size() called without hash_init()");
      }
      return EVP_MD_size(md_);
    }

    /// Returns the digest algorithm's block size in bytes.
    ///
    /// \returns The block size in bytes.
    size_t block_size() const {
      if (nullptr == ctx_) {
        throw_exception<std::runtime_error>(
            "block_size() called without hash_init()");
      }
      return EVP_MD_block_size(md_);
    }

   private:
    const EVP_MD* md_{nullptr};
    EvpMdCtxUniquePtr ctx_{nullptr};

    void hash_reset() noexcept {
      ctx_.reset(nullptr);
      md_ = nullptr;
    }

    void copy_impl(const Digest& that) {
      if (that.md_ != nullptr && that.ctx_ != nullptr) {
        hash_init(that.md_);
        check_libssl_result(1, EVP_MD_CTX_copy_ex(ctx_.get(), that.ctx_.get()));
      } else {
        this->hash_reset();
      }
    }

    void move_impl(Digest&& that) noexcept {
      std::swap(this->md_, that.md_);
      std::swap(this->ctx_, that.ctx_);
    }
  };

  /// Computes a one-shot digest of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param md The message-digest algorithm to use.
  /// \param data The input bytes to hash.
  static void hash(MutableByteRange out, const EVP_MD* md, ByteRange data) {
    Digest hash;
    hash.hash_init(md);
    hash.hash_update(data);
    hash.hash_final(out);
  }
  /// Computes a one-shot digest of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param md The message-digest algorithm to use.
  /// \param data The input buffer chain to hash.
  static void hash(MutableByteRange out, const EVP_MD* md, const IOBuf& data) {
    Digest hash;
    hash.hash_init(md);
    hash.hash_update(data);
    hash.hash_final(out);
  }
  /// Computes a SHA-1 digest of a byte range (deprecated, insecure).
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input bytes to hash.
  [[deprecated(
      "sha1 should never be used. It is slow and insecure. See https://www.internalfb.com/wiki/What_Hash_Should_I_Use")]]
  static void sha1(MutableByteRange out, ByteRange data) {
    hash(out, EVP_sha1(), data);
  }
  /// Computes a SHA-1 digest of an IOBuf chain (deprecated, insecure).
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input buffer chain to hash.
  [[deprecated(
      "sha1 should never be used. It is slow and insecure. See https://www.internalfb.com/wiki/What_Hash_Should_I_Use")]]
  static void sha1(MutableByteRange out, const IOBuf& data) {
    hash(out, EVP_sha1(), data);
  }
  /// Computes a SHA-256 digest of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input bytes to hash.
  static void sha256(MutableByteRange out, ByteRange data) {
    hash(out, EVP_sha256(), data);
  }
  /// Computes a SHA-256 digest of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input buffer chain to hash.
  static void sha256(MutableByteRange out, const IOBuf& data) {
    hash(out, EVP_sha256(), data);
  }
  /// Computes a SHA-512 digest of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input bytes to hash.
  static void sha512(MutableByteRange out, ByteRange data) {
    hash(out, EVP_sha512(), data);
  }
  /// Computes a SHA-512 digest of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input buffer chain to hash.
  static void sha512(MutableByteRange out, const IOBuf& data) {
    hash(out, EVP_sha512(), data);
  }
#if FOLLY_OPENSSL_HAS_BLAKE2B
  /// Computes a BLAKE2s-256 digest of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input bytes to hash.
  static void blake2s256(MutableByteRange out, ByteRange data) {
    hash(out, EVP_blake2s256(), data);
  }
  /// Computes a BLAKE2s-256 digest of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input buffer chain to hash.
  static void blake2s256(MutableByteRange out, const IOBuf& data) {
    hash(out, EVP_blake2s256(), data);
  }
  /// Computes a BLAKE2b-512 digest of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input bytes to hash.
  static void blake2b512(MutableByteRange out, ByteRange data) {
    hash(out, EVP_blake2b512(), data);
  }
  /// Computes a BLAKE2b-512 digest of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param data The input buffer chain to hash.
  static void blake2b512(MutableByteRange out, const IOBuf& data) {
    hash(out, EVP_blake2b512(), data);
  }
#endif

  /// Incremental keyed-hash (HMAC) over one or more input chunks.
  class Hmac {
   public:
    /// Constructs an uninitialized HMAC; call hash_init() before use.
    Hmac() noexcept {} // = default;

    /// Copy constructor; duplicates the other HMAC's internal state.
    ///
    /// \param that The HMAC to copy from.
    Hmac(const Hmac& that) { copy_impl(that); }

    /// Move constructor.
    ///
    /// \param that The HMAC to move from.
    Hmac(Hmac&& that) noexcept { move_impl(std::move(that)); }

    /// Copy assignment operator; duplicates the other HMAC's state.
    ///
    /// \param that The HMAC to copy from.
    /// \returns A reference to this HMAC.
    Hmac& operator=(const Hmac& that) {
      if (this != &that) {
        copy_impl(that);
      }
      return *this;
    }

    /// Move assignment operator.
    ///
    /// \param that The HMAC to move from.
    /// \returns A reference to this HMAC.
    Hmac& operator=(Hmac&& that) noexcept {
      if (this != &that) {
        move_impl(std::move(that));
        that.hash_reset();
      }
      return *this;
    }

    /// Initializes the HMAC with an algorithm and secret key.
    ///
    /// \param md The message-digest algorithm to use.
    /// \param key The secret key.
    void hash_init(const EVP_MD* md, ByteRange key) {
      ensure_ctx();
      check_libssl_result(
          1,
          HMAC_Init_ex(ctx_.get(), key.data(), int(key.size()), md, nullptr));
      md_ = md;
    }

    /// Feeds more input bytes into the HMAC.
    ///
    /// \param data The bytes to hash.
    void hash_update(ByteRange data) {
      if (ctx_ == nullptr) {
        throw_exception<std::runtime_error>(
            "hash_update() called without hash_init()");
      }
      check_libssl_result(1, HMAC_Update(ctx_.get(), data.data(), data.size()));
    }

    /// Feeds the contents of an IOBuf chain into the HMAC.
    ///
    /// \param data The buffer chain to hash.
    void hash_update(const IOBuf& data) {
      for (auto r : data) {
        hash_update(r);
      }
    }

    /// Computes the final HMAC value and resets the object.
    ///
    /// \param out Destination buffer; its size must equal the digest length.
    void hash_final(MutableByteRange out) {
      if (ctx_ == nullptr) {
        throw_exception<std::runtime_error>(
            "hash_final() called without hash_init()");
      }
      const auto size = EVP_MD_size(md_);
      check_out_size(size_t(size), out);
      unsigned int len = 0;
      check_libssl_result(1, HMAC_Final(ctx_.get(), out.data(), &len));
      check_libssl_result(size, int(len));
      hash_reset();
    }

   private:
    const EVP_MD* md_{nullptr};
    HmacCtxUniquePtr ctx_{nullptr};

    void ensure_ctx() {
      if (ctx_ == nullptr) {
        ctx_.reset(HMAC_CTX_new());
        if (ctx_ == nullptr) {
          throw_exception<std::runtime_error>(
              "HMAC_CTX_new() returned nullptr");
        }
      }
    }

    void hash_reset() noexcept {
      md_ = nullptr;
      ctx_.reset(nullptr);
    }

    void copy_impl(const Hmac& that) {
      if (that.md_ != nullptr && that.ctx_ != nullptr) {
        ensure_ctx();
        this->md_ = that.md_;
        check_libssl_result(
            1, HMAC_CTX_copy(this->ctx_.get(), that.ctx_.get()));
      } else {
        hash_reset();
      }
    }

    void move_impl(Hmac&& that) noexcept {
      std::swap(this->md_, that.md_);
      std::swap(this->ctx_, that.ctx_);
    }
  };

  /// Computes a one-shot HMAC of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param md The message-digest algorithm to use.
  /// \param key The secret key.
  /// \param data The input bytes to authenticate.
  static void hmac(
      MutableByteRange out, const EVP_MD* md, ByteRange key, ByteRange data) {
    Hmac hmac;
    hmac.hash_init(md, key);
    hmac.hash_update(data);
    hmac.hash_final(out);
  }
  /// Computes a one-shot HMAC of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param md The message-digest algorithm to use.
  /// \param key The secret key.
  /// \param data The input buffer chain to authenticate.
  static void hmac(
      MutableByteRange out,
      const EVP_MD* md,
      ByteRange key,
      const IOBuf& data) {
    Hmac hmac;
    hmac.hash_init(md, key);
    hmac.hash_update(data);
    hmac.hash_final(out);
  }
  /// Computes an HMAC-SHA-1 of a byte range (deprecated, insecure).
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input bytes to authenticate.
  [[deprecated(
      "sha1 should never be used. It is slow and insecure. See https://www.internalfb.com/wiki/What_Hash_Should_I_Use")]]
  static void hmac_sha1(MutableByteRange out, ByteRange key, ByteRange data) {
    hmac(out, EVP_sha1(), key, data);
  }
  /// Computes an HMAC-SHA-1 of an IOBuf chain (deprecated, insecure).
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input buffer chain to authenticate.
  [[deprecated(
      "sha1 should never be used. It is slow and insecure. See https://www.internalfb.com/wiki/What_Hash_Should_I_Use")]]
  static void hmac_sha1(
      MutableByteRange out, ByteRange key, const IOBuf& data) {
    hmac(out, EVP_sha1(), key, data);
  }
  /// Computes an HMAC-SHA-256 of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input bytes to authenticate.
  static void hmac_sha256(MutableByteRange out, ByteRange key, ByteRange data) {
    hmac(out, EVP_sha256(), key, data);
  }
  /// Computes an HMAC-SHA-256 of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input buffer chain to authenticate.
  static void hmac_sha256(
      MutableByteRange out, ByteRange key, const IOBuf& data) {
    hmac(out, EVP_sha256(), key, data);
  }
  /// Computes an HMAC-SHA-512 of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input bytes to authenticate.
  static void hmac_sha512(MutableByteRange out, ByteRange key, ByteRange data) {
    hmac(out, EVP_sha512(), key, data);
  }
  /// Computes an HMAC-SHA-512 of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input buffer chain to authenticate.
  static void hmac_sha512(
      MutableByteRange out, ByteRange key, const IOBuf& data) {
    hmac(out, EVP_sha512(), key, data);
  }
#if FOLLY_OPENSSL_HAS_BLAKE2B
  /// Computes an HMAC-BLAKE2s-256 of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input bytes to authenticate.
  static void hmac_blake2s256(
      MutableByteRange out, ByteRange key, ByteRange data) {
    hmac(out, EVP_blake2s256(), key, data);
  }
  /// Computes an HMAC-BLAKE2s-256 of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input buffer chain to authenticate.
  static void hmac_blake2s256(
      MutableByteRange out, ByteRange key, const IOBuf& data) {
    hmac(out, EVP_blake2s256(), key, data);
  }
  /// Computes an HMAC-BLAKE2b-512 of a byte range.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input bytes to authenticate.
  static void hmac_blake2b512(
      MutableByteRange out, ByteRange key, ByteRange data) {
    hmac(out, EVP_blake2b512(), key, data);
  }
  /// Computes an HMAC-BLAKE2b-512 of an IOBuf chain.
  ///
  /// \param out Destination buffer sized to the digest length.
  /// \param key The secret key.
  /// \param data The input buffer chain to authenticate.
  static void hmac_blake2b512(
      MutableByteRange out, ByteRange key, const IOBuf& data) {
    hmac(out, EVP_blake2b512(), key, data);
  }
#endif

 private:
  static inline void check_out_size(size_t size, MutableByteRange out) {
    if (FOLLY_LIKELY(size == out.size())) {
      return;
    }
    check_out_size_throw(size, out);
  }
  [[noreturn]] static void check_out_size_throw(
      size_t size, MutableByteRange out);

  static inline void check_libssl_result(int expected, int result) {
    if (FOLLY_LIKELY(result == expected)) {
      return;
    }
    throw_exception<std::runtime_error>("openssl crypto function failed");
  }
};

} // namespace ssl
} // namespace folly
