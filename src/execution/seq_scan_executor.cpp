
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include "common/config.h"
#include "common/exception.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan), table_iter_(nullptr, {}, nullptr), table_end_(nullptr, {}, nullptr) {
  auto catalog = exec_ctx_->GetCatalog();
  auto transaction = exec_ctx_->GetTransaction();
  auto table_oid = plan_->GetTableOid();
  auto table_info = catalog->GetTable(table_oid);
  table_iter_ = table_info->table_->Begin(transaction);
  table_end_ = table_info->table_->End();
}

void SeqScanExecutor::Init() {
  auto txn = exec_ctx_->GetTransaction();
  if (txn->GetIsolationLevel() != IsolationLevel::READ_UNCOMMITTED &&
      !txn->IsTableIntentionSharedLocked(plan_->GetTableOid()) &&
      !txn->IsTableIntentionExclusiveLocked(plan_->GetTableOid())) {
    std::cout << "Txn " << exec_ctx_->GetTransaction()->GetTransactionId() << " ";
    std::cout << "Seqscan Init " << std::endl;
    bool success =
        exec_ctx_->GetLockManager()->LockTable(txn, LockManager::LockMode::INTENTION_SHARED, plan_->GetTableOid());
    if (!success) {
      // txn->SetState(TransactionState::ABORTED);
      throw ExecutionException("SeqScan LockTable IS Failed");
    }
    // ever_lock_ = true;
  }
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto txn = exec_ctx_->GetTransaction();
  if (table_iter_ == table_end_) {
    // std::cout << "SeqScan End" << std::endl;
    return false;
  }
  // std::cout << "SeqScan Next" << std::endl;
  *rid = table_iter_->GetRid();
  if (txn->GetIsolationLevel() != IsolationLevel::READ_UNCOMMITTED) {
    // std::cout << "Seqscan ";
    exec_ctx_->GetLockManager()->LockRow(txn, LockManager::LockMode::SHARED, plan_->GetTableOid(), *rid);
  }
  *tuple = Tuple(*table_iter_);
  table_iter_++;
  return true;
}

}  // namespace bustub
