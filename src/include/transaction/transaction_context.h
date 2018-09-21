#pragma once
#include <vector>
#include "common/object_pool.h"
#include "common/typedefs.h"
#include "storage/data_table.h"
#include "storage/record_buffer.h"
#include "storage/storage_defs.h"
#include "storage/tuple_access_strategy.h"
#include "storage/undo_record.h"
#include "storage/write_ahead_log/log_record.h"
#include "transaction/transaction_util.h"

namespace terrier::storage {
class GarbageCollector;
}

namespace terrier::transaction {
/**
 * A transaction context encapsulates the information kept while the transaction is running
 */
class TransactionContext {
 public:
  /**
   * Constructs a new transaction context. Beware that the buffer pool given must be the same one the log manager uses,
   * if logging is enabled.
   * // TODO(Tianyu): We can terrier assert the above condition, but I need to go figure out friends.
   * @param start the start timestamp of the transaction
   * @param txn_id the id of the transaction, should be larger than all start time and commit time
   * @param buffer_pool the buffer pool to draw this transaction's undo buffer from
   * @param log_manager pointer to log manager in the system, or nullptr, if logging is disabled
   */
  TransactionContext(const timestamp_t start, const timestamp_t txn_id,
                     storage::RecordBufferSegmentPool *const buffer_pool, storage::LogManager *const log_manager)
      : start_time_(start), txn_id_(txn_id), undo_buffer_(buffer_pool), redo_buffer_(log_manager, buffer_pool) {}

  /**
   * @return start time of this transaction
   */
  timestamp_t StartTime() const { return start_time_; }

  /**
   * @return id of this transaction
   */
  const std::atomic<timestamp_t> &TxnId() const { return txn_id_; }

  /**
   * @return id of this transaction
   */
  std::atomic<timestamp_t> &TxnId() { return txn_id_; }

  /**
   * Reserve space on this transaction's undo buffer for a record to log the update given
   * @param table pointer to the updated DataTable object
   * @param slot the TupleSlot being updated
   * @param redo the content of the update
   * @return a persistent pointer to the head of a memory chunk large enough to hold the undo record
   */
  storage::UndoRecord *UndoRecordForUpdate(storage::DataTable *const table, const storage::TupleSlot slot,
                                           const storage::ProjectedRow &redo) {
    const uint32_t size = storage::UndoRecord::Size(redo);
    return storage::UndoRecord::Initialize(undo_buffer_.NewEntry(size), txn_id_.load(), slot, table, redo);
  }

  /**
   * Reserve space on this transaction's undo buffer for a record to log the insert given
   * @param table pointer to the updated DataTable object
   * @param slot the TupleSlot being updated
   * @param insert_record_initializer ProjectedRowInitializer used to initialize an insert undo record
   * @return a persistent pointer to the head of a memory chunk large enough to hold the undo record
   */
  storage::UndoRecord *UndoRecordForInsert(storage::DataTable *const table, const storage::TupleSlot slot,
                                           const storage::ProjectedRowInitializer &insert_record_initializer) {
    byte *result = undo_buffer_.NewEntry(storage::UndoRecord::Size(insert_record_initializer));
    return storage::UndoRecord::Initialize(result, txn_id_.load(), slot, table, insert_record_initializer);
  }

  /**
   * Expose a record that can hold a change, described by the initializer given, that will be logged out to disk.
   * The change can either be copied into this space, or written in the space and then used to change the DataTable.
   * @param table the DataTable that this record changes
   * @param slot the slot that this record changes
   * @param initializer the initializer to use for the underlying record
   * @return pointer to the initialized redo record.
   */
  storage::RedoRecord *StageWrite(storage::DataTable *const table, const storage::TupleSlot slot,
                                  const storage::ProjectedRowInitializer &initializer) {
    uint32_t size = storage::RedoRecord::Size(initializer);
    auto *log_record =
        storage::RedoRecord::Initialize(redo_buffer_.NewEntry(size), start_time_, table, slot, initializer);
    return log_record->GetUnderlyingRecordBodyAs<storage::RedoRecord>();
  }

 private:
  friend class storage::GarbageCollector;
  friend class TransactionManager;
  const timestamp_t start_time_;
  std::atomic<timestamp_t> txn_id_;
  storage::UndoBuffer undo_buffer_;
  storage::RedoBuffer redo_buffer_;
};
}  // namespace terrier::transaction