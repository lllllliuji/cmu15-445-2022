//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lock_manager.cpp
//
// Identification: src/concurrency/lock_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/lock_manager.h"
#include <algorithm>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/config.h"
#include "common/rid.h"
#include "concurrency/transaction.h"
#include "concurrency/transaction_manager.h"

namespace bustub {
auto LockManager::GetTableQueue(table_oid_t oid) -> std::shared_ptr<LockRequestQueue> {
  std::scoped_lock<std::mutex> lk(table_lock_map_latch_);
  if (table_lock_map_.find(oid) == table_lock_map_.end()) {
    table_lock_map_[oid] = std::make_shared<LockRequestQueue>();
  }
  return table_lock_map_[oid];
}

auto LockManager::GetRowQueue(RID rid) -> std::shared_ptr<LockRequestQueue> {
  std::scoped_lock<std::mutex> lk(row_lock_map_latch_);
  if (row_lock_map_.find(rid) == row_lock_map_.end()) {
    row_lock_map_[rid] = std::make_shared<LockRequestQueue>();
  }
  return row_lock_map_[rid];
}

auto LockManager::HoldLock(Transaction *txn, table_oid_t oid, LockMode &lock_mode) -> bool {
  bool hold_lock = false;
  if (txn->IsTableIntentionSharedLocked(oid)) {
    lock_mode = LockMode::INTENTION_SHARED;
    hold_lock = true;
  } else if (txn->IsTableIntentionExclusiveLocked(oid)) {
    lock_mode = LockMode::INTENTION_EXCLUSIVE;
    hold_lock = true;
  } else if (txn->IsTableSharedLocked(oid)) {
    lock_mode = LockMode::SHARED;
    hold_lock = true;
  } else if (txn->IsTableSharedIntentionExclusiveLocked(oid)) {
    lock_mode = LockMode::SHARED_INTENTION_EXCLUSIVE;
    hold_lock = true;
  } else if (txn->IsTableExclusiveLocked(oid)) {
    lock_mode = LockMode::EXCLUSIVE;
    hold_lock = true;
  }
  return hold_lock;
}

auto LockManager::HoldLock(Transaction *txn, table_oid_t oid, RID rid, LockMode &lock_mode) -> bool {
  bool hold_lock = false;
  if (txn->IsRowSharedLocked(oid, rid)) {
    lock_mode = LockMode::SHARED;
    hold_lock = true;
  } else if (txn->IsRowExclusiveLocked(oid, rid)) {
    lock_mode = LockMode::EXCLUSIVE;
    hold_lock = true;
  }
  return hold_lock;
}

auto LockManager::IsCompatible(LockMode a, LockMode b) -> bool {
  switch (a) {
    case LockMode::INTENTION_SHARED:
      if (b == LockMode::EXCLUSIVE) {
        return false;
      }
      break;
    case LockMode::INTENTION_EXCLUSIVE:
      if (b == LockMode::SHARED || b == LockMode::SHARED_INTENTION_EXCLUSIVE || b == LockMode::EXCLUSIVE) {
        return false;
      }
      break;
    case LockMode::SHARED:
      if (b == LockMode::INTENTION_EXCLUSIVE || b == LockMode::SHARED_INTENTION_EXCLUSIVE || b == LockMode::EXCLUSIVE) {
        return false;
      }
      break;
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      if (b != LockMode::INTENTION_SHARED) {
        return false;
      }
      break;
    case LockMode::EXCLUSIVE:
      return false;
  }
  return true;
}

auto LockManager::IsUpgradeable(LockMode a, LockMode b) -> bool {
  if (a == b) {
    return true;
  }
  // printf("%d %d\n", a, b);
  switch (a) {
    case LockMode::INTENTION_SHARED:
      return true;
      break;
    case LockMode::INTENTION_EXCLUSIVE:
    case LockMode::SHARED:
      if (b != LockMode::EXCLUSIVE && b != LockMode::SHARED_INTENTION_EXCLUSIVE) {
        return false;
      }
      break;
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      if (b != LockMode::EXCLUSIVE) {
        return false;
      }
      break;
    case LockMode::EXCLUSIVE:
      std::cout << "X" << std::endl;
      return false;
  }
  return true;
}

auto LockManager::NormalLockRequestCheck(Transaction *txn, LockMode lock_mode, AbortReason &abort_reason) -> bool {
  switch (txn->GetIsolationLevel()) {
    case IsolationLevel::READ_UNCOMMITTED:
      // never allowed
      if (lock_mode == LockMode::SHARED || lock_mode == LockMode::INTENTION_SHARED ||
          lock_mode == LockMode::SHARED_INTENTION_EXCLUSIVE) {
        // txn->SetState(TransactionState::ABORTED);
        // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_SHARED_ON_READ_UNCOMMITTED);
        // std::cout << "TransactionId " << txn->GetTransactionId();
        // std::cout << "LOCK_SHARED_ON_READ_UNCOMMITTED" << std::endl;
        abort_reason = AbortReason::LOCK_SHARED_ON_READ_UNCOMMITTED;
        return false;
      }
      // The transaction is required to take only IX, X locks.
      assert(lock_mode == LockMode::INTENTION_EXCLUSIVE || lock_mode == LockMode::EXCLUSIVE);
      // x, ix is not allowed in shrinking
      if (txn->GetState() == TransactionState::SHRINKING) {
        // txn->SetState(TransactionState::ABORTED);
        // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
        // std::cout << "TransactionId " << txn->GetTransactionId();
        // std::cout << "LOCK_ON_SHRINKING" << std::endl;
        abort_reason = AbortReason::LOCK_ON_SHRINKING;
        return false;
      }
      break;
    case IsolationLevel::READ_COMMITTED:
      // Only IS, S locks are allowed in the SHRINKING state
      if (txn->GetState() == TransactionState::SHRINKING && lock_mode != LockMode::INTENTION_SHARED &&
          lock_mode != LockMode::SHARED) {
        // txn->SetState(TransactionState::ABORTED);
        // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
        // std::cout << "TransactionId " << txn->GetTransactionId();
        // std::cout << "LOCK_ON_SHRINKING" << std::endl;
        abort_reason = AbortReason::LOCK_ON_SHRINKING;
        return false;
      }
      break;
    case IsolationLevel::REPEATABLE_READ:
      // No locks are allowed in the SHRINKING state
      if (txn->GetState() == TransactionState::SHRINKING) {
        // txn->SetState(TransactionState::ABORTED);
        // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
        // std::cout << "TransactionId " << txn->GetTransactionId();
        // std::cout << "LOCK_ON_SHRINKING" << std::endl;
        abort_reason = AbortReason::LOCK_ON_SHRINKING;
        return false;
      }
      break;
  }
  return true;
}

auto LockManager::NormalUnlockRequestCheck(Transaction *txn, table_oid_t oid, LockMode &unlock_mode,
                                           AbortReason &abort_reason) -> bool {
  // doesn't hold any lock
  if (!txn->IsTableIntentionSharedLocked(oid) && !txn->IsTableIntentionExclusiveLocked(oid) &&
      !txn->IsTableSharedLocked(oid) && !txn->IsTableSharedIntentionExclusiveLocked(oid) &&
      !txn->IsTableExclusiveLocked(oid)) {
    abort_reason = AbortReason::ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD;
    // std::cout << "TransactionId " << txn->GetTransactionId();
    // std::cout << "ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD" << std::endl;
    return false;
    // txn->SetState(TransactionState::ABORTED);
    // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD);
  }
  // table unlock should after all row unlock
  if (!txn->GetSharedRowLockSet()->operator[](oid).empty() || !txn->GetExclusiveRowLockSet()->operator[](oid).empty()) {
    // std::cout << txn->GetSharedRowLockSet()->empty() << " " << txn->GetExclusiveRowLockSet()->empty() << std::endl;
    abort_reason = AbortReason::TABLE_UNLOCKED_BEFORE_UNLOCKING_ROWS;
    // std::cout << "TransactionId " << txn->GetTransactionId();
    // std::cout << "TABLE_UNLOCKED_BEFORE_UNLOCKING_ROWS" << std::endl;
    return false;
    // txn->SetState(TransactionState::ABORTED);
    // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::TABLE_UNLOCKED_BEFORE_UNLOCKING_ROWS);
  }
  if (txn->IsTableIntentionSharedLocked(oid)) {
    unlock_mode = LockMode::INTENTION_SHARED;
  } else if (txn->IsTableIntentionExclusiveLocked(oid)) {
    unlock_mode = LockMode::INTENTION_EXCLUSIVE;
  } else if (txn->IsTableSharedLocked(oid)) {
    unlock_mode = LockMode::SHARED;
  } else if (txn->IsTableSharedIntentionExclusiveLocked(oid)) {
    unlock_mode = LockMode::SHARED_INTENTION_EXCLUSIVE;
  } else if (txn->IsTableExclusiveLocked(oid)) {
    unlock_mode = LockMode::EXCLUSIVE;
  }
  return true;
}

auto LockManager::RowLockRequestCheck(Transaction *txn, LockMode lock_mode, table_oid_t oid, AbortReason &abort_reason)
    -> bool {
  switch (lock_mode) {
    case LockMode::INTENTION_SHARED:
    case LockMode::INTENTION_EXCLUSIVE:
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      // Row locking should not support Intention locks
      abort_reason = AbortReason::ATTEMPTED_INTENTION_LOCK_ON_ROW;
      // std::cout << "TransactionId " << txn->GetTransactionId();
      // std::cout << "ATTEMPTED_INTENTION_LOCK_ON_ROW" << std::endl;
      return false;
      // txn->SetState(TransactionState::ABORTED);
      // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::ATTEMPTED_INTENTION_LOCK_ON_ROW);
      break;
    case LockMode::SHARED:
      // any lock on table is ok
      if (!txn->IsTableIntentionSharedLocked(oid) && !txn->IsTableIntentionExclusiveLocked(oid) &&
          !txn->IsTableSharedLocked(oid) && !txn->IsTableSharedIntentionExclusiveLocked(oid) &&
          !txn->IsTableExclusiveLocked(oid)) {
        abort_reason = AbortReason::TABLE_LOCK_NOT_PRESENT;
        // std::cout << "TransactionId " << txn->GetTransactionId();
        // std::cout << "TABLE_LOCK_NOT_PRESENT" << std::endl;
        return false;
        // txn->SetState(TransactionState::ABORTED);
        // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::TABLE_LOCK_NOT_PRESENT);
      }
      break;
    case LockMode::EXCLUSIVE:
      // must hold either X, IX, or SIX on the table
      if (!txn->IsTableExclusiveLocked(oid) && !txn->IsTableIntentionExclusiveLocked(oid) &&
          !txn->IsTableSharedIntentionExclusiveLocked(oid)) {
        abort_reason = AbortReason::TABLE_LOCK_NOT_PRESENT;
        // std::cout << "TransactionId " << txn->GetTransactionId();
        // std::cout << "TABLE_LOCK_NOT_PRESENT" << std::endl;
        return false;
        // txn->SetState(TransactionState::ABORTED);
        // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::TABLE_LOCK_NOT_PRESENT);
      }
      break;
  }
  return true;
}

auto LockManager::RowUnLockRequestCheck(Transaction *txn, table_oid_t oid, RID rid, LockMode &unlock_mode,
                                        AbortReason &abort_reason) -> bool {
  // doesn't hold any lock
  if (!txn->IsRowSharedLocked(oid, rid) && !txn->IsRowExclusiveLocked(oid, rid)) {
    abort_reason = AbortReason::ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD;
    // std::cout << "TransactionId " << txn->GetTransactionId();
    // std::cout << "ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD" << std::endl;
    return false;
    // txn->SetState(TransactionState::ABORTED);
    // throw TransactionAbortException(txn->GetTransactionId(), AbortReason::ATTEMPTED_UNLOCK_BUT_NO_LOCK_HELD);
  }
  if (txn->IsRowSharedLocked(oid, rid)) {
    unlock_mode = LockMode::SHARED;
  } else if (txn->IsRowExclusiveLocked(oid, rid)) {
    unlock_mode = LockMode::EXCLUSIVE;
  }
  return true;
}

void LockManager::UpdateTxnStateOnUnlock(Transaction *txn, LockMode unlock_mode) {
  switch (txn->GetIsolationLevel()) {
    case IsolationLevel::READ_UNCOMMITTED:
    case IsolationLevel::READ_COMMITTED:
      if (unlock_mode == LockMode::EXCLUSIVE) {
        txn->SetState(TransactionState::SHRINKING);
      }
      break;
    case IsolationLevel::REPEATABLE_READ:
      if (unlock_mode == LockMode::SHARED || unlock_mode == LockMode::EXCLUSIVE) {
        txn->SetState(TransactionState::SHRINKING);
      }
      break;
  }
}

auto LockManager::LockTable(Transaction *txn, LockMode lock_mode, const table_oid_t &oid) -> bool {
  std::cout << "LockTable: TransactionId " << txn->GetTransactionId();
  switch (lock_mode) {
    case LockMode::INTENTION_SHARED:
      std::cout << " IS";
      break;
    case LockMode::INTENTION_EXCLUSIVE:
      std::cout << " IX";
      break;
    case LockMode::SHARED:
      std::cout << " S";
      break;
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      std::cout << " SIX";
      break;
    case LockMode::EXCLUSIVE:
      std::cout << " X";
      break;
  }
  std::cout << " TableId " << oid << std::endl;
  txn->LockTxn();
  // 1. check correct lock mode depending on isolation level
  AbortReason abort_reason{};
  LockMode cur_mode{};
  if (!NormalLockRequestCheck(txn, lock_mode, abort_reason)) {
    txn->SetState(TransactionState::ABORTED);
    txn->UnlockTxn();
    throw TransactionAbortException(txn->GetTransactionId(), abort_reason);
  }
  auto table_queue = GetTableQueue(oid);
  std::unique_lock<std::mutex> lk(table_queue->latch_);
  // std::cout << "get lock" << std::endl;
  // std::cout << "txn " << txn->GetTransactionId() << std::endl;
  // 2. check if it's an upgrade request
  if (!HoldLock(txn, oid, cur_mode)) {
    // printf("%d\n", cur_mode);
    table_queue->request_queue_.push_back(std::make_shared<LockRequest>(txn->GetTransactionId(), lock_mode, oid));
  } else {
    // 2.1 same as current lock_mode
    // printf("%d %d\n", cur_mode, lock_mode);
    if (cur_mode == lock_mode) {
      txn->UnlockTxn();
      return true;
    }
    // 2.2 current lock_mode doesn't allowed to upgrade to lock_mode
    if (!IsUpgradeable(cur_mode, lock_mode)) {
      txn->SetState(TransactionState::ABORTED);
      txn->UnlockTxn();
      // std::cout << "TransactionId " << txn->GetTransactionId();
      // std::cout << "INCOMPATIBLE_UPGRADE" << std::endl;
      throw TransactionAbortException(txn->GetTransactionId(), AbortReason::INCOMPATIBLE_UPGRADE);
    }
    // 2.3 another lock upgrading is on the way
    if (table_queue->upgrading_ != INVALID_TXN_ID) {
      txn->SetState(TransactionState::ABORTED);
      txn->UnlockTxn();
      // std::cout << "TransactionId " << txn->GetTransactionId();
      // std::cout << "UPGRADE_CONFLICT" << std::endl;
      throw TransactionAbortException(txn->GetTransactionId(), AbortReason::UPGRADE_CONFLICT);
    }
    // 2.4 good, give up current lock
    table_queue->upgrading_ = txn->GetTransactionId();
    table_queue->request_queue_.remove_if([&](const std::shared_ptr<LockRequest> &lock_request) {
      return lock_request->txn_id_ == txn->GetTransactionId() && lock_request->oid_ == oid;
    });
    switch (cur_mode) {
      case LockMode::INTENTION_SHARED:
        txn->GetIntentionSharedTableLockSet()->erase(oid);
        break;
      case LockMode::INTENTION_EXCLUSIVE:
        txn->GetIntentionExclusiveTableLockSet()->erase(oid);
        break;
      case LockMode::SHARED:
        txn->GetSharedTableLockSet()->erase(oid);
        break;
      case LockMode::SHARED_INTENTION_EXCLUSIVE:
        txn->GetSharedIntentionExclusiveTableLockSet()->erase(oid);
        break;
      case LockMode::EXCLUSIVE:
        txn->GetExclusiveTableLockSet()->erase(oid);
        break;
    }
    // 2.5 insert to front
    auto it = std::find_if(table_queue->request_queue_.begin(), table_queue->request_queue_.end(),
                           [&](const std::shared_ptr<LockRequest> &lock_request) { return !lock_request->granted_; });
    table_queue->request_queue_.insert(it, std::make_shared<LockRequest>(txn->GetTransactionId(), lock_mode, oid));
  }
  txn->UnlockTxn();
  bool abort = false;
  table_queue->cv_.wait(lk, [&]() -> bool {
    txn->LockTxn();
    // std::cout << "in wait" << std::endl;
    if (txn->GetState() == TransactionState::ABORTED) {
      abort = true;
      txn->UnlockTxn();
      // std::cout << "a" << std::endl;
      return true;
    }
    auto first_ungranted =
        std::find_if(table_queue->request_queue_.begin(), table_queue->request_queue_.end(),
                     [](const std::shared_ptr<LockRequest> &lock_request) { return !lock_request->granted_; });
    // std::cout << "first " << (*first_ungranted)->txn_id_ << std::endl;
    // this request is not the first ungranted, need to wait
    if ((*first_ungranted)->txn_id_ != txn->GetTransactionId() || (*first_ungranted)->lock_mode_ != lock_mode ||
        (*first_ungranted)->oid_ != oid) {
      txn->UnlockTxn();
      // std::cout << "b" << std::endl;
      // std::cout << (*first_ungranted)->txn_id_ << " " << (*first_ungranted)->lock_mode_ << " " <<
      // (*first_ungranted)->oid_ << std::endl;
      return false;
    }
    bool compatible = true;
    for (auto iter = table_queue->request_queue_.begin(); iter != first_ungranted; iter++) {
      if (!IsCompatible(lock_mode, (*iter)->lock_mode_)) {
        compatible = false;
        break;
      }
    }
    if (!compatible) {
      txn->UnlockTxn();
      // std::cout << "c" << std::endl;
      return false;
    }
    switch ((*first_ungranted)->lock_mode_) {
      case LockMode::INTENTION_SHARED:
        txn->GetIntentionSharedTableLockSet()->insert(oid);
        break;
      case LockMode::INTENTION_EXCLUSIVE:
        txn->GetIntentionExclusiveTableLockSet()->insert(oid);
        break;
      case LockMode::SHARED:
        txn->GetSharedTableLockSet()->insert(oid);
        break;
      case LockMode::SHARED_INTENTION_EXCLUSIVE:
        txn->GetSharedIntentionExclusiveTableLockSet()->insert(oid);
        break;
      case LockMode::EXCLUSIVE:
        txn->GetExclusiveTableLockSet()->insert(oid);
        break;
    }
    (*first_ungranted)->granted_ = true;
    if (table_queue->upgrading_ == txn->GetTransactionId()) {
      table_queue->upgrading_ = INVALID_TXN_ID;
    }
    txn->UnlockTxn();
    return true;
  });
  if (abort) {
    if (txn->GetTransactionId() == table_queue->upgrading_) {
      table_queue->upgrading_ = INVALID_TXN_ID;
    }
    table_queue->request_queue_.remove_if([&](const std::shared_ptr<LockRequest> &request) -> bool {
      return request->txn_id_ == txn->GetTransactionId() && request->oid_ == oid && request->lock_mode_ == lock_mode;
    });
    lk.unlock();
    table_queue->cv_.notify_all();
    return false;
  }
  lk.unlock();
  table_queue->cv_.notify_all();
  std::cout << "LockTable: TransactionId " << txn->GetTransactionId();
  switch (lock_mode) {
    case LockMode::INTENTION_SHARED:
      std::cout << " IS";
      break;
    case LockMode::INTENTION_EXCLUSIVE:
      std::cout << " IX";
      break;
    case LockMode::SHARED:
      std::cout << " S";
      break;
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      std::cout << " SIX";
      break;
    case LockMode::EXCLUSIVE:
      std::cout << " X";
      break;
  }
  std::cout << " TableId " << oid << " Success!" << std::endl;
  // std::cout << "HoldLock " << HoldLock(txn, oid, cur_mode) << std::endl;
  return true;
}

auto LockManager::UnlockTable(Transaction *txn, const table_oid_t &oid) -> bool {
  std::cout << "UnlockTable: TransactionId " << txn->GetTransactionId();
  std::cout << " TableId " << oid << std::endl;
  auto table_queue = GetTableQueue(oid);
  txn->LockTxn();
  std::unique_lock<std::mutex> lk(table_queue->latch_);
  AbortReason abort_reason{};
  LockMode unlock_mode{};
  // check state
  if (!NormalUnlockRequestCheck(txn, oid, unlock_mode, abort_reason)) {
    txn->SetState(TransactionState::ABORTED);
    txn->UnlockTxn();
    throw TransactionAbortException(txn->GetTransactionId(), abort_reason);
  }
  // update lock state
  if (auto txn_state = txn->GetState();
      txn_state != TransactionState::COMMITTED && txn_state != TransactionState::ABORTED) {
    UpdateTxnStateOnUnlock(txn, unlock_mode);
  }
  // remove from request queue
  table_queue->request_queue_.remove_if([&](const std::shared_ptr<LockRequest> &request) -> bool {
    return request->txn_id_ == txn->GetTransactionId() && request->oid_ == oid;
  });
  // remove from txn
  switch (unlock_mode) {
    case LockMode::INTENTION_SHARED:
      txn->GetIntentionSharedTableLockSet()->erase(oid);
      break;
    case LockMode::INTENTION_EXCLUSIVE:
      txn->GetIntentionExclusiveTableLockSet()->erase(oid);
      break;
    case LockMode::SHARED:
      txn->GetSharedTableLockSet()->erase(oid);
      break;
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      txn->GetSharedIntentionExclusiveTableLockSet()->erase(oid);
      break;
    case LockMode::EXCLUSIVE:
      txn->GetExclusiveTableLockSet()->erase(oid);
      break;
  }
  lk.unlock();
  txn->UnlockTxn();
  table_queue->cv_.notify_all();
  return true;
}

auto LockManager::LockRow(Transaction *txn, LockMode lock_mode, const table_oid_t &oid, const RID &rid) -> bool {
  std::cout << "LockRow: TransactionId " << txn->GetTransactionId();
  switch (lock_mode) {
    case LockMode::INTENTION_SHARED:
      std::cout << " IS";
      break;
    case LockMode::INTENTION_EXCLUSIVE:
      std::cout << " IX";
      break;
    case LockMode::SHARED:
      std::cout << " S";
      break;
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      std::cout << " SIX";
      break;
    case LockMode::EXCLUSIVE:
      std::cout << " X";
      break;
  }
  std::cout << " TableId " << oid << " Rid " << rid.Get() << std::endl;
  auto row_queue = GetRowQueue(rid);
  txn->LockTxn();
  std::unique_lock<std::mutex> lk(row_queue->latch_);
  AbortReason abort_reason;
  if (!NormalLockRequestCheck(txn, lock_mode, abort_reason)) {
    txn->SetState(TransactionState::ABORTED);
    txn->UnlockTxn();
    throw TransactionAbortException(txn->GetTransactionId(), abort_reason);
  }
  if (!RowLockRequestCheck(txn, lock_mode, oid, abort_reason)) {
    txn->SetState(TransactionState::ABORTED);
    txn->UnlockTxn();
    throw TransactionAbortException(txn->GetTransactionId(), abort_reason);
  }
  LockMode cur_mode;
  if (!HoldLock(txn, oid, rid, cur_mode)) {
    row_queue->request_queue_.push_back(std::make_shared<LockRequest>(txn->GetTransactionId(), lock_mode, oid, rid));
  } else {
    // 2.1 same as current lock_mode
    if (cur_mode == lock_mode) {
      txn->UnlockTxn();
      return true;
    }
    // 2.2 current lock_mode doesn't allowed to upgrade to lock_mode
    if (!IsUpgradeable(cur_mode, lock_mode)) {
      txn->SetState(TransactionState::ABORTED);
      txn->UnlockTxn();
      std::cout << "TransactionId " << txn->GetTransactionId();
      std::cout << "INCOMPATIBLE_UPGRADE" << std::endl;
      throw TransactionAbortException(txn->GetTransactionId(), AbortReason::INCOMPATIBLE_UPGRADE);
    }
    // 2.3 lock upgrading is on the way
    if (row_queue->upgrading_ != INVALID_TXN_ID) {
      txn->SetState(TransactionState::ABORTED);
      txn->UnlockTxn();
      std::cout << "TransactionId " << txn->GetTransactionId();
      std::cout << "UPGRADE_CONFLICT" << std::endl;
      throw TransactionAbortException(txn->GetTransactionId(), AbortReason::UPGRADE_CONFLICT);
    }
    // 2.4 good, give up current lock
    row_queue->upgrading_ = txn->GetTransactionId();
    row_queue->request_queue_.remove_if([&](const std::shared_ptr<LockRequest> &lock_request) {
      return lock_request->txn_id_ == txn->GetTransactionId() && lock_request->oid_ == oid;
    });
    switch (cur_mode) {
      case LockMode::SHARED:
        txn->GetSharedRowLockSet()->erase(oid);
        break;
      case LockMode::EXCLUSIVE:
        txn->GetExclusiveRowLockSet()->erase(oid);
        break;
      case LockMode::INTENTION_SHARED:
      case LockMode::INTENTION_EXCLUSIVE:
      case LockMode::SHARED_INTENTION_EXCLUSIVE:
        break;
    }
    // 2.5 insert to front
    auto it = std::find_if(row_queue->request_queue_.begin(), row_queue->request_queue_.end(),
                           [&](const std::shared_ptr<LockRequest> &lock_request) { return !lock_request->granted_; });
    row_queue->request_queue_.insert(it, std::make_shared<LockRequest>(txn->GetTransactionId(), lock_mode, oid, rid));
  }
  txn->UnlockTxn();
  bool abort = false;
  row_queue->cv_.wait(lk, [&]() -> bool {
    txn->LockTxn();
    if (txn->GetState() == TransactionState::ABORTED) {
      abort = true;
      txn->UnlockTxn();
      return true;
    }
    auto first_ungranted =
        std::find_if(row_queue->request_queue_.begin(), row_queue->request_queue_.end(),
                     [](const std::shared_ptr<LockRequest> &lock_request) { return !lock_request->granted_; });
    // this request is not the first ungranted, need to wait
    if ((*first_ungranted)->txn_id_ != txn->GetTransactionId() || (*first_ungranted)->lock_mode_ != lock_mode ||
        (*first_ungranted)->oid_ != oid || !((*first_ungranted)->rid_ == rid)) {
      txn->UnlockTxn();
      return false;
    }
    bool compatible = true;
    for (auto iter = row_queue->request_queue_.begin(); iter != first_ungranted; iter++) {
      if (!IsCompatible(lock_mode, (*iter)->lock_mode_)) {
        compatible = false;
        break;
      }
    }
    if (!compatible) {
      txn->UnlockTxn();
      return false;
    }
    switch ((*first_ungranted)->lock_mode_) {
      case LockMode::SHARED:
        txn->GetSharedRowLockSet()->operator[](oid).insert(rid);
        break;
      case LockMode::EXCLUSIVE:
        txn->GetExclusiveRowLockSet()->operator[](oid).insert(rid);
        break;
      case LockMode::INTENTION_SHARED:
      case LockMode::INTENTION_EXCLUSIVE:
      case LockMode::SHARED_INTENTION_EXCLUSIVE:
        break;
    }
    (*first_ungranted)->granted_ = true;
    // lock upgrade complete, start another round
    if (row_queue->upgrading_ == txn->GetTransactionId()) {
      row_queue->upgrading_ = INVALID_TXN_ID;
    }
    txn->UnlockTxn();
    return true;
  });
  if (abort) {
    if (txn->GetTransactionId() == row_queue->upgrading_) {
      row_queue->upgrading_ = INVALID_TXN_ID;
    }
    row_queue->request_queue_.remove_if([&](const std::shared_ptr<LockRequest> &request) -> bool {
      return request->txn_id_ == txn->GetTransactionId() && request->oid_ == oid && request->lock_mode_ == lock_mode &&
             request->rid_ == rid;
    });
    lk.unlock();
    row_queue->cv_.notify_all();
    return false;
  }
  lk.unlock();
  row_queue->cv_.notify_all();
  std::cout << "LockRow: TransactionId " << txn->GetTransactionId();
  switch (lock_mode) {
    case LockMode::INTENTION_SHARED:
      std::cout << " IS";
      break;
    case LockMode::INTENTION_EXCLUSIVE:
      std::cout << " IX";
      break;
    case LockMode::SHARED:
      std::cout << " S";
      break;
    case LockMode::SHARED_INTENTION_EXCLUSIVE:
      std::cout << " SIX";
      break;
    case LockMode::EXCLUSIVE:
      std::cout << " X";
      break;
  }
  std::cout << " TableId " << oid << " Rid " << rid.Get() << " Success!" << std::endl;
  return true;
}

auto LockManager::UnlockRow(Transaction *txn, const table_oid_t &oid, const RID &rid) -> bool {
  std::cout << "UnlockRow: TransactionId " << txn->GetTransactionId();
  std::cout << " TableId " << oid << " Rid " << rid.Get() << std::endl;
  auto row_queue = GetRowQueue(rid);
  txn->LockTxn();
  std::unique_lock<std::mutex> lk(row_queue->latch_);
  AbortReason abort_reason{};
  LockMode unlock_mode{};
  if (!RowUnLockRequestCheck(txn, oid, rid, unlock_mode, abort_reason)) {
    txn->SetState(TransactionState::ABORTED);
    txn->UnlockTxn();
    throw TransactionAbortException(txn->GetTransactionId(), abort_reason);
  }
  if (txn->GetState() != TransactionState::ABORTED && txn->GetState() != TransactionState::COMMITTED) {
    UpdateTxnStateOnUnlock(txn, unlock_mode);
  }
  row_queue->request_queue_.remove_if([&](const std::shared_ptr<LockRequest> &request) -> bool {
    return request->txn_id_ == txn->GetTransactionId() && request->oid_ == oid && request->rid_ == rid;
  });
  if (unlock_mode == LockMode::SHARED) {
    // int x = txn->GetSharedRowLockSet()->operator[](oid).size();
    txn->GetSharedRowLockSet()->operator[](oid).erase(rid);
    // int y = txn->GetSharedRowLockSet()->operator[](oid).size();
    // std::cout << "x y z " << x << " " << y << " " << z << std::endl;
  } else if (unlock_mode == LockMode::EXCLUSIVE) {
    txn->GetExclusiveRowLockSet()->operator[](oid).erase(rid);
  }
  txn->UnlockTxn();
  row_queue->cv_.notify_all();
  return true;
}

void LockManager::BuildWaitForGraph() {
  waits_for_.clear();
  // table
  for (const auto &[table_id, table_queue] : table_lock_map_) {
    std::scoped_lock<std::mutex> lk(table_queue->latch_);
    std::unordered_set<txn_id_t> granted;
    std::unordered_set<txn_id_t> ungranted;
    for (const auto &request : table_queue->request_queue_) {
      if (request->granted_) {
        granted.insert(request->txn_id_);
      } else {
        ungranted.insert(request->txn_id_);
      }
    }
    for (const auto &start : ungranted) {
      for (const auto &end : granted) {
        AddEdge(start, end);
      }
    }
  }
  // row
  for (const auto &[row_id, row_queue] : row_lock_map_) {
    std::scoped_lock<std::mutex> lk(row_queue->latch_);
    std::unordered_set<txn_id_t> granted;
    std::unordered_set<txn_id_t> ungranted;
    for (const auto &request : row_queue->request_queue_) {
      if (request->granted_) {
        granted.insert(request->txn_id_);
      } else {
        ungranted.insert(request->txn_id_);
      }
    }
    for (const auto &start : ungranted) {
      for (const auto &end : granted) {
        AddEdge(start, end);
      }
    }
  }
}

auto LockManager::Dfs(txn_id_t curr, std::unordered_set<txn_id_t> &visited, std::vector<txn_id_t> &path) -> txn_id_t {
  visited.insert(curr);
  path.push_back(curr);
  // std::cout << curr << std::endl;
  for (const auto &neighbor : waits_for_[curr]) {
    if (visited.find(neighbor) == visited.end()) {
      auto cycle_id = Dfs(neighbor, visited, path);
      if (cycle_id != -1) {
        return cycle_id;
      }
    } else if (std::find(path.begin(), path.end(), neighbor) != path.end()) {
      return curr;
    }
  }
  path.pop_back();
  return -1;
}

// auto LockManager::TopologicalSort() -> txn_id_t {
//   std::unordered_map<txn_id_t, int> degree;
//   for (const auto &[start, end_set] : waits_for_) {
//     if (degree.find(start) == degree.end()) {
//       degree[start] = 0;
//     }
//     for (const auto &end : end_set) {
//       degree[end]++;
//     }
//   }
//   std::queue<txn_id_t> q;
//   for (const auto &[a, b] : degree) {
//     if (b == 0) {
//       q.push(a);
//     }
//   }
//   while (!q.empty()) {
//     auto t = q.front();
//     q.pop();
//     for (const auto &end : waits_for_[t]) {
//       degree[end]--;
//       if (degree[end] == 0) {
//         q.push(end);
//       }
//     }
//   }
//   int cycle = -1;
//   for (const auto &[a, b] : degree) {
//     if (b > 0) {
//       cycle = std::max(cycle, a);
//     }
//   }
//   return cycle;
// }

void LockManager::AddEdge(txn_id_t t1, txn_id_t t2) {
  // std::cout << "AddEdge " << t1 << " " << t2 << std::endl;
  waits_for_[t1].insert(t2);
}

void LockManager::RemoveEdge(txn_id_t t1, txn_id_t t2) {
  // std::cout << "RemoveEdge " << t1 << " " << t2 << std::endl;
  waits_for_[t1].erase(t2);
}

// auto LockManager::HasCycle(txn_id_t *txn_id) -> bool {
//   int cycle = TopologicalSort();
//   if (cycle == -1) {
//     return false;
//   }
//   *txn_id = cycle;
//   return true;
// }

auto LockManager::HasCycle(txn_id_t *txn_id) -> bool {
  std::vector<txn_id_t> path;
  std::unordered_set<txn_id_t> visited;
  for (const auto &[start, end_set] : waits_for_) {
    if (visited.find(start) == visited.end()) {
      auto cycle_id = Dfs(start, visited, path);
      if (cycle_id != -1) {
        *txn_id = cycle_id;
        return true;
      }
    }
  }
  return false;
}

auto LockManager::GetEdgeList() -> std::vector<std::pair<txn_id_t, txn_id_t>> {
  std::vector<std::pair<txn_id_t, txn_id_t>> edges;
  for (const auto &[start, end_set] : waits_for_) {
    for (const auto &end : end_set) {
      edges.emplace_back(start, end);
    }
  }
  return edges;
}

void LockManager::RunCycleDetection() {
  while (enable_cycle_detection_) {
    std::this_thread::sleep_for(cycle_detection_interval);
    {  // TODO(students): detect deadlock
      std::scoped_lock<std::mutex> table_lock(table_lock_map_latch_);
      std::scoped_lock<std::mutex> row_lock(row_lock_map_latch_);
      BuildWaitForGraph();
      txn_id_t cycle_txn_id = -1;
      while (HasCycle(&cycle_txn_id)) {
        auto cycle_txn = TransactionManager::GetTransaction(cycle_txn_id);
        cycle_txn->SetState(TransactionState::ABORTED);
        // erase cycle_txn_id from wait for graph
        waits_for_.erase(cycle_txn_id);
        for (auto &[_, end_set] : waits_for_) {
          end_set.erase(cycle_txn_id);
        }
        // erase related request from table queue
        for (auto &[table_id, table_queue] : table_lock_map_) {
          std::scoped_lock<std::mutex> lk(table_queue->latch_);
          table_queue->request_queue_.remove_if(
              [&](const std::shared_ptr<LockRequest> &request) -> bool { return request->txn_id_ == cycle_txn_id; });
        }
        // erase related request from row lock queue
        for (auto &[row_id, row_queue] : row_lock_map_) {
          std::scoped_lock<std::mutex> lk(row_queue->latch_);
          row_queue->request_queue_.remove_if(
              [&](const std::shared_ptr<LockRequest> &request) -> bool { return request->txn_id_ == cycle_txn_id; });
        }
      }
      // std::cout << "cycle " << cycle_txn_id << std::endl;
      if (cycle_txn_id != -1) {
        for (const auto &[table_id, table_queue] : table_lock_map_) {
          table_queue->cv_.notify_all();
        }
        for (const auto &[row_id, row_queue] : row_lock_map_) {
          row_queue->cv_.notify_all();
        }
      }
    }
  }
}

}  // namespace bustub
