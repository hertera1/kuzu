#pragma once

#include "common/types/ku_string.h"
#include "common/types/types.h"
#include "function/hash/hash_functions.h"
#include "storage/index/hash_index_header.h"
#include "storage/storage_utils.h"

namespace kuzu {
namespace storage {

// NOLINTBEGIN(cert-err58-cpp): This is the best way to get the datatype size because it avoids
// refactoring.
static const uint32_t NUM_BYTES_FOR_INT64_KEY =
    storage::StorageUtils::getDataTypeSize(common::LogicalType{common::LogicalTypeID::INT64});
static const uint32_t NUM_BYTES_FOR_STRING_KEY =
    storage::StorageUtils::getDataTypeSize(common::LogicalType{common::LogicalTypeID::STRING});
// NOLINTEND(cert-err58-cpp)

const uint64_t NUM_HASH_INDEXES_LOG2 = 8;
const uint64_t NUM_HASH_INDEXES = 1 << NUM_HASH_INDEXES_LOG2;

static constexpr common::page_idx_t INDEX_HEADER_ARRAY_HEADER_PAGE_IDX = 0;
static constexpr common::page_idx_t P_SLOTS_HEADER_PAGE_IDX = 1;
static constexpr common::page_idx_t O_SLOTS_HEADER_PAGE_IDX = 2;
static constexpr common::page_idx_t NUM_HEADER_PAGES = 3;
static constexpr uint64_t INDEX_HEADER_IDX_IN_ARRAY = 0;

enum class SlotType : uint8_t { PRIMARY = 0, OVF = 1 };

struct SlotInfo {
    slot_id_t slotId{UINT64_MAX};
    SlotType slotType{SlotType::PRIMARY};
};

class HashIndexUtils {

public:
    inline static bool areStringPrefixAndLenEqual(
        std::string_view keyToLookup, const common::ku_string_t& keyInEntry) {
        auto prefixLen = std::min(
            (uint64_t)keyInEntry.len, static_cast<uint64_t>(common::ku_string_t::PREFIX_LENGTH));
        return keyToLookup.length() == keyInEntry.len &&
               memcmp(keyToLookup.data(), keyInEntry.prefix, prefixLen) == 0;
    }

    template<typename T>
    inline static common::hash_t hash(const T& key) {
        common::hash_t hash;
        function::Hash::operation(key, hash);
        return hash;
    }

    inline static uint8_t getFingerprintForHash(common::hash_t hash) {
        // Last 8 bits before the bits used to calculate the hash index position is the fingerprint
        return (hash >> (64 - NUM_HASH_INDEXES_LOG2 - 8)) & 255;
    }

    inline static slot_id_t getPrimarySlotIdForHash(
        const HashIndexHeader& indexHeader, common::hash_t hash) {
        auto slotId = hash & indexHeader.levelHashMask;
        if (slotId < indexHeader.nextSplitSlotId) {
            slotId = hash & indexHeader.higherLevelHashMask;
        }
        return slotId;
    }

    inline static uint64_t getHashIndexPosition(common::IndexHashable auto key) {
        return (HashIndexUtils::hash(key) >> (64 - NUM_HASH_INDEXES_LOG2)) & (NUM_HASH_INDEXES - 1);
    }

    static inline uint64_t getNumRequiredEntries(
        uint64_t numExistingEntries, uint64_t numNewEntries) {
        return ceil((double)(numExistingEntries + numNewEntries) * common::DEFAULT_HT_LOAD_FACTOR);
    }
};
} // namespace storage
} // namespace kuzu
