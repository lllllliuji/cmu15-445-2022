//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/index_iterator.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include "common/config.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
 public:
  // you may define your own constructor based on your member variables
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

  IndexIterator();
  IndexIterator(page_id_t page_id, int pos, BufferPoolManager *buffer_pool_manager);
  ~IndexIterator();  // NOLINT

  auto IsEnd() -> bool;

  auto operator*() -> const MappingType &;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool {
    return page_id_ == itr.page_id_ && pos_ == itr.pos_ && buffer_pool_manager_ == itr.buffer_pool_manager_;
  }

  auto operator!=(const IndexIterator &itr) const -> bool { return !((*this) == itr); }

 private:
  // add your own private member variables here
  Page *page_;
  page_id_t page_id_;
  int pos_;
  BufferPoolManager *buffer_pool_manager_;
};

}  // namespace bustub
