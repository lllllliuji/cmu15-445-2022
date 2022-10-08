//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {
  // std::cout << "Init " << replacer_size_ << " " << k_ << std::endl;
  list_ = std::make_shared<DLinkedList>();
}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  // std::cout << "Evict" << std::endl;
  if (curr_size_ == 0) {
    return false;
  }
  // Print();
  std::shared_ptr<DLinkedNode> target = list_->RemoveTarget();
  if (!target) {
    return false;
  }
  lru_cache_.erase(target->id_);
  *frame_id = target->id_;
  curr_size_--;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  if (static_cast<size_t>(frame_id) > replacer_size_) {
    return;
  }
  std::scoped_lock<std::mutex> lock(latch_);
  std::cout << "RecordAccess " << frame_id << std::endl;
  if (lru_cache_.find(frame_id) == lru_cache_.end()) {
    std::shared_ptr<DLinkedNode> node = std::make_shared<DLinkedNode>(frame_id, k_);
    list_->InsertNode(node);
    lru_cache_[frame_id] = node;
  } else {
    lru_cache_[frame_id]->Update();
    list_->RemoveNode(lru_cache_[frame_id]);
    list_->InsertNode(lru_cache_[frame_id]);
  }
  // Print();
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  // std::cout << "SetEvictable " << frame_id << " " << set_evictable << std::endl; 
  std::scoped_lock<std::mutex> lock(latch_);
  if (lru_cache_.find(frame_id) == lru_cache_.end() || lru_cache_[frame_id]->is_evictable_ == set_evictable) {
    return;
  }
  lru_cache_[frame_id]->is_evictable_ = set_evictable;
  if (set_evictable) {
    curr_size_++;
  } else {
    curr_size_--;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  if (lru_cache_.find(frame_id) == lru_cache_.end()) {
    return;
  }
  if (!lru_cache_[frame_id]->is_evictable_) {
    return;
  }
  // std::cout << "Remove " << frame_id << std::endl;
  list_->RemoveNode(lru_cache_[frame_id]);
  lru_cache_.erase(frame_id);
  curr_size_--;
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  // std::cout << "Size" << std::endl;
  return curr_size_;
}

}  // namespace bustub
