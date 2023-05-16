#include "execution/executors/sort_executor.h"
#include "binder/bound_order_by.h"
#include "common/rid.h"
#include "type/type.h"

namespace bustub {

SortExecutor::SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_(std::move(child_executor)) {}

void SortExecutor::Init() {
  // std::cout << "Sort Init" << std::endl;
  child_->Init();
  auto order_bys = plan_->GetOrderBy();
  //   for (const auto& order_by : order_bys) {
  //     if (order_by.first == OrderByType::ASC) {
  //         std::cout << "asc ";
  //     }
  //     if (order_by.first == OrderByType::DESC) {
  //         std::cout << "desc ";
  //     }
  //     if (order_by.first == OrderByType::DEFAULT) {
  //         std::cout << "default ";
  //     }
  //   }
  //   std::cout << std::endl;
  std::vector<Tuple> intemdeiate_tuple;
  Tuple tuple;
  RID rid;
  while (child_->Next(&tuple, &rid)) {
    intemdeiate_tuple.emplace_back(tuple);
  }
  std::sort(intemdeiate_tuple.begin(), intemdeiate_tuple.end(), [&](const auto &tuple_a, const auto &tuple_b) {
    for (const auto &order_by : order_bys) {
      auto order_by_type = order_by.first;
      auto order_expr = order_by.second;
      auto a_value = order_expr->Evaluate(&tuple_a, child_->GetOutputSchema());
      auto b_value = order_expr->Evaluate(&tuple_b, child_->GetOutputSchema());
      if (a_value.CompareEquals(b_value) == CmpBool::CmpTrue) {
        continue;
      }
      CmpBool cmp_result = CmpBool::CmpFalse;
      if (order_by_type == OrderByType::DESC) {
        cmp_result = a_value.CompareGreaterThan(b_value);
      } else {
        cmp_result = a_value.CompareLessThan(b_value);
      }
      if (cmp_result == CmpBool::CmpTrue) {
        return true;
      }
      if (cmp_result == CmpBool::CmpFalse) {
        return false;
      }
    }
    return true;
  });
  for (const auto &tuple : intemdeiate_tuple) {
    results_.push(tuple);
  }
}

auto SortExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (!results_.empty()) {
    *tuple = results_.front();
    results_.pop();
    return true;
  }
  return false;
}

}  // namespace bustub
