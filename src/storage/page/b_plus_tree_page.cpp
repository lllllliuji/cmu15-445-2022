//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/b_plus_tree_page.h"
#include "common/config.h"

namespace bustub {

/*
 * Helper methods to get/set page type
 * Page type enum class is defined in b_plus_tree_page.h
 */
auto BPlusTreePage::IsLeafPage() const -> bool { return this->page_type_ == IndexPageType::LEAF_PAGE; }
auto BPlusTreePage::IsRootPage() const -> bool { return this->parent_page_id_ == INVALID_PAGE_ID; }
void BPlusTreePage::SetPageType(IndexPageType page_type) { this->page_type_ = page_type; }

/*
 * Helper methods to get/set size (number of key/value pairs stored in that
 * page)
 */
auto BPlusTreePage::GetSize() const -> int { return this->size_; }
void BPlusTreePage::SetSize(int size) { this->size_ = size; }
void BPlusTreePage::IncreaseSize(int amount) { this->size_ += amount; }

/*
 * Helper methods to get/set max size (capacity) of the page
 */
auto BPlusTreePage::GetMaxSize() const -> int { return this->max_size_; }
void BPlusTreePage::SetMaxSize(int size) { this->max_size_ = size; }

/*
 * Helper method to get min page size
 * Generally, min page size == max page size / 2
 */
auto BPlusTreePage::GetMinSize() const -> int {
  if (IsRootPage()) {
    return IsLeafPage() ? 1 : 2;
  }
  if (IsLeafPage()) {
    return max_size_ / 2;
  }
  return (max_size_ + 1) / 2;
}

/*
 * Helper methods to get/set parent page id
 */
auto BPlusTreePage::GetParentPageId() const -> page_id_t { return this->parent_page_id_; }
void BPlusTreePage::SetParentPageId(page_id_t parent_page_id) { this->parent_page_id_ = parent_page_id; }

/*
 * Helper methods to get/set self page id
 */
auto BPlusTreePage::GetPageId() const -> page_id_t { return this->page_id_; }
void BPlusTreePage::SetPageId(page_id_t page_id) { this->page_id_ = page_id; }

auto BPlusTreePage::InsertSafe() const -> bool {
  // leaf page split after reach max size, so it's unsafe at maxsize - 1
  if (IsLeafPage()) {
    return GetSize() < GetMaxSize() - 1;
  }
  return GetSize() < GetMaxSize();
}

auto BPlusTreePage::RemoveSafe() const -> bool { return GetSize() > GetMinSize(); }

/*
 * Helper methods to set lsn
 */
void BPlusTreePage::SetLSN(lsn_t lsn) { lsn_ = lsn; }

}  // namespace bustub
