#pragma once

#include <bitset>
#include <unordered_map>

#include "binder/expression/rel_expression.h"

namespace kuzu {
namespace binder {

constexpr static uint8_t MAX_NUM_QUERY_VARIABLES = 64;

class QueryGraph;
struct SubqueryGraph;
struct SubqueryGraphHasher;
using subquery_graph_set_t = std::unordered_set<SubqueryGraph, SubqueryGraphHasher>;
template<typename T>
using subquery_graph_V_map_t = std::unordered_map<SubqueryGraph, T, SubqueryGraphHasher>;

// hash on node bitset if subgraph has no rel
struct SubqueryGraphHasher {
    std::size_t operator()(const SubqueryGraph& key) const;
};

struct SubqueryGraph {
    const QueryGraph& queryGraph;
    std::bitset<MAX_NUM_QUERY_VARIABLES> queryNodesSelector;
    std::bitset<MAX_NUM_QUERY_VARIABLES> queryRelsSelector;

    explicit SubqueryGraph(const QueryGraph& queryGraph) : queryGraph{queryGraph} {}

    inline void addQueryNode(uint32_t nodePos) { queryNodesSelector[nodePos] = true; }
    inline void addQueryRel(uint32_t relPos) { queryRelsSelector[relPos] = true; }
    inline void addSubqueryGraph(const SubqueryGraph& other) {
        queryRelsSelector |= other.queryRelsSelector;
        queryNodesSelector |= other.queryNodesSelector;
    }

    inline uint32_t getNumQueryRels() const { return queryRelsSelector.count(); }
    inline uint32_t getTotalNumVariables() const {
        return queryNodesSelector.count() + queryRelsSelector.count();
    }
    inline bool isSingleRel() const {
        return queryRelsSelector.count() == 1 && queryNodesSelector.count() == 0;
    }

    bool containAllVariables(std::unordered_set<std::string>& variables) const;

    std::unordered_set<uint32_t> getNodeNbrPositions() const;
    std::unordered_set<uint32_t> getRelNbrPositions() const;
    subquery_graph_set_t getNbrSubgraphs(uint32_t size) const;
    std::vector<uint32_t> getConnectedNodePos(const SubqueryGraph& nbr) const;

    // E.g. query graph (a)-[e1]->(b) and subgraph (a)-[e1], although (b) is not in subgraph, we
    // return both (a) and (b) regardless of node selector. See needPruneJoin() in
    // join_order_enumerator.cpp for its use case.
    std::unordered_set<uint32_t> getNodePositionsIgnoringNodeSelector() const;

    bool operator==(const SubqueryGraph& other) const {
        return queryRelsSelector == other.queryRelsSelector &&
               queryNodesSelector == other.queryNodesSelector;
    }

private:
    subquery_graph_set_t getBaseNbrSubgraph() const;
    subquery_graph_set_t getNextNbrSubgraphs(const SubqueryGraph& prevNbr) const;
};

// QueryGraph represents a connected pattern specified in MATCH clause.
class QueryGraph {
public:
    QueryGraph() = default;

    std::vector<std::shared_ptr<NodeOrRelExpression>> getAllPatterns() const;

    inline uint32_t getNumQueryNodes() const { return queryNodes.size(); }
    inline bool containsQueryNode(const std::string& queryNodeName) const {
        return queryNodeNameToPosMap.contains(queryNodeName);
    }
    inline std::vector<std::shared_ptr<NodeExpression>> getQueryNodes() const { return queryNodes; }
    inline std::shared_ptr<NodeExpression> getQueryNode(const std::string& queryNodeName) const {
        return queryNodes[getQueryNodePos(queryNodeName)];
    }
    inline std::vector<std::shared_ptr<NodeExpression>> getQueryNodes(
        const std::vector<uint32_t>& nodePoses) const {
        std::vector<std::shared_ptr<NodeExpression>> result;
        result.reserve(nodePoses.size());
        for (auto nodePos : nodePoses) {
            result.push_back(queryNodes[nodePos]);
        }
        return result;
    }
    inline std::shared_ptr<NodeExpression> getQueryNode(uint32_t nodePos) const {
        return queryNodes[nodePos];
    }
    inline uint32_t getQueryNodePos(NodeExpression& node) const {
        return getQueryNodePos(node.getUniqueName());
    }
    inline uint32_t getQueryNodePos(const std::string& queryNodeName) const {
        return queryNodeNameToPosMap.at(queryNodeName);
    }
    void addQueryNode(std::shared_ptr<NodeExpression> queryNode);

    inline uint32_t getNumQueryRels() const { return queryRels.size(); }
    inline bool containsQueryRel(const std::string& queryRelName) const {
        return queryRelNameToPosMap.contains(queryRelName);
    }
    inline std::vector<std::shared_ptr<RelExpression>> getQueryRels() const { return queryRels; }
    inline std::shared_ptr<RelExpression> getQueryRel(const std::string& queryRelName) const {
        return queryRels.at(queryRelNameToPosMap.at(queryRelName));
    }
    inline std::shared_ptr<RelExpression> getQueryRel(uint32_t relPos) const {
        return queryRels[relPos];
    }
    inline uint32_t getQueryRelPos(const std::string& queryRelName) const {
        return queryRelNameToPosMap.at(queryRelName);
    }
    void addQueryRel(std::shared_ptr<RelExpression> queryRel);

    bool canProjectExpression(const std::shared_ptr<Expression>& expression) const;

    bool isConnected(const QueryGraph& other);

    void merge(const QueryGraph& other);

    inline std::unique_ptr<QueryGraph> copy() const { return std::make_unique<QueryGraph>(*this); }

private:
    std::unordered_map<std::string, uint32_t> queryNodeNameToPosMap;
    std::unordered_map<std::string, uint32_t> queryRelNameToPosMap;
    std::vector<std::shared_ptr<NodeExpression>> queryNodes;
    std::vector<std::shared_ptr<RelExpression>> queryRels;
};

// QueryGraphCollection represents a pattern (a set of connected components) specified in MATCH
// clause.
class QueryGraphCollection {
public:
    QueryGraphCollection() = default;
    DELETE_COPY_DEFAULT_MOVE(QueryGraphCollection);

    void addAndMergeQueryGraphIfConnected(QueryGraph queryGraphToAdd);
    void finalize();

    inline uint32_t getNumQueryGraphs() const { return queryGraphs.size(); }
    inline QueryGraph* getQueryGraphUnsafe(uint32_t idx) { return &queryGraphs[idx]; }
    inline const QueryGraph* getQueryGraph(uint32_t idx) const { return &queryGraphs[idx]; }

    std::vector<std::shared_ptr<NodeExpression>> getQueryNodes() const;
    std::vector<std::shared_ptr<RelExpression>> getQueryRels() const;

private:
    std::vector<QueryGraph> queryGraphs;
};

struct BoundGraphPattern {
    QueryGraphCollection queryGraphCollection;
    std::shared_ptr<Expression> where;

    BoundGraphPattern() = default;
    DELETE_COPY_DEFAULT_MOVE(BoundGraphPattern);
};

} // namespace binder
} // namespace kuzu
