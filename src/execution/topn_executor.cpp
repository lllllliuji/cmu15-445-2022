#include "execution/executors/topn_executor.h"
#include <algorithm>
#include <queue>
#include "common/rid.h"
#include "storage/table/tuple.h"

namespace bustub {

TopNExecutor::TopNExecutor(ExecutorContext *exec_ctx, const TopNPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_(std::move(child_executor)), n_(plan_->GetN()) {}

void TopNExecutor::Init() {
  child_->Init();
  Tuple tuple;
  RID rid;
  auto order_bys = plan_->GetOrderBy();
  auto func = [&](const Tuple &tuple_a, const Tuple &tuple_b) {
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
  };
  std::priority_queue<Tuple, std::vector<Tuple>, decltype(func)> q(func);
  while (child_->Next(&tuple, &rid)) {
    q.push(tuple);
    while (q.size() > n_) {
      q.pop();
    }
  }
  while (!q.empty()) {
    result_.push_back(q.top());
    q.pop();
  }
}

auto TopNExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (result_.empty()) {
    return false;
  }
  *tuple = result_.back();
  result_.pop_back();
  return true;
}

}  // namespace bustub
