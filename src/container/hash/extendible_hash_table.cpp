//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <functional>
#include <list>
#include <utility>

#include "container/hash/extendible_hash_table.h"
#include "storage/page/page.h"

namespace bustub {

template <typename K, typename V>
ExtendibleHashTable<K, V>::ExtendibleHashTable(size_t bucket_size) : bucket_size_(bucket_size) {
  this->global_depth_ = 0;
  this->num_buckets_ = 1;
  dir_.emplace_back(std::make_shared<Bucket>(bucket_size));
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::IndexOf(const K &key) -> size_t {
  int mask = (1 << global_depth_) - 1;
  return std::hash<K>()(key) & mask;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepth() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetGlobalDepthInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepthInternal() const -> int {
  return global_depth_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepth(int dir_index) const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetLocalDepthInternal(dir_index);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepthInternal(int dir_index) const -> int {
  return dir_[dir_index]->GetDepth();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBuckets() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetNumBucketsInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBucketsInternal() const -> int {
  return num_buckets_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Find(const K &key, V &value) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  size_t index = IndexOf(key);
  return dir_.at(index)->Find(key, value);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Remove(const K &key) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  size_t index = IndexOf(key);
  return dir_.at(index)->Remove(key);
}

template <typename K, typename V>
void ExtendibleHashTable<K, V>::Insert(const K &key, const V &value) {
  std::scoped_lock<std::mutex> lock(latch_);
  size_t index = IndexOf(key);
  std::shared_ptr<Bucket> target_bucket = dir_.at(index);
  while (target_bucket->IsFull()) {
    int local_depth = target_bucket->GetDepth();
    if (local_depth == global_depth_) {
      size_t length = dir_.size();
      for (size_t i = 0; i < length; i++) {
        dir_.emplace_back(dir_.at(i));
      }
      global_depth_++;
    }
    int mask = 1 << local_depth;
    auto new_bucket = std::make_shared<Bucket>(bucket_size_, local_depth + 1);
    auto &new_list = new_bucket->GetItems();
    auto &old_list = target_bucket->GetItems();
    auto divide_point = std::partition(old_list.begin(), old_list.end(),
                                       [&](const std::pair<K, V> &item) { return IndexOf(item.first) & mask; });
    new_list.splice(new_list.end(), old_list, old_list.begin(), divide_point);
    for (size_t i = 0; i < dir_.size(); i++) {
      if (dir_[i] == target_bucket) {
        if ((i & mask) != 0U) {
          dir_[i] = new_bucket;
        }
      }
    }
    num_buckets_++;
    target_bucket->IncrementDepth();
    index = IndexOf(key);
    target_bucket = dir_.at(index);
  }
  target_bucket->Insert(key, value);
}

//===--------------------------------------------------------------------===//
// Bucket
//===--------------------------------------------------------------------===//
template <typename K, typename V>
ExtendibleHashTable<K, V>::Bucket::Bucket(size_t array_size, int depth) : size_(array_size), depth_(depth) {}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Find(const K &key, V &value) -> bool {
  auto it = std::find_if(list_.begin(), list_.end(), [&](const std::pair<K, V> &item) { return item.first == key; });
  if (it == list_.end()) {
    return false;
  }
  value = it->second;
  return true;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Remove(const K &key) -> bool {
  auto it = std::find_if(list_.begin(), list_.end(), [&](const std::pair<K, V> &item) { return item.first == key; });
  if (it == list_.end()) {
    return false;
  }
  list_.erase(it);
  return true;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Insert(const K &key, const V &value) -> bool {
  auto it = std::find_if(list_.begin(), list_.end(), [&](const std::pair<K, V> &item) { return item.first == key; });
  if (it != list_.end()) {
    it->second = value;
  } else {
    list_.emplace_back(std::make_pair(key, value));
  }
  return true;
}

template class ExtendibleHashTable<page_id_t, Page *>;
template class ExtendibleHashTable<Page *, std::list<Page *>::iterator>;
template class ExtendibleHashTable<int, int>;
// test purpose
template class ExtendibleHashTable<int, std::string>;
template class ExtendibleHashTable<int, std::list<int>::iterator>;

}  // namespace bustub
