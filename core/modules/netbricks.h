#ifndef BESS_MODULES_NETBRICKS_
#define BESS_MODULES_NETBRICKS_

#include "../module.h"
#include "../module_msg.pb.h"

class NetBricks final : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  pb_error_t Init(const bess::pb::NetBricksArg &arg);
  void DeInit();

  void ProcessBatch(bess::PacketBatch *batch);

 private:
  struct PacketBuf {
    size_t capacity;
    size_t cnt;
    bess::Packet **pkts;
  };

  const size_t kBufSize = 64;

  pb_error_t InitNetBricks();

  std::allocator<bess::Packet *> alloc_;

  void *handle_;                // shared object handle

  void *ctx_;
  void (*func_run_)(void *);    // address of run_once() function

  PacketBuf rx_buf_;
  PacketBuf tx_buf_;
};

#endif  // BESS_MODULES_NETBRICKS_

