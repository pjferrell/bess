#ifndef BESS_MODULES_EXACTMATCH_H_
#define BESS_MODULES_EXACTMATCH_H_

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/cuckoo_map.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8

static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define HASH_KEY_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

using google::protobuf::RepeatedPtrField;
using bess::utils::HashResult;
using bess::utils::CuckooMapWithVariableKeySize;

struct em_hkey_t {
  uint64_t u64_arr[MAX_FIELDS];
};

inline bool em_keyeq(const em_hkey_t &lhs, const em_hkey_t &rhs, size_t len) {
  const uint64_t *a = lhs.u64_arr;
  const uint64_t *b = rhs.u64_arr;

  switch (len >> 3) {
    default:
      promise_unreachable();
    case 8:
      if (unlikely(a[7] != b[7]))
        return false;
    case 7:
      if (unlikely(a[6] != b[6]))
        return false;
    case 6:
      if (unlikely(a[5] != b[5]))
        return false;
    case 5:
      if (unlikely(a[4] != b[4]))
        return false;
    case 4:
      if (unlikely(a[3] != b[3]))
        return false;
    case 3:
      if (unlikely(a[2] != b[2]))
        return false;
    case 2:
      if (unlikely(a[1] != b[1]))
        return false;
    case 1:
      if (unlikely(a[0] != b[0]))
        return false;
  }
  return true;
}

inline HashResult em_hash(const em_hkey_t &key, size_t len) {
  HashResult init_val = 0;
#if __SSE4_2__ && __x86_64
  const uint64_t *a = key.u64_arr;

  switch (len >> 3) {
    default:
      promise_unreachable();
    case 8:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 7:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 6:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 5:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 4:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 3:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 2:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 1:
      init_val = crc32c_sse42_u64(*a++, init_val);
  }

  return init_val;
#else
  return rte_hash_crc(key, key.key_len, init_val);
#endif
}

inline bool em_keyeq_fixed(const em_hkey_t &lhs, const em_hkey_t &rhs) {
  return em_keyeq(lhs, rhs, sizeof(em_hkey_t));
}

inline HashResult em_hash_fixed(const em_hkey_t &key) {
  return em_hash(key, sizeof(em_hkey_t));
}

typedef CuckooMapWithVariableKeySize<em_hkey_t, gate_idx_t, em_hash_fixed,
                                     em_keyeq_fixed, em_hash, em_keyeq>
    htable_t;

struct EmField {
  /* bits with 1: the bit must be considered.
   * bits with 0: don't care */
  uint64_t mask;

  int attr_id; /* -1 for offset-based fields */

  /* Relative offset in the packet data for offset-based fields.
   *  (starts from data_off, not the beginning of the headroom */
  int offset;

  int pos; /* relative position in the key */

  int size; /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

class ExactMatch final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  ExactMatch()
      : Module(),
        default_gate_(),
        total_key_size_(),
        num_fields_(),
        fields_(),
        ht_() {}

  void ProcessBatch(bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  pb_error_t Init(const bess::pb::ExactMatchArg &arg);
  pb_cmd_response_t CommandAdd(const bess::pb::ExactMatchCommandAddArg &arg);
  pb_cmd_response_t CommandDelete(
      const bess::pb::ExactMatchCommandDeleteArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);
  pb_cmd_response_t CommandSetDefaultGate(
      const bess::pb::ExactMatchCommandSetDefaultGateArg &arg);

 private:
  pb_error_t AddFieldOne(const bess::pb::ExactMatchArg_Field &field,
                         struct EmField *f, int idx);
  pb_error_t GatherKey(const RepeatedPtrField<std::string> &fields,
                       em_hkey_t *key);

  gate_idx_t default_gate_;

  uint32_t total_key_size_;

  int num_fields_;
  EmField fields_[MAX_FIELDS];

  htable_t ht_;
};

#endif  // BESS_MODULES_EXACTMATCH_H_
