#include <memory>
#include <vector>
#include "execution/plans/abstract_plan.h"
#include "execution/plans/limit_plan.h"
#include "execution/plans/sort_plan.h"
#include "execution/plans/topn_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

auto Optimizer::OptimizeSortLimitAsTopN(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement sort + limit -> top N optimizer rule
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSortLimitAsTopN(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));
  if (optimized_plan->GetType() == PlanType::Limit && !optimized_plan->GetChildren().empty() &&
      optimized_plan->GetChildren().size() == 1 && optimized_plan->GetChildren()[0]->GetType() == PlanType::Sort) {
    auto limit_plan_ptr = dynamic_cast<LimitPlanNode *>(optimized_plan.get());
    SchemaRef output_schema = std::make_shared<Schema>(optimized_plan->OutputSchema());
    auto &sort_plan = dynamic_cast<const SortPlanNode &>(*optimized_plan->GetChildren()[0]);
    return std::make_shared<TopNPlanNode>(output_schema, sort_plan.GetChildAt(0), sort_plan.GetOrderBy(),
                                          limit_plan_ptr->GetLimit());
  }
  return optimized_plan;
}

}  // namespace bustub
