#include "netbricks.h"

#include <dlfcn.h>

#include <memory>

pb_error_t NetBricks::Init(const bess::pb::NetBricksArg &arg) {
  if (arg.file() == "")
    return pb_error(EINVAL, "'file' must be given");

  // Make sure "file" (libxxx.so) is available
  handle_ = dlmopen(LM_ID_NEWLM, arg.file().c_str(), RTLD_LAZY);
  if (!handle_) {
    return pb_error(EINVAL, "dlmopen(%s) failed: %s", arg.file().c_str(),
                    dlerror());
  }

  func_run_ = reinterpret_cast<void (*)(void *)>(dlsym(handle_, "run_once"));
  if (!func_run_) {
    return pb_error(EINVAL, "run_once() is not found: %s", dlerror());
  }

  return InitNetBricks();
}

pb_error_t NetBricks::InitNetBricks() {
  rx_buf_ = {
    .capacity = kBufSize,
    .cnt = 0,
    .pkts = alloc_.allocate(kBufSize),
  };

  tx_buf_ = {
    .capacity = kBufSize,
    .cnt = 0,
    .pkts = alloc_.allocate(kBufSize),
  };

  void *(*init_mod)(PacketBuf *, PacketBuf *);
  init_mod = reinterpret_cast<void *(*)(PacketBuf *, PacketBuf *)>(
      dlsym(handle_, "init_mod"));
  if (!init_mod) {
    return pb_error(EINVAL, "init_mod() is not found: %s", dlerror());
  }

  ctx_ = init_mod(&rx_buf_, &tx_buf_);
  if (!ctx_) {
    return pb_error(EINVAL, "init_mod() failed");
  }

  return pb_errno(0);
}

void NetBricks::DeInit() {
  if (handle_) {
    if (dlclose(handle_) != 0) {
      LOG(WARNING) << "dlclose() failed: %s", dlerror();
    }
  }

  alloc_.deallocate(rx_buf_.pkts, kBufSize);
  alloc_.deallocate(tx_buf_.pkts, kBufSize);
}

void NetBricks::ProcessBatch(bess::PacketBatch *batch) {
  //gate_idx_t igate = get_igate();

  rx_buf_.cnt = batch->cnt();
  rte_memcpy(rx_buf_.pkts, batch->pkts(), sizeof(bess::Packet *) * rx_buf_.cnt);

  func_run_(ctx_);

  bess::Packet **ptr = tx_buf_.pkts;
  while (tx_buf_.cnt > 0) {
    int pkts_in_batch = std::min(bess::PacketBatch::kMaxBurst, tx_buf_.cnt);

    rte_memcpy(batch->pkts(), ptr, sizeof(bess::Packet *) * pkts_in_batch);
    batch->set_cnt(pkts_in_batch);

    tx_buf_.cnt -= pkts_in_batch;
    ptr += pkts_in_batch;

    RunNextModule(batch);
  }
}

ADD_MODULE(NetBricks, "netbricks", "embeds a Netbricks datapath")
