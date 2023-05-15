//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <vector>

#include "common/rid.h"
#include "execution/executors/aggregation_executor.h"
#include "execution/plans/aggregation_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_(std::move(child)),
      aht_(plan_->GetAggregates(), plan_->GetAggregateTypes()),
      aht_iterator_(aht_.Begin()),
      first_(true) {
  // std::cout << this << " aggregator constructor" << std::endl;
}

void AggregationExecutor::Init() {
  child_->Init();
  Tuple tuple;
  RID rid;
  while (child_->Next(&tuple, &rid)) {
    // std::cout << "while next" << std::endl;
    // std::cout << tuple.ToString(&child_->GetOutputSchema()) << std::endl;
    auto key = MakeAggregateKey(&tuple);
    // std::cout << "after make key" << std::endl;
    auto value = MakeAggregateValue(&tuple);
    // std::cout << "after make value" << std::endl;
    aht_.InsertCombine(key, value);
    // std::cout << "after insert combine" << std::endl;
  }
  aht_iterator_ = aht_.Begin();
  // std::cout << plan_->GetAggregates().size() << std::endl;
  // std::cout << plan_->GetGroupBys().size() << std::endl;
  // std::cout << this << " init"
  //           << " child " << child_.get() << std::endl;
  // aht_ = std::make_unique<SimpleAggregationHashTable>(plan_->GetAggregates(), plan_->GetAggregateTypes());
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // std::cout << this << " here next" << std::endl;
  if (aht_iterator_ == aht_.End()) {
    if (first_) {
      // std::cout << this << " first" << std::endl;
      first_ = false;
      if (plan_->GetGroupBys().empty()) {
        *tuple = Tuple(aht_.GenerateInitialAggregateValue().aggregates_, &plan_->OutputSchema());
        return true;
      }
    }
    // std::cout << this << " end" << std::endl;
    return false;
  }
  std::vector<Value> keys = aht_iterator_.Key().group_bys_;
  std::vector<Value> values = aht_iterator_.Val().aggregates_;
  std::vector<Value> result(keys.begin(), keys.end());
  for (const auto &value : values) {
    result.push_back(value);
  }
  //   std::cout << "here" << std::endl;
  //   std::cout << values.front().ToString() << std::endl;
  // std::cout << result.size() << " " << plan_->OutputSchema().GetColumnCount() << std::endl;
  *tuple = Tuple(result, &plan_->OutputSchema());
  // std::cout << tuple->ToString(&plan_->OutputSchema()) << std::endl;
  //   std::cout << "there" << std::endl;
  ++aht_iterator_;
  first_ = false;
  return true;
}

auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_.get(); }

}  // namespace bustub
