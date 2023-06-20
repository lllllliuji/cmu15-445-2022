//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"
#include "type/value_factory.h"

namespace bustub {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2022 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  Tuple tuple;
  RID rid;
  while (right_executor_->Next(&tuple, &rid)) {
    right_tuples_.push_back(tuple);
  }
  // std::cout << "right_tuples_ " << right_tuples_.size() << std::endl;
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    if (round_end_) {
      // std::cout << "round_end" << std::endl;
      if (!left_executor_->Next(&left_tuple_, rid)) {
        // std::cout << "here" << std::endl;
        // std::cout << left_tuple_.ToString(&left_executor_->GetOutputSchema()) << std::endl;
        return false;
      }
      match_ = false;
      round_end_ = false;
    }
    // std::cout << left_tuple_.ToString(&left_executor_->GetOutputSchema()) << std::endl;
    if (right_index_ == right_tuples_.size()) {
      round_end_ = true;
      right_index_ = 0;
      if (plan_->join_type_ == JoinType::LEFT && !match_) {
        std::vector<Value> values;
        for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(left_tuple_.GetValue(&left_executor_->GetOutputSchema(), i));
        }
        for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(ValueFactory::GetNullValueByType(right_executor_->GetOutputSchema().GetColumn(i).GetType()));
        }
        *tuple = Tuple(values, &plan_->OutputSchema());
        return true;
      }
    } else {
      // std::cout << "left_tuple_ " << left_tuple_.ToString(&left_executor_->GetOutputSchema()) << std::endl;
      // std::cout << "right_tuple_ " << right_tuples_[right_index_].ToString(&right_executor_->GetOutputSchema())
      // << std::endl;
      auto result = plan_->Predicate().EvaluateJoin(&left_tuple_, left_executor_->GetOutputSchema(),
                                                    &right_tuples_[right_index_], right_executor_->GetOutputSchema());
      if (!result.IsNull() && result.GetAs<bool>()) {
        match_ = true;
        std::vector<Value> values;
        for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(left_tuple_.GetValue(&left_executor_->GetOutputSchema(), i));
        }
        for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(right_tuples_[right_index_].GetValue(&right_executor_->GetOutputSchema(), i));
        }
        *tuple = Tuple(values, &plan_->OutputSchema());
        right_index_++;
        return true;
      }
      right_index_++;
    }
  }
}

}  // namespace bustub
