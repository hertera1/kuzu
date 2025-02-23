#include "binder/expression/property_expression.h"
#include "planner/operator/scan/logical_scan_internal_id.h"
#include "planner/operator/scan/logical_scan_node_property.h"
#include "planner/planner.h"

using namespace kuzu::common;
using namespace kuzu::binder;

namespace kuzu {
namespace planner {

void Planner::appendScanInternalID(
    std::shared_ptr<Expression> internalID, std::vector<table_id_t> tableIDs, LogicalPlan& plan) {
    KU_ASSERT(plan.isEmpty());
    auto scan = make_shared<LogicalScanInternalID>(std::move(internalID), std::move(tableIDs));
    scan->computeFactorizedSchema();
    // update cardinality
    plan.setCardinality(cardinalityEstimator.estimateScanNode(scan.get()));
    plan.setLastOperator(std::move(scan));
}

void Planner::appendScanNodeProperties(std::shared_ptr<Expression> nodeID,
    std::vector<common::table_id_t> tableIDs, const expression_vector& properties,
    LogicalPlan& plan) {
    expression_vector propertiesToScan_;
    for (auto& property : properties) {
        if (((PropertyExpression&)*property).isInternalID()) {
            continue;
        }
        KU_ASSERT(!plan.getSchema()->isExpressionInScope(*property));
        propertiesToScan_.push_back(property);
    }
    if (propertiesToScan_.empty()) {
        return;
    }
    auto scanNodeProperty = make_shared<LogicalScanNodeProperty>(
        std::move(nodeID), std::move(tableIDs), propertiesToScan_, plan.getLastOperator());
    scanNodeProperty->computeFactorizedSchema();
    plan.setLastOperator(std::move(scanNodeProperty));
}

} // namespace planner
} // namespace kuzu
