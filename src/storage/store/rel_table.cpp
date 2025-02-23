#include "storage/store/rel_table.h"

#include "common/cast.h"
#include "common/exception/message.h"
#include "storage/stats/rels_store_statistics.h"
#include "storage/store/rel_table_data.h"

using namespace kuzu::catalog;
using namespace kuzu::common;
using namespace kuzu::transaction;

namespace kuzu {
namespace storage {

RelDetachDeleteState::RelDetachDeleteState() {
    auto tempSharedState = std::make_shared<DataChunkState>();
    dstNodeIDVector = std::make_unique<ValueVector>(LogicalType{LogicalTypeID::INTERNAL_ID});
    relIDVector = std::make_unique<ValueVector>(LogicalType{LogicalTypeID::INTERNAL_ID});
    dstNodeIDVector->setState(tempSharedState);
    relIDVector->setState(tempSharedState);
}

RelTable::RelTable(BMFileHandle* dataFH, BMFileHandle* metadataFH, RelsStoreStats* relsStoreStats,
    MemoryManager* memoryManager, RelTableCatalogEntry* relTableEntry, WAL* wal,
    bool enableCompression)
    : Table{relTableEntry, relsStoreStats, memoryManager, wal} {
    fwdRelTableData = std::make_unique<RelTableData>(dataFH, metadataFH, bufferManager, wal,
        relTableEntry, relsStoreStats, RelDataDirection::FWD, enableCompression);
    bwdRelTableData = std::make_unique<RelTableData>(dataFH, metadataFH, bufferManager, wal,
        relTableEntry, relsStoreStats, RelDataDirection::BWD, enableCompression);
}

void RelTable::read(Transaction* transaction, TableReadState& readState,
    ValueVector* inNodeIDVector, const std::vector<ValueVector*>& outputVectors) {
    auto& relReadState = ku_dynamic_cast<TableReadState&, RelDataReadState&>(readState);
    scan(transaction, relReadState, inNodeIDVector, outputVectors);
}

void RelTable::insert(Transaction* transaction, ValueVector* srcNodeIDVector,
    ValueVector* dstNodeIDVector, const std::vector<ValueVector*>& propertyVectors) {
    fwdRelTableData->insert(transaction, srcNodeIDVector, dstNodeIDVector, propertyVectors);
    bwdRelTableData->insert(transaction, dstNodeIDVector, srcNodeIDVector, propertyVectors);
    auto relsStats = ku_dynamic_cast<TablesStatistics*, RelsStoreStats*>(tablesStatistics);
    relsStats->updateNumRelsByValue(tableID, 1);
}

void RelTable::update(transaction::Transaction* transaction, column_id_t columnID,
    ValueVector* srcNodeIDVector, ValueVector* dstNodeIDVector, ValueVector* relIDVector,
    ValueVector* propertyVector) {
    fwdRelTableData->update(transaction, columnID, srcNodeIDVector, relIDVector, propertyVector);
    bwdRelTableData->update(transaction, columnID, dstNodeIDVector, relIDVector, propertyVector);
}

void RelTable::delete_(Transaction* transaction, ValueVector* srcNodeIDVector,
    ValueVector* dstNodeIDVector, ValueVector* relIDVector) {
    auto fwdDeleted =
        fwdRelTableData->delete_(transaction, srcNodeIDVector, dstNodeIDVector, relIDVector);
    auto bwdDeleted =
        bwdRelTableData->delete_(transaction, dstNodeIDVector, srcNodeIDVector, relIDVector);
    KU_ASSERT(fwdDeleted == bwdDeleted);
    if (fwdDeleted && bwdDeleted) {
        auto relsStats = ku_dynamic_cast<TablesStatistics*, RelsStoreStats*>(tablesStatistics);
        relsStats->updateNumRelsByValue(tableID, -1);
    }
}

void RelTable::detachDelete(Transaction* transaction, RelDataDirection direction,
    ValueVector* srcNodeIDVector, RelDetachDeleteState* deleteState) {
    KU_ASSERT(srcNodeIDVector->state->selVector->selectedSize == 1);
    auto tableData =
        direction == RelDataDirection::FWD ? fwdRelTableData.get() : bwdRelTableData.get();
    auto reverseTableData =
        direction == RelDataDirection::FWD ? bwdRelTableData.get() : fwdRelTableData.get();
    auto relDataReadState = std::make_unique<RelDataReadState>();
    initializeReadState(transaction, direction, {0}, srcNodeIDVector, relDataReadState.get());
    row_idx_t numRelsDeleted = detachDeleteForCSRRels(transaction, tableData, reverseTableData,
        srcNodeIDVector, relDataReadState.get(), deleteState);
    auto relsStats = ku_dynamic_cast<TablesStatistics*, RelsStoreStats*>(tablesStatistics);
    relsStats->updateNumRelsByValue(tableID, -numRelsDeleted);
}

void RelTable::checkIfNodeHasRels(
    Transaction* transaction, RelDataDirection direction, ValueVector* srcNodeIDVector) {
    KU_ASSERT(srcNodeIDVector->state->isFlat());
    auto nodeIDPos = srcNodeIDVector->state->selVector->selectedPositions[0];
    auto nodeOffset = srcNodeIDVector->getValue<nodeID_t>(nodeIDPos).offset;
    auto res = direction == common::RelDataDirection::FWD ?
                   fwdRelTableData->checkIfNodeHasRels(transaction, nodeOffset) :
                   bwdRelTableData->checkIfNodeHasRels(transaction, nodeOffset);
    if (res) {
        throw RuntimeException(ExceptionMessage::violateDeleteNodeWithConnectedEdgesConstraint(
            tableName, std::to_string(nodeOffset),
            RelDataDirectionUtils::relDirectionToString(direction)));
    }
}

row_idx_t RelTable::detachDeleteForCSRRels(Transaction* transaction, RelTableData* tableData,
    RelTableData* reverseTableData, ValueVector* srcNodeIDVector,
    RelDataReadState* relDataReadState, RelDetachDeleteState* deleteState) {
    row_idx_t numRelsDeleted = 0;
    auto tempState = deleteState->dstNodeIDVector->state.get();
    while (relDataReadState->hasMoreToRead(transaction)) {
        scan(transaction, *relDataReadState, srcNodeIDVector,
            {deleteState->dstNodeIDVector.get(), deleteState->relIDVector.get()});
        auto numRelsScanned = tempState->selVector->selectedSize;
        tempState->selVector->resetSelectorToValuePosBufferWithSize(1);
        for (auto i = 0u; i < numRelsScanned; i++) {
            tempState->selVector->selectedPositions[0] = i;
            auto deleted = tableData->delete_(transaction, srcNodeIDVector,
                deleteState->dstNodeIDVector.get(), deleteState->relIDVector.get());
            auto reverseDeleted =
                reverseTableData->delete_(transaction, deleteState->dstNodeIDVector.get(),
                    srcNodeIDVector, deleteState->relIDVector.get());
            KU_ASSERT(deleted == reverseDeleted);
            numRelsDeleted += (deleted && reverseDeleted);
        }
        tempState->selVector->resetSelectorToUnselectedWithSize(DEFAULT_VECTOR_CAPACITY);
    }
    return numRelsDeleted;
}

void RelTable::scan(Transaction* transaction, RelDataReadState& scanState,
    ValueVector* inNodeIDVector, const std::vector<ValueVector*>& outputVectors) {
    auto tableData = getDirectedTableData(scanState.direction);
    tableData->scan(transaction, scanState, inNodeIDVector, outputVectors);
}

void RelTable::addColumn(
    Transaction* transaction, const Property& property, ValueVector* defaultValueVector) {
    auto relsStats = ku_dynamic_cast<TablesStatistics*, RelsStoreStats*>(tablesStatistics);
    relsStats->setPropertyStatisticsForTable(tableID, property.getPropertyID(),
        PropertyStatistics{!defaultValueVector->hasNoNullsGuarantee()});
    relsStats->addMetadataDAHInfo(tableID, *property.getDataType());
    fwdRelTableData->addColumn(transaction,
        RelDataDirectionUtils::relDirectionToString(RelDataDirection::FWD),
        fwdRelTableData->getAdjColumn()->getMetadataDA(),
        *relsStats->getPropertyMetadataDAHInfo(
            transaction, tableID, fwdRelTableData->getNumColumns(), RelDataDirection::FWD),
        property, defaultValueVector, relsStats);
    bwdRelTableData->addColumn(transaction,
        RelDataDirectionUtils::relDirectionToString(RelDataDirection::BWD),
        bwdRelTableData->getAdjColumn()->getMetadataDA(),
        *relsStats->getPropertyMetadataDAHInfo(
            transaction, tableID, bwdRelTableData->getNumColumns(), RelDataDirection::BWD),
        property, defaultValueVector, relsStats);
    // TODO(Guodong): addColumn is not going through localStorage design for now. So it needs to add
    // tableID into the wal's updated table set separately, as it won't trigger prepareCommit.
    wal->addToUpdatedTables(tableID);
}

void RelTable::prepareCommit(Transaction* transaction, LocalTable* localTable) {
    wal->addToUpdatedTables(tableID);
    fwdRelTableData->prepareLocalTableToCommit(transaction, localTable->getLocalTableData(0));
    bwdRelTableData->prepareLocalTableToCommit(transaction, localTable->getLocalTableData(1));
}

void RelTable::prepareRollback(LocalTableData* localTableData) {
    localTableData->clear();
}

void RelTable::checkpointInMemory() {
    fwdRelTableData->checkpointInMemory();
    bwdRelTableData->checkpointInMemory();
}

void RelTable::rollbackInMemory() {
    fwdRelTableData->rollbackInMemory();
    bwdRelTableData->rollbackInMemory();
}

} // namespace storage
} // namespace kuzu
