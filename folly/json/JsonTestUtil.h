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

#include <iostream>
#include <string>

#include <folly/Range.h>
#include <folly/json/dynamic.h>

/// Facebook's open-source library of C++ components.
namespace folly {

/**
 * Compares two JSON strings and returns whether they represent the
 * same document (thus ignoring things like object ordering or multiple
 * representations of the same number).
 *
 * This is implemented by deserializing both strings into dynamic, so it
 * is not efficient and it is meant to only be used in tests.
 *
 * It will throw an exception if any of the inputs is invalid.
 *
 * \param json1 The first JSON string.
 * \param json2 The second JSON string.
 * \returns True if both strings represent the same document.
 */
bool compareJson(StringPiece json1, StringPiece json2);

/**
 * Like compareJson, but if strNestingDepth > 0 then contained strings that
 * are valid JSON will be compared using compareJsonWithNestedJson(str1,
 * str2, strNestingDepth - 1).
 *
 * \param json1 The first JSON string.
 * \param json2 The second JSON string.
 * \param strNestingDepth How many levels of JSON-in-string to compare.
 * \returns True if both strings represent the same document.
 */
bool compareJsonWithNestedJson(
    StringPiece json1, StringPiece json2, unsigned strNestingDepth);

/**
 * Like compareJson, but with dynamic instances.
 *
 * \param obj1 The first dynamic.
 * \param obj2 The second dynamic.
 * \param strNestingDepth How many levels of JSON-in-string to compare.
 * \returns True if both dynamics represent the same document.
 */
bool compareDynamicWithNestedJson(
    dynamic const& obj1, dynamic const& obj2, unsigned strNestingDepth);

/**
 * Like compareJson, but allows for the given tolerance when comparing
 * numbers.
 *
 * Note that in the dynamic flavor of JSON 64-bit integers are a
 * supported type. If the values to be compared are both integers,
 * tolerance is not applied (it may not be possible to represent them
 * as double without loss of precision).
 *
 * When comparing objects exact key match is required, including if
 * keys are doubles (again a dynamic extension).
 *
 * \param json1 The first JSON string.
 * \param json2 The second JSON string.
 * \param tolerance The allowed numeric difference.
 * \returns True if both strings match within the tolerance.
 */
bool compareJsonWithTolerance(
    StringPiece json1, StringPiece json2, double tolerance);

/**
 * Like compareJsonWithTolerance, but operates directly on the
 * dynamics.
 *
 * \param obj1 The first dynamic.
 * \param obj2 The second dynamic.
 * \param tolerance The allowed numeric difference.
 * \returns True if both dynamics match within the tolerance.
 */
bool compareDynamicWithTolerance(
    const dynamic& obj1, const dynamic& obj2, double tolerance);

} // namespace folly

/**
 * GTest predicate asserting two JSON strings are equal documents.
 *
 * \param json1 The first JSON string.
 * \param json2 The second JSON string.
 */
#define FOLLY_EXPECT_JSON_EQ(json1, json2) \
  EXPECT_PRED2(::folly::compareJson, json1, json2)

/**
 * GTest predicate asserting two JSON strings are equal, comparing one level
 * of nested JSON-in-string.
 *
 * \param json1 The first JSON string.
 * \param json2 The second JSON string.
 */
#define FOLLY_EXPECT_JSON_WITH_NESTED_JSON_EQ(json1, json2) \
  EXPECT_PRED3(::folly::compareJsonWithNestedJson, json1, json2, 1)

/**
 * GTest predicate asserting two JSON strings are equal within a tolerance.
 *
 * \param json1 The first JSON string.
 * \param json2 The second JSON string.
 * \param tolerance The allowed numeric difference.
 */
#define FOLLY_EXPECT_JSON_NEAR(json1, json2, tolerance) \
  EXPECT_PRED3(::folly::compareJsonWithTolerance, json1, json2, tolerance)
