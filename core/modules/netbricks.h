#ifndef BESS_MODULES_NETBRICKS_
#define BESS_MODULES_NETBRICKS_

#include "../module.h"
#include "../module_msg.pb.h"

class NetBricks final : public Module {
 public:
  static const gate_idx_t kNumIGates = 16;
  static const gate_idx_t kNumOGates = 16;

  pb_error_t Init(const bess::pb::NetBricksArg &arg);
  void DeInit();

  void ProcessBatch(bess::PacketBatch *batch);

 private:
  struct PacketBuf {
    size_t capacity;
    size_t cnt;
    bess::Packet **pkts;
  };

  static constexpr size_t kBufSize = 64;

  pb_error_t InitNetBricks();

  std::allocator<bess::Packet *> alloc_;

  void *handle_;                // shared object handle

  void *ctx_;                   // opaque pointer to the context
  void (*func_run_)(void *);    // address of run_once() function

  size_t num_gates_;
  PacketBuf **rx_buf_ptrs_;
  PacketBuf **tx_buf_ptrs_;
};

#endif  // BESS_MODULES_NETBRICKS_
