/**
 * index_iterator.cpp
 */
#include <cassert>

#include "buffer/buffer_pool_manager.h"
#include "common/config.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_page.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator() = default;

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(page_id_t page_id, int pos, BufferPoolManager *buffer_pool_manager)
    : page_id_(page_id), pos_(pos), buffer_pool_manager_(buffer_pool_manager) {
  if (page_id_ != INVALID_PAGE_ID) {
    page_ = buffer_pool_manager->FetchPage(page_id_);
  }
}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() { buffer_pool_manager_->UnpinPage(page_id_, false); }

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool {
  auto *leaf_page = reinterpret_cast<LeafPage *>(page_->GetData());
  return static_cast<bool>(pos_ == leaf_page->GetMaxSize() - 1 && leaf_page->GetNextPageId() == INVALID_PAGE_ID);
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> const MappingType & {
  auto *leaf_page = reinterpret_cast<LeafPage *>(page_->GetData());
  return leaf_page->GetMappingType(pos_);
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  auto *leaf_page = reinterpret_cast<LeafPage *>(page_->GetData());
  pos_++;
  if (pos_ == leaf_page->GetSize()) {
    pos_ = 0;
    page_id_ = leaf_page->GetNextPageId();
    buffer_pool_manager_->UnpinPage(page_->GetPageId(), false);
    if (page_id_ == INVALID_PAGE_ID) {
      page_ = nullptr;
    } else {
      page_ = buffer_pool_manager_->FetchPage(page_id_);
    }
  }
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
