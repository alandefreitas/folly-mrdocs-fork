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

#include <unordered_map>
#include <unordered_set>

#include <glog/logging.h>

/// The Folly library.
namespace folly {
/// Implementation details.
namespace observer_detail {

/// Tracks directed edges and rejects any that would introduce a cycle.
template <typename NodeId>
class GraphCycleDetector {
  using NodeSet = std::unordered_set<NodeId>;

 public:
  /**
   * Add new edge. If edge creates a cycle then it's not added and false is
   * returned.
   *
   * \param from The source node of the edge.
   * \param to The destination node of the edge.
   * \returns `true` if the edge was added, `false` if it would create a cycle.
   */
  bool addEdge(NodeId from, NodeId to) {
    // In general case DFS may be expensive here, but in most cases to-node will
    // have no edges, so it should be O(1).
    NodeSet visitedNodes;
    dfs(visitedNodes, to);
    if (visitedNodes.count(from)) {
      return false;
    }

    auto& nodes = edges_[from];
    DCHECK_EQ(nodes.count(to), 0u);
    nodes.insert(to);

    return true;
  }

  /// Removes an existing edge from the graph.
  ///
  /// \param from The source node of the edge.
  /// \param to The destination node of the edge.
  void removeEdge(NodeId from, NodeId to) {
    auto& nodes = edges_[from];
    DCHECK(nodes.count(to));
    nodes.erase(to);
    if (nodes.empty()) {
      edges_.erase(from);
    }
  }

 private:
  void dfs(NodeSet& visitedNodes, NodeId node) {
    // We don't terminate early if cycle is detected, because this is considered
    // an error condition, so not worth optimizing for.
    if (visitedNodes.count(node)) {
      return;
    }

    visitedNodes.insert(node);

    if (!edges_.count(node)) {
      return;
    }

    for (const auto& to : edges_[node]) {
      dfs(visitedNodes, to);
    }
  }

  std::unordered_map<NodeId, NodeSet> edges_;
};
} // namespace observer_detail
} // namespace folly
