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

void SeqScanExecutor::Init() {}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (table_iter_ == table_end_) {
    // std::cout << "SeqScan End" << std::endl;
    return false;
  }
  // std::cout << "SeqScan Next" << std::endl;
  *tuple = Tuple(*table_iter_);
  *rid = RID{tuple->GetRid()};
  table_iter_++;
  return true;
}

}  // namespace bustub
