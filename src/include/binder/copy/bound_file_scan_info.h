#pragma once

#include "binder/expression/expression.h"
#include "function/table/bind_data.h"
#include "function/table_functions.h"

namespace kuzu {
namespace binder {

struct BoundFileScanInfo {
    function::TableFunction* copyFunc;
    std::unique_ptr<function::TableFuncBindData> bindData;
    binder::expression_vector columns;

    BoundFileScanInfo(function::TableFunction* copyFunc,
        std::unique_ptr<function::TableFuncBindData> bindData, binder::expression_vector columns)
        : copyFunc{copyFunc}, bindData{std::move(bindData)}, columns{std::move(columns)} {}
    EXPLICIT_COPY_DEFAULT_MOVE(BoundFileScanInfo);

private:
    BoundFileScanInfo(const BoundFileScanInfo& other)
        : copyFunc{other.copyFunc}, bindData{other.bindData->copy()}, columns{other.columns} {}
};

} // namespace binder
} // namespace kuzu
