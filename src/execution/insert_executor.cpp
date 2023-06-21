//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <vector>

#include "concurrency/lock_manager.h"
#include "execution/executors/insert_executor.h"
#include "type/type_id.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  auto catalog = exec_ctx_->GetCatalog();
  auto table_oid = plan_->TableOid();
  table_info_ = catalog->GetTable(table_oid);
  table_index_infos_ = catalog->GetTableIndexes(table_info_->name_);
}

void InsertExecutor::Init() {
  child_executor_->Init();
  auto txn = exec_ctx_->GetTransaction();
  if (!txn->IsTableIntentionExclusiveLocked(plan_->TableOid())) {
    std::cout << "Txn " << exec_ctx_->GetTransaction()->GetTransactionId() << " ";
    std::cout << "Insert Init " << std::endl;
    auto success =
        exec_ctx_->GetLockManager()->LockTable(txn, LockManager::LockMode::INTENTION_EXCLUSIVE, plan_->TableOid());
    if (!success) {
      txn->SetState(TransactionState::ABORTED);
      throw ExecutionException("Insert LockTable IX Failed");
    }
  }
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  if (completed_) {
    return false;
  }
  Tuple tmp_tuple{};
  RID tmp_rid{};
  int32_t count = 0;
  // std::cout << "Insert Next" << std::endl;
  auto txn = exec_ctx_->GetTransaction();
  while (child_executor_->Next(&tmp_tuple, &tmp_rid)) {
    // std::cout << "Insert ";
    exec_ctx_->GetLockManager()->LockRow(txn, LockManager::LockMode::EXCLUSIVE, plan_->TableOid(), tmp_rid);
    table_info_->table_->InsertTuple(tmp_tuple, &tmp_rid, exec_ctx_->GetTransaction());
    for (auto index_info : table_index_infos_) {
      auto key_tuple =
          tmp_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->InsertEntry(key_tuple, tmp_rid, exec_ctx_->GetTransaction());
    }
    count++;
  }
  // std::cout << "Insert count " << count << std::endl;
  std::vector<Value> values;
  values.emplace_back(TypeId::INTEGER, count);
  *tuple = Tuple(values, &plan_->OutputSchema());
  completed_ = true;
  return true;
}

}  // namespace bustub
