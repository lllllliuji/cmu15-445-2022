//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "type/value_factory.h"

namespace bustub {

NestIndexJoinExecutor::NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_(std::move(child_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2022 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestIndexJoinExecutor::Init() { child_->Init(); }

auto NestIndexJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  Tuple outer_tuple;
  Tuple inner_tuple;
  while (true) {
    if (!match_rids_.empty()) {
      auto match_rid = match_rids_.front();
      match_rids_.pop();
      exec_ctx_->GetCatalog()
          ->GetTable(plan_->GetInnerTableOid())
          ->table_->GetTuple(match_rid, &inner_tuple, exec_ctx_->GetTransaction());
      std::vector<Value> values;
      for (uint32_t i = 0; i < child_->GetOutputSchema().GetColumnCount(); i++) {
        values.push_back(outer_tuple.GetValue(&child_->GetOutputSchema(), i));
      }
      for (uint32_t i = 0; i < plan_->InnerTableSchema().GetColumnCount(); i++) {
        values.push_back(inner_tuple.GetValue(&plan_->InnerTableSchema(), i));
      }
      *tuple = Tuple(values, &plan_->OutputSchema());
      return true;
    }
    if (!child_->Next(&outer_tuple, rid)) {
      return false;
    }
    Value key = plan_->KeyPredicate()->Evaluate(&outer_tuple, child_->GetOutputSchema());

    // std::vector<Value> key_values(plan_->InnerTableSchema().GetColumnCount());
    // key_values[0] = key;
    // std::cout << "here4111" << std::endl;
    Tuple key_tuple(std::vector<Value>{key},
                    exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid())->index_->GetKeySchema());
    std::vector<RID> rids;
    exec_ctx_->GetCatalog()
        ->GetIndex(plan_->GetIndexOid())
        ->index_->ScanKey(key_tuple, &rids, exec_ctx_->GetTransaction());
    if (rids.empty()) {
      if (plan_->GetJoinType() == JoinType::LEFT) {
        std::vector<Value> values;
        for (uint32_t i = 0; i < child_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(outer_tuple.GetValue(&child_->GetOutputSchema(), i));
        }
        for (uint32_t i = 0; i < plan_->InnerTableSchema().GetColumnCount(); i++) {
          values.push_back(ValueFactory::GetNullValueByType(plan_->InnerTableSchema().GetColumn(i).GetType()));
        }
        *tuple = Tuple(values, &plan_->OutputSchema());
        return true;
      }
      continue;
    }
    for (auto rid : rids) {
      match_rids_.push(rid);
    }
  }
}

}  // namespace bustub
