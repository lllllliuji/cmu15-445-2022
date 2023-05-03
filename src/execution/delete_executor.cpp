//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/delete_executor.h"

namespace bustub {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  auto catalog = exec_ctx_->GetCatalog();
  auto table_oid = plan_->TableOid();
  table_info_ = catalog->GetTable(table_oid);
  table_index_infos_ = catalog->GetTableIndexes(table_info_->name_);
}

void DeleteExecutor::Init() {}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  if (completed_) {
    return false;
  }
  Tuple tmp_tuple{};
  RID tmp_rid{};
  int32_t count = 0;
  while (child_executor_->Next(&tmp_tuple, &tmp_rid)) {
    table_info_->table_->MarkDelete(tmp_rid, exec_ctx_->GetTransaction());
    for (auto index_info : table_index_infos_) {
      auto key_tuple =
          tmp_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->DeleteEntry(key_tuple, tmp_rid, exec_ctx_->GetTransaction());
    }
    count++;
  }
  std::vector<Value> values;
  values.emplace_back(TypeId::INTEGER, count);
  *tuple = Tuple(values, &plan_->OutputSchema());
  completed_ = true;
  return true;
}

}  // namespace bustub
