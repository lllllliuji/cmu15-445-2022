//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"
#include "storage/index/b_plus_tree_index.h"

namespace bustub {
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {
  auto catalog = exec_ctx_->GetCatalog();
  auto index_oid = plan_->GetIndexOid();
  auto index_info = catalog->GetIndex(index_oid);
  table_info_ = catalog->GetTable(index_info->table_name_);
  auto tree = dynamic_cast<BPlusTreeIndexForOneIntegerColumn *>(index_info->index_.get());
  index_iter_ = tree->GetBeginIterator();
  index_iter_end_ = tree->GetEndIterator();
}

void IndexScanExecutor::Init() {}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (index_iter_ == index_iter_end_) {
    return false;
  }
  *rid = (*index_iter_).second;
  table_info_->table_->GetTuple(*rid, tuple, exec_ctx_->GetTransaction());
  ++index_iter_;
  return true;
}

}  // namespace bustub
