#include <cassert>
#include <cstddef>
#include <memory>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "buffer/buffer_pool_manager_instance.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "concurrency/transaction.h"
#include "storage/index/b_plus_tree.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/header_page.h"
#include "storage/page/page.h"
#include "type/value.h"

namespace bustub {
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
                          int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      root_page_id_(INVALID_PAGE_ID),
      buffer_pool_manager_(buffer_pool_manager),
      comparator_(comparator),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {
  // abstract layer to protect root_page_id_
  dummy_page_ = &abstrat_layer_;
  // std::cout << leaf_max_size << " " << internal_max_size << std::endl;
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool { return this->root_page_id_ == INVALID_PAGE_ID; }
/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *transaction) -> bool {
  dummy_page_->RLatch();
  if (IsEmpty()) {
    dummy_page_->RUnlatch();
    return false;
  }
  Page *root_page = buffer_pool_manager_->FetchPage(root_page_id_);
  root_page->RLatch();
  dummy_page_->RUnlatch();
  Page *page = FindLeaf(root_page, key, nullptr);
  auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  auto index = leaf_page->LowerBound(key, comparator_);
  bool success = false;
  if (index < leaf_page->GetSize() && comparator_(key, leaf_page->KeyAt(index)) == 0) {
    success = true;
    // std::cout << "GetValue: " << key << " " << leaf_page->ValueAt(index) << std::endl;
    result->emplace_back(leaf_page->ValueAt(index));
  }
  page->RUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
  assert(transaction == nullptr || transaction->GetPageSet()->empty());
  return success;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeaf(Page *root_page, const KeyType &key, Transaction *transaction) -> Page * {
  Page *page = root_page;
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  while (!b_plus_tree_page->IsLeafPage()) {
    auto internal_page = reinterpret_cast<InternalPage *>(b_plus_tree_page);
    auto index = internal_page->LowerBound(key, comparator_);
    if (index >= internal_page->GetSize() || comparator_(internal_page->KeyAt(index), key) > 0) {
      index--;
    }
    Page *child_page = buffer_pool_manager_->FetchPage(internal_page->ValueAt(index));
    b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    if (transaction != nullptr && b_plus_tree_page->IsLeafPage()) {
      child_page->WLatch();
    } else {
      child_page->RLatch();
    }
    page->RUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
    page = child_page;
  }
  return page;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertInParent(Page *l_page, Page *r_page, Transaction *transaction) {
  auto l_bplus_tree_page = reinterpret_cast<BPlusTreePage *>(l_page->GetData());
  auto r_bplus_tree_page = reinterpret_cast<BPlusTreePage *>(r_page->GetData());
  KeyType key{};
  if (r_bplus_tree_page->IsLeafPage()) {
    auto r_leaf_page = reinterpret_cast<LeafPage *>(r_page->GetData());
    key = r_leaf_page->KeyAt(0);
  } else {
    auto r_internal_page = reinterpret_cast<InternalPage *>(r_page->GetData());
    key = r_internal_page->KeyAt(0);
  }
  if (l_bplus_tree_page->IsRootPage()) {
    page_id_t new_root_page_id;
    auto root_page = buffer_pool_manager_->NewPage(&new_root_page_id);
    auto root_internal_page = reinterpret_cast<InternalPage *>(root_page->GetData());
    root_internal_page->Init(new_root_page_id, INVALID_PAGE_ID, internal_max_size_);
    root_internal_page->Set(0, {}, l_bplus_tree_page->GetPageId());
    root_internal_page->Set(1, key, r_bplus_tree_page->GetPageId());
    root_internal_page->SetSize(2);
    root_page_id_ = new_root_page_id;
    l_bplus_tree_page->SetParentPageId(new_root_page_id);
    r_bplus_tree_page->SetParentPageId(new_root_page_id);
    UpdateRootPageId(new_root_page_id);
    buffer_pool_manager_->UnpinPage(new_root_page_id, true);
    return;
  }
  auto parent_page_id = l_bplus_tree_page->GetParentPageId();
  auto parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
  auto parent_internal_page = reinterpret_cast<InternalPage *>(parent_page->GetData());

  assert(parent_internal_page != nullptr);
  // insert safe
  if (parent_internal_page->InsertSafe()) {
    InsertInternal(parent_page, key, r_page->GetPageId());
    buffer_pool_manager_->UnpinPage(parent_internal_page->GetPageId(), true);
    return;
  }
  auto new_page = InsertInternal(parent_page, key, r_bplus_tree_page->GetPageId());
  InsertInParent(parent_page, new_page, transaction);
  buffer_pool_manager_->UnpinPage(parent_page_id, true);
  buffer_pool_manager_->UnpinPage(new_page->GetPageId(), true);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertInternal(Page *page, KeyType key, page_id_t page_id) -> Page * {
  auto internal_page = reinterpret_cast<InternalPage *>(page->GetData());
  if (internal_page->InsertSafe()) {
    auto index = internal_page->LowerBound(key, comparator_);
    assert(index >= internal_page->GetSize() || comparator_(internal_page->KeyAt(index), key) > 0);
    for (int i = internal_page->GetSize() - 1; i >= index; i--) {
      internal_page->Set(i + 1, internal_page->KeyAt(i), internal_page->ValueAt(i));
    }
    internal_page->Set(index, key, page_id);
    internal_page->IncreaseSize(1);
    return nullptr;
  }
  // this insert may cause page split
  page_id_t new_page_id;
  auto new_page = buffer_pool_manager_->NewPage(&new_page_id);
  auto new_internal_page = reinterpret_cast<InternalPage *>(new_page->GetData());
  new_internal_page->Init(new_page_id, internal_page->GetParentPageId(), internal_max_size_);
  // insert to internal page, the last element is {key, page_id}
  for (int i = 1; i < internal_page->GetSize(); i++) {
    if (comparator_(key, internal_page->KeyAt(i)) < 0) {
      auto tmp_key = internal_page->KeyAt(i);
      auto tmp_page_id = internal_page->ValueAt(i);
      internal_page->Set(i, key, page_id);
      key = tmp_key;
      page_id = tmp_page_id;
    }
  }
  int half = (internal_page->GetSize() + 1) / 2;
  int sz = 0;
  for (int i = half; i < internal_page->GetSize(); i++) {
    new_internal_page->Set(sz++, internal_page->KeyAt(i), internal_page->ValueAt(i));
    auto child_page = buffer_pool_manager_->FetchPage(internal_page->ValueAt(i));
    auto child_bplus_tree = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child_bplus_tree->SetParentPageId(new_internal_page->GetPageId());
    buffer_pool_manager_->UnpinPage(child_page->GetPageId(), true);
  }
  new_internal_page->Set(sz++, key, page_id);
  auto child_page = buffer_pool_manager_->FetchPage(page_id);
  auto child_bplus_tree = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
  child_bplus_tree->SetParentPageId(new_internal_page->GetPageId());
  buffer_pool_manager_->UnpinPage(child_page->GetPageId(), true);
  internal_page->SetSize(half);
  new_internal_page->SetSize(sz);
  return new_page;
}

// caller ensure thant leaf_page's size must less than leaf_max_size_
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertInLeaf(LeafPage *leaf_page, const KeyType &key, const ValueType &value) -> bool {
  auto index = leaf_page->LowerBound(key, comparator_);
  // duplicate key
  if (index < leaf_page->GetSize() && comparator_(key, leaf_page->KeyAt(index)) == 0) {
    return false;
  }
  for (int i = leaf_page->GetSize() - 1; i >= index; i--) {
    leaf_page->Set(i + 1, leaf_page->KeyAt(i), leaf_page->ValueAt(i));
  }
  leaf_page->Set(index, key, value);
  leaf_page->IncreaseSize(1);
  // std::cout << "Insert " << key << " " << value << std::endl;
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitLeaf(LeafPage *leaf_page) -> Page * {
  page_id_t new_page_id;
  auto new_page = buffer_pool_manager_->NewPage(&new_page_id);
  auto new_leaf_page = reinterpret_cast<LeafPage *>(new_page->GetData());
  new_leaf_page->Init(new_page_id, leaf_page->GetParentPageId(), leaf_max_size_);
  int half = leaf_page->GetSize() / 2;
  int sz = 0;
  for (int i = half; i < leaf_page->GetSize(); i++) {
    new_leaf_page->Set(sz++, leaf_page->KeyAt(i), leaf_page->ValueAt(i));
  }
  new_leaf_page->SetNextPageId(leaf_page->GetNextPageId());
  new_leaf_page->SetSize(sz);
  leaf_page->SetNextPageId(new_page_id);
  leaf_page->SetSize(leaf_page->GetSize() - sz);
  return new_page;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value, Transaction *transaction) -> bool {
  dummy_page_->RLatch();
  if (IsEmpty()) {
    dummy_page_->RUnlatch();
    dummy_page_->WLatch();
    // still empty ?
    if (IsEmpty()) {
      page_id_t new_root_page_id;
      auto page = buffer_pool_manager_->NewPage(&new_root_page_id);
      auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
      leaf_page->Init(new_root_page_id, INVALID_PAGE_ID, leaf_max_size_);
      leaf_page->Set(0, key, value);
      leaf_page->SetSize(1);
      root_page_id_ = new_root_page_id;
      UpdateRootPageId(new_root_page_id);
      buffer_pool_manager_->UnpinPage(new_root_page_id, true);
      dummy_page_->WUnlatch();
      return true;
    }
    dummy_page_->WUnlatch();
    return Insert(key, value, transaction);
  }
  Page *root_page = buffer_pool_manager_->FetchPage(root_page_id_);
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(root_page->GetData());
  Page *page;
  if (b_plus_tree_page->IsLeafPage()) {
    page = root_page;
    root_page->WLatch();
    dummy_page_->RUnlatch();
  } else {
    root_page->RLatch();
    dummy_page_->RUnlatch();
    page = FindLeaf(root_page, key, transaction);
  }
  // 1. optimistic
  auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  auto index = leaf_page->LowerBound(key, comparator_);
  // duplicate key
  if (index < leaf_page->GetSize() && comparator_(key, leaf_page->KeyAt(index)) == 0) {
    page->WUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
    return false;
  }
  if (leaf_page->InsertSafe()) {
    bool success = InsertInLeaf(leaf_page, key, value);
    page->WUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetPageId(), success);
    return success;
  }
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
  auto page_set = transaction->GetPageSet();
  while (!page_set->empty()) {
    auto tmp_page = page_set->front();
    page_set->pop_front();
    tmp_page->RUnlatch();
    buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), false);
  }
  assert(transaction->GetPageSet()->empty());
  bool success = PessimisticInsert(key, value, transaction);
  return success;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::PessimisticInsert(const KeyType &key, const ValueType &value, Transaction *transaction) -> bool {
  assert(transaction->GetPageSet()->empty());
  dummy_page_->WLatch();
  if (IsEmpty()) {
    page_id_t new_root_page_id;
    auto page = buffer_pool_manager_->NewPage(&new_root_page_id);
    auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
    leaf_page->Init(new_root_page_id, INVALID_PAGE_ID, leaf_max_size_);
    leaf_page->Set(0, key, value);
    leaf_page->SetSize(1);
    root_page_id_ = new_root_page_id;
    UpdateRootPageId(new_root_page_id);
    buffer_pool_manager_->UnpinPage(new_root_page_id, true);
    dummy_page_->WUnlatch();
    return true;
  }
  Page *page = buffer_pool_manager_->FetchPage(root_page_id_);
  page->WLatch();
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  if (b_plus_tree_page->InsertSafe()) {
    dummy_page_->WUnlatch();
  } else {
    transaction->AddIntoPageSet(dummy_page_);
  }
  while (!b_plus_tree_page->IsLeafPage()) {
    auto internal_page = reinterpret_cast<InternalPage *>(b_plus_tree_page);
    auto index = internal_page->LowerBound(key, comparator_);
    if (index >= internal_page->GetSize() || comparator_(internal_page->KeyAt(index), key) > 0) {
      index--;
    }
    Page *child_page = buffer_pool_manager_->FetchPage(internal_page->ValueAt(index));
    child_page->WLatch();
    b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    // release ancient's write lock if child page is insert safe
    if (b_plus_tree_page->InsertSafe()) {
      auto page_set = transaction->GetPageSet();
      while (!page_set->empty()) {
        auto tmp_page = page_set->front();
        page_set->pop_front();
        tmp_page->WUnlatch();
        buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), false);
      }
    }
    transaction->AddIntoPageSet(page);
    page = child_page;
  }
  auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  bool success = InsertInLeaf(leaf_page, key, value);
  // duplicate key, times fly, may be this update aquire the write lock on leaf from root, leaf may be safe
  if (leaf_page->GetSize() < leaf_page->GetMaxSize()) {
    // buffer_pool_manager_->UnpinPage(page->GetPageId(), true);
    auto page_set = transaction->GetPageSet();
    while (!page_set->empty()) {
      auto tmp_page = page_set->front();
      page_set->pop_front();
      tmp_page->WUnlatch();
      buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), false);
    }
    page->WUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetPageId(), success);
    return success;
  }
  // split and insert parent
  auto new_page = SplitLeaf(leaf_page);
  InsertInParent(page, new_page, transaction);
  auto page_set = transaction->GetPageSet();
  while (!page_set->empty()) {
    auto tmp_page = page_set->front();
    page_set->pop_front();
    tmp_page->WUnlatch();
    buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), true);
  }
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetPageId(), true);
  buffer_pool_manager_->UnpinPage(new_page->GetPageId(), true);
  return true;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *transaction) {
  dummy_page_->RLatch();
  if (IsEmpty()) {
    dummy_page_->RUnlatch();
    return;
  }
  Page *root_page = buffer_pool_manager_->FetchPage(root_page_id_);
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(root_page->GetData());
  Page *page;
  if (b_plus_tree_page->IsLeafPage()) {
    page = root_page;
    root_page->WLatch();
    dummy_page_->RUnlatch();
  } else {
    root_page->RLatch();
    dummy_page_->RUnlatch();
    page = FindLeaf(root_page, key, transaction);
  }
  auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  auto index = leaf_page->LowerBound(key, comparator_);
  // if key doesn't exist
  if (index >= leaf_page->GetSize() || comparator_(key, leaf_page->KeyAt(index)) != 0) {
    page->RUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
    return;
  }
  // 1. optimistic delete
  if (leaf_page->RemoveSafe()) {
    Delete(page, key, transaction);
    page->WUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetPageId(), true);
    return;
  }
  auto page_set = transaction->GetPageSet();
  while (!page_set->empty()) {
    auto tmp_page = page_set->front();
    page_set->pop_front();
    tmp_page->RUnlatch();
    buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), false);
  }
  page->RUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
  // 2. pessimisteic delete
  PessimisticRemove(key, transaction);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::PessimisticRemove(const KeyType &key, Transaction *transaction) {
  assert(transaction->GetPageSet()->empty());
  assert(transaction->GetDeletedPageSet()->empty());
  dummy_page_->WLatch();
  if (IsEmpty()) {
    dummy_page_->WUnlatch();
    return;
  }
  Page *page = buffer_pool_manager_->FetchPage(root_page_id_);
  page->WLatch();
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  if (b_plus_tree_page->RemoveSafe()) {
    dummy_page_->WUnlatch();
  } else {
    transaction->AddIntoPageSet(dummy_page_);
  }
  while (!b_plus_tree_page->IsLeafPage()) {
    auto internal_page = reinterpret_cast<InternalPage *>(b_plus_tree_page);
    auto index = internal_page->LowerBound(key, comparator_);
    if (index >= internal_page->GetSize() || comparator_(internal_page->KeyAt(index), key) > 0) {
      index--;
    }
    Page *child_page = buffer_pool_manager_->FetchPage(internal_page->ValueAt(index));
    child_page->WLatch();
    b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    if (b_plus_tree_page->RemoveSafe()) {
      auto page_set = transaction->GetPageSet();
      while (!page_set->empty()) {
        auto tmp_page = page_set->front();
        page_set->pop_front();
        tmp_page->WUnlatch();
        buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), false);
      }
    }
    transaction->AddIntoPageSet(page);
    page = child_page;
  }
  // recursive delete
  DeleteEntry(page, key, transaction);
  auto page_set = transaction->GetPageSet();
  while (!page_set->empty()) {
    auto tmp_page = page_set->front();
    page_set->pop_front();
    tmp_page->WUnlatch();
    buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), true);
  }
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetPageId(), true);
  // delete pages
  for (auto delete_page_id : *transaction->GetDeletedPageSet()) {
    buffer_pool_manager_->DeletePage(delete_page_id);
  }
  transaction->GetDeletedPageSet()->clear();
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Delete(Page *page, const KeyType &key, Transaction *transaction) {
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  // delete
  if (b_plus_tree_page->IsLeafPage()) {
    auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
    auto index = leaf_page->LowerBound(key, comparator_);
    // key doesn't exist
    if (index >= leaf_page->GetSize() || comparator_(key, leaf_page->KeyAt(index)) != 0) {
      return;
    }
    for (int i = index + 1; i < leaf_page->GetSize(); i++) {
      leaf_page->Set(i - 1, leaf_page->KeyAt(i), leaf_page->ValueAt(i));
    }
    leaf_page->IncreaseSize(-1);
    // std::cout << "Delete " << key << std::endl;
  } else {
    auto internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    auto index = internal_page->LowerBound(key, comparator_);
    // key doesn't exist
    if (comparator_(key, internal_page->KeyAt(index)) != 0) {
      return;
    }
    for (int i = index + 1; i < internal_page->GetSize(); i++) {
      internal_page->Set(i - 1, internal_page->KeyAt(i), internal_page->ValueAt(i));
    }
    internal_page->IncreaseSize(-1);
    // std::cout << "Delete " << key << std::endl;
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::DeleteEntry(Page *page, const KeyType &key, Transaction *transaction) {
  Delete(page, key, transaction);
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  // if the b plus tree now is empty
  if (b_plus_tree_page->IsRootPage() && b_plus_tree_page->GetSize() == 0) {
    root_page_id_ = INVALID_PAGE_ID;
    // UpdateRootPageId(root_page_id_);
    transaction->AddIntoDeletedPageSet(page->GetPageId());
    return;
  }
  if (b_plus_tree_page->GetSize() >= b_plus_tree_page->GetMinSize()) {
    auto page_set = transaction->GetPageSet();
    while (!page_set->empty()) {
      auto tmp_page = page_set->front();
      page_set->pop_front();
      tmp_page->WUnlatch();
      buffer_pool_manager_->UnpinPage(tmp_page->GetPageId(), false);
    }
    return;
  }
  page_id_t page_id = page->GetPageId();
  if (!b_plus_tree_page->IsLeafPage() && b_plus_tree_page->IsRootPage() && b_plus_tree_page->GetSize() == 1) {
    auto internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t new_root_page_id = internal_page->ValueAt(0);
    Page *new_root_page = buffer_pool_manager_->FetchPage(new_root_page_id);
    auto new_root_bplus_tree_page = reinterpret_cast<BPlusTreePage *>(new_root_page->GetData());
    // new_root_page->WLatch();
    new_root_bplus_tree_page->SetParentPageId(INVALID_PAGE_ID);
    // new_root_page->WUnlatch();
    buffer_pool_manager_->UnpinPage(new_root_page_id, true);
    root_page_id_ = new_root_page_id;
    UpdateRootPageId(root_page_id_);
    transaction->AddIntoDeletedPageSet(page_id);
    return;
  }
  // till here, root page must return
  assert(b_plus_tree_page->IsRootPage() == false);
  // buffer_pool_manager_->UnpinPage(page_id, false);
  KeyType divide_key{};
  page_id_t prev_page_id = GetBrotherPageId(page_id, true, divide_key, transaction);
  if (prev_page_id == INVALID_PAGE_ID) {
    prev_page_id = GetBrotherPageId(page_id, false, divide_key, transaction);
    std::swap(page_id, prev_page_id);
  }
  assert(page_id != INVALID_PAGE_ID);
  assert(prev_page_id != INVALID_PAGE_ID);
  MergePage(prev_page_id, page_id, divide_key, transaction);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::MergePage(page_id_t left_page_id, page_id_t right_page_id, const KeyType &divide_key,
                               Transaction *transaction) {
  Page *l_page = buffer_pool_manager_->FetchPage(left_page_id);
  Page *r_page = buffer_pool_manager_->FetchPage(right_page_id);
  auto l_bplus_tree_page = reinterpret_cast<BPlusTreePage *>(l_page->GetData());
  // auto r_bplus_tree_page = reinterpret_cast<BPlusTreePage *>(r_page->GetData());
  if (l_bplus_tree_page->IsLeafPage()) {
    auto l_leaf_page = reinterpret_cast<LeafPage *>(l_page->GetData());
    auto r_leaf_page = reinterpret_cast<LeafPage *>(r_page->GetData());
    // coalesce
    if (l_leaf_page->GetSize() + r_leaf_page->GetSize() < l_leaf_page->GetMaxSize()) {
      int sz = l_leaf_page->GetSize();
      for (int i = 0; i < r_leaf_page->GetSize(); i++) {
        l_leaf_page->Set(sz++, r_leaf_page->KeyAt(i), r_leaf_page->ValueAt(i));
      }
      l_leaf_page->SetSize(sz);
      l_leaf_page->SetNextPageId(r_leaf_page->GetNextPageId());

      transaction->AddIntoDeletedPageSet(right_page_id);
      auto parent_page = buffer_pool_manager_->FetchPage(l_leaf_page->GetParentPageId());
      DeleteEntry(parent_page, divide_key, transaction);
      buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
      buffer_pool_manager_->UnpinPage(left_page_id, true);
      buffer_pool_manager_->UnpinPage(right_page_id, false);
      return;
    }
    // redistribute
    if (l_leaf_page->GetSize() > r_leaf_page->GetSize()) {
      // move right for one place
      for (int i = r_leaf_page->GetSize() - 1; i >= 0; i--) {
        r_leaf_page->Set(i + 1, r_leaf_page->KeyAt(i), r_leaf_page->ValueAt(i));
      }
      r_leaf_page->Set(0, l_leaf_page->KeyAt(l_leaf_page->GetSize() - 1),
                       l_leaf_page->ValueAt(l_leaf_page->GetSize() - 1));
      l_leaf_page->IncreaseSize(-1);
      r_leaf_page->IncreaseSize(1);
    } else {
      l_leaf_page->Set(l_leaf_page->GetSize(), r_leaf_page->KeyAt(0), r_leaf_page->ValueAt(0));
      for (int i = 1; i < r_leaf_page->GetSize(); i++) {
        r_leaf_page->Set(i - 1, r_leaf_page->KeyAt(i), r_leaf_page->ValueAt(i));
      }
      l_leaf_page->IncreaseSize(1);
      r_leaf_page->IncreaseSize(-1);
    }
    page_id_t parent_page_id = l_leaf_page->GetParentPageId();
    auto parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
    auto parent_internal_page = reinterpret_cast<InternalPage *>(parent_page->GetData());
    auto index = parent_internal_page->LowerBound(divide_key, comparator_);
    parent_internal_page->SetKeyAt(index, r_leaf_page->KeyAt(0));
    buffer_pool_manager_->UnpinPage(parent_page_id, true);
    buffer_pool_manager_->UnpinPage(left_page_id, true);
    buffer_pool_manager_->UnpinPage(right_page_id, true);
    return;
  }
  // internal page
  auto l_internal_page = reinterpret_cast<InternalPage *>(l_page->GetData());
  auto r_internal_page = reinterpret_cast<InternalPage *>(r_page->GetData());
  // coalesce
  if (l_internal_page->GetSize() + r_internal_page->GetSize() <= l_internal_page->GetMaxSize()) {
    int sz = l_internal_page->GetSize();
    l_internal_page->Set(sz++, divide_key, r_internal_page->ValueAt(0));
    for (int i = 1; i < r_internal_page->GetSize(); i++) {
      l_internal_page->Set(sz++, r_internal_page->KeyAt(i), r_internal_page->ValueAt(i));
    }
    for (int i = l_internal_page->GetSize(); i < sz; i++) {
      page_id_t r_child_page_id = l_internal_page->ValueAt(i);
      auto r_child_page = buffer_pool_manager_->FetchPage(r_child_page_id);
      auto r_child_b_plus_page = reinterpret_cast<BPlusTreePage *>(r_child_page->GetData());
      r_child_b_plus_page->SetParentPageId(l_internal_page->GetPageId());
      buffer_pool_manager_->UnpinPage(r_child_page_id, true);
    }
    l_internal_page->SetSize(sz);
    transaction->AddIntoDeletedPageSet(right_page_id);
    auto parent_page = buffer_pool_manager_->FetchPage(l_internal_page->GetParentPageId());
    DeleteEntry(parent_page, divide_key, transaction);
    buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
    buffer_pool_manager_->UnpinPage(left_page_id, true);
    buffer_pool_manager_->UnpinPage(right_page_id, false);  // because it's deleted
    return;
  }
  // redistribute
  if (l_internal_page->GetSize() > r_internal_page->GetSize()) {
    r_internal_page->SetKeyAt(0, divide_key);
    for (int i = r_internal_page->GetSize() - 1; i >= 0; i--) {
      r_internal_page->Set(i + 1, r_internal_page->KeyAt(i), r_internal_page->ValueAt(i));
    }
    r_internal_page->Set(0, l_internal_page->KeyAt(l_internal_page->GetSize() - 1),
                         l_internal_page->ValueAt(l_internal_page->GetSize() - 1));
    l_internal_page->IncreaseSize(-1);
    r_internal_page->IncreaseSize(1);
    // update parent page id
    auto child_page = buffer_pool_manager_->FetchPage(r_internal_page->ValueAt(0));
    auto child_bplus_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child_bplus_page->SetParentPageId(r_internal_page->GetPageId());
    buffer_pool_manager_->UnpinPage(child_page->GetPageId(), true);
  } else {
    l_internal_page->Set(l_internal_page->GetSize(), divide_key, r_internal_page->ValueAt(0));
    for (int i = 1; i < r_internal_page->GetSize(); i++) {
      r_internal_page->Set(i - 1, r_internal_page->KeyAt(i), r_internal_page->ValueAt(i));
    }
    l_internal_page->IncreaseSize(1);
    r_internal_page->IncreaseSize(-1);
    auto child_page = buffer_pool_manager_->FetchPage(l_internal_page->ValueAt(l_internal_page->GetSize() - 1));
    auto child_bplus_tree = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child_bplus_tree->SetParentPageId(l_internal_page->GetPageId());
    buffer_pool_manager_->UnpinPage(child_page->GetPageId(), true);
  }
  page_id_t parent_page_id = l_internal_page->GetParentPageId();
  auto parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
  auto parent_internal_page = reinterpret_cast<InternalPage *>(parent_page->GetData());
  auto index = parent_internal_page->LowerBound(divide_key, comparator_);
  parent_internal_page->SetKeyAt(index, r_internal_page->KeyAt(0));
  buffer_pool_manager_->UnpinPage(parent_page_id, true);
  buffer_pool_manager_->UnpinPage(left_page_id, true);
  buffer_pool_manager_->UnpinPage(right_page_id, true);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetBrotherPageId(page_id_t page_id, bool prev, KeyType &divide_key, Transaction *transaction)
    -> page_id_t {
  Page *page = buffer_pool_manager_->FetchPage(page_id);
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  page_id_t parent_page_id = b_plus_tree_page->GetParentPageId();
  Page *parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
  auto parent_page_internal = reinterpret_cast<InternalPage *>(parent_page->GetData());
  int index = INVALID_PAGE_ID;
  bool found = false;
  for (int i = 0; i < parent_page_internal->GetSize(); i++) {
    if (parent_page_internal->ValueAt(i) == page_id) {
      index = i;
      found = true;
      break;
    }
  }
  assert(found == true);
  page_id_t brother_page_id = INVALID_PAGE_ID;
  if (prev && index != 0) {
    divide_key = parent_page_internal->KeyAt(index);
    brother_page_id = parent_page_internal->ValueAt(index - 1);
  }
  if (!prev && index != parent_page_internal->GetSize() - 1) {
    divide_key = parent_page_internal->KeyAt(index + 1);
    brother_page_id = parent_page_internal->ValueAt(index + 1);
  }
  buffer_pool_manager_->UnpinPage(page_id, false);
  buffer_pool_manager_->UnpinPage(parent_page_id, false);
  // add to page set
  if (brother_page_id != INVALID_PAGE_ID) {
    auto brother_page = buffer_pool_manager_->FetchPage(brother_page_id);
    transaction->AddIntoPageSet(brother_page);
    brother_page->WLatch();
  }
  return brother_page_id;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leaftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  dummy_page_->RLatch();
  if (IsEmpty()) {
    dummy_page_->RUnlatch();
    return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, buffer_pool_manager_);
  }
  Page *root_page = buffer_pool_manager_->FetchPage(root_page_id_);
  root_page->RLatch();
  dummy_page_->RUnlatch();
  auto page = root_page;
  // auto page = buffer_pool_manager_->FetchPage(root_page_id_);
  auto b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  while (b_plus_tree_page != nullptr && !b_plus_tree_page->IsLeafPage()) {
    auto internal_page = reinterpret_cast<InternalPage *>(b_plus_tree_page);
    Page *child_page = buffer_pool_manager_->FetchPage(internal_page->ValueAt(0));
    child_page->RLatch();
    page->RUnlatch();
    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
    page = child_page;
    b_plus_tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  }
  auto page_id = page->GetPageId();
  page->RUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
  return INDEXITERATOR_TYPE(page_id, 0, buffer_pool_manager_);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  dummy_page_->RLatch();
  if (IsEmpty()) {
    dummy_page_->RUnlatch();
    return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, buffer_pool_manager_);
  }
  Page *root_page = buffer_pool_manager_->FetchPage(root_page_id_);
  root_page->RLatch();
  dummy_page_->RUnlatch();
  Page *page = FindLeaf(root_page, key, nullptr);
  auto leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  int pos = leaf_page->LowerBound(key, comparator_);
  page_id_t page_id = leaf_page->GetPageId();
  page->RUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
  return INDEXITERATOR_TYPE(page_id, pos, buffer_pool_manager_);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE {
  return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, buffer_pool_manager_);
}

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t { return root_page_id_; }

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/
/*
 * Update/Insert root page id in header page(where page_id = 0, header_page is
 * defined under include/page/header_page.h)
 * Call this method everytime root page id is changed.
 * @parameter: insert_record      defualt value is false. When set to true,
 * insert a record <index_name, root_page_id> into header page instead of
 * updating it.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateRootPageId(int insert_record) {
  auto *header_page = static_cast<HeaderPage *>(buffer_pool_manager_->FetchPage(HEADER_PAGE_ID));
  if (insert_record != 0) {
    // create a new record<index_name + root_page_id> in header_page
    header_page->InsertRecord(index_name_, root_page_id_);
  } else {
    // update root_page_id in header_page
    header_page->UpdateRecord(index_name_, root_page_id_);
  }
  buffer_pool_manager_->UnpinPage(HEADER_PAGE_ID, true);
}

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string &file_name, Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;

    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, transaction);
  }
}
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string &file_name, Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, transaction);
  }
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Draw(BufferPoolManager *bpm, const std::string &outf) {
  if (IsEmpty()) {
    LOG_WARN("Draw an empty tree");
    return;
  }
  std::ofstream out(outf);
  out << "digraph G {" << std::endl;
  ToGraph(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(root_page_id_)->GetData()), bpm, out);
  out << "}" << std::endl;
  out.flush();
  out.close();
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Print(BufferPoolManager *bpm) {
  if (IsEmpty()) {
    LOG_WARN("Print an empty tree");
    return;
  }
  ToString(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(root_page_id_)->GetData()), bpm);
}

/**
 * This method is used for debug only, You don't need to modify
 * @tparam KeyType
 * @tparam ValueType
 * @tparam KeyComparator
 * @param page
 * @param bpm
 * @param out
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToGraph(BPlusTreePage *page, BufferPoolManager *bpm, std::ofstream &out) const {
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    // Print node name
    out << leaf_prefix << leaf->GetPageId();
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">P=" << leaf->GetPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">"
        << "max_size=" << leaf->GetMaxSize() << ",min_size=" << leaf->GetMinSize() << ",size=" << leaf->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf->GetSize(); i++) {
      out << "<TD>" << leaf->KeyAt(i) << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
      out << leaf_prefix << leaf->GetPageId() << " -> " << leaf_prefix << leaf->GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << leaf->GetPageId() << " " << leaf_prefix << leaf->GetNextPageId() << "};\n";
    }

    // Print parent links if there is a parent
    if (leaf->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << leaf->GetParentPageId() << ":p" << leaf->GetPageId() << " -> " << leaf_prefix
          << leaf->GetPageId() << ";\n";
    }
  } else {
    auto *inner = reinterpret_cast<InternalPage *>(page);
    // Print node name
    out << internal_prefix << inner->GetPageId();
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">P=" << inner->GetPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">"
        << "max_size=" << inner->GetMaxSize() << ",min_size=" << inner->GetMinSize() << ",size=" << inner->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < inner->GetSize(); i++) {
      out << "<TD PORT=\"p" << inner->ValueAt(i) << "\">";
      if (i > 0) {
        out << inner->KeyAt(i);
      } else {
        out << " ";
      }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Parent link
    if (inner->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << inner->GetParentPageId() << ":p" << inner->GetPageId() << " -> " << internal_prefix
          << inner->GetPageId() << ";\n";
    }
    // Print leaves
    for (int i = 0; i < inner->GetSize(); i++) {
      auto child_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i))->GetData());
      ToGraph(child_page, bpm, out);
      if (i > 0) {
        auto sibling_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i - 1))->GetData());
        if (!sibling_page->IsLeafPage() && !child_page->IsLeafPage()) {
          out << "{rank=same " << internal_prefix << sibling_page->GetPageId() << " " << internal_prefix
              << child_page->GetPageId() << "};\n";
        }
        bpm->UnpinPage(sibling_page->GetPageId(), false);
      }
    }
  }
  bpm->UnpinPage(page->GetPageId(), false);
}

/**
 * This function is for debug only, you don't need to modify
 * @tparam KeyType
 * @tparam ValueType
 * @tparam KeyComparator
 * @param page
 * @param bpm
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToString(BPlusTreePage *page, BufferPoolManager *bpm) const {
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    std::cout << "Leaf Page: " << leaf->GetPageId() << " parent: " << leaf->GetParentPageId()
              << " next: " << leaf->GetNextPageId() << std::endl;
    for (int i = 0; i < leaf->GetSize(); i++) {
      std::cout << leaf->KeyAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
  } else {
    auto *internal = reinterpret_cast<InternalPage *>(page);
    std::cout << "Internal Page: " << internal->GetPageId() << " parent: " << internal->GetParentPageId() << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      std::cout << internal->KeyAt(i) << ": " << internal->ValueAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      ToString(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(internal->ValueAt(i))->GetData()), bpm);
    }
  }
  bpm->UnpinPage(page->GetPageId(), false);
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
