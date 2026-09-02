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

#include <exception>
#include <typeinfo>

#include <folly/debugging/exception_tracer/Compatibility.h>

#if FOLLY_HAS_EXCEPTION_TRACER

namespace folly {
namespace exception_tracer {

/// Signature of a callback invoked when an exception is thrown via __cxa_throw.
using CxaThrowSig = void(void*, std::type_info*, void (**)(void*)) noexcept;
/// Signature of a callback invoked when a catch block begins via __cxa_begin_catch.
using CxaBeginCatchSig = void(void*) noexcept;
/// Signature of a callback invoked when an exception is rethrown via __cxa_rethrow.
using CxaRethrowSig = void() noexcept;
/// Signature of a callback invoked when a catch block ends via __cxa_end_catch.
using CxaEndCatchSig = void() noexcept;
/// Signature of a callback invoked when an exception is rethrown via std::rethrow_exception.
using RethrowExceptionSig = void(std::exception_ptr) noexcept;

/// Register a callback invoked on each __cxa_throw.
///
/// \param callback The callback to register.
void registerCxaThrowCallback(CxaThrowSig& callback);
/// Register a callback invoked on each __cxa_begin_catch.
///
/// \param callback The callback to register.
void registerCxaBeginCatchCallback(CxaBeginCatchSig& callback);
/// Register a callback invoked on each __cxa_rethrow.
///
/// \param callback The callback to register.
void registerCxaRethrowCallback(CxaRethrowSig& callback);
/// Register a callback invoked on each __cxa_end_catch.
///
/// \param callback The callback to register.
void registerCxaEndCatchCallback(CxaEndCatchSig& callback);
/// Register a callback invoked on each std::rethrow_exception.
///
/// \param callback The callback to register.
void registerRethrowExceptionCallback(RethrowExceptionSig& callback);
/// Unregister a previously registered __cxa_throw callback.
///
/// \param callback The callback to unregister.
void unregisterCxaThrowCallback(CxaThrowSig& callback);
/// Unregister a previously registered __cxa_begin_catch callback.
///
/// \param callback The callback to unregister.
void unregisterCxaBeginCatchCallback(CxaBeginCatchSig& callback);
/// Unregister a previously registered __cxa_rethrow callback.
///
/// \param callback The callback to unregister.
void unregisterCxaRethrowCallback(CxaRethrowSig& callback);
/// Unregister a previously registered __cxa_end_catch callback.
///
/// \param callback The callback to unregister.
void unregisterCxaEndCatchCallback(CxaEndCatchSig& callback);
/// Unregister a previously registered std::rethrow_exception callback.
///
/// \param callback The callback to unregister.
void unregisterRethrowExceptionCallback(RethrowExceptionSig& callback);

} // namespace exception_tracer
} // namespace folly

#endif //  FOLLY_HAS_EXCEPTION_TRACER
