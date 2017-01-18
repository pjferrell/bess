#include "netbricks.h"

#include <dlfcn.h>

#include <cstdlib>
#include <memory>

#include "../mem_alloc.h"

pb_error_t NetBricks::Init(const bess::pb::NetBricksArg &arg) {
  static_assert(NetBricks::kBufSize >= bess::PacketBatch::kMaxBurst,
                "kBufSize is too small");

  if (arg.file() == "") {
    return pb_error(EINVAL, "'file' must be specified");
  }

  for (const auto &it : arg.env()) {
    setenv(it.first.c_str(), it.second.c_str(), 1);
  }

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

  num_gates_ = arg.num_gates();
  if (num_gates_ < 1) {
    num_gates_ = 1;
  }

  return InitNetBricks();
}

pb_error_t NetBricks::InitNetBricks() {
  rx_buf_ptrs_ = (PacketBuf **)mem_alloc(sizeof(PacketBuf *) * num_gates_);
  tx_buf_ptrs_ = (PacketBuf **)mem_alloc(sizeof(PacketBuf *) * num_gates_);

  for (size_t i = 0; i < num_gates_; i++) {
    rx_buf_ptrs_[i] = (PacketBuf *)mem_alloc(sizeof(PacketBuf));
    rx_buf_ptrs_[i]->capacity = kBufSize;
    rx_buf_ptrs_[i]->cnt = 0;
    rx_buf_ptrs_[i]->pkts = alloc_.allocate(kBufSize);

    tx_buf_ptrs_[i] = (PacketBuf *)mem_alloc(sizeof(PacketBuf));
    tx_buf_ptrs_[i]->capacity = kBufSize;
    tx_buf_ptrs_[i]->cnt = 0;
    tx_buf_ptrs_[i]->pkts = alloc_.allocate(kBufSize);
  }

  void *(*init_mod)(size_t, PacketBuf **, PacketBuf **);
  init_mod = reinterpret_cast<void *(*)(size_t, PacketBuf **, PacketBuf **)>(
      dlsym(handle_, "init_mod"));
  if (!init_mod) {
    return pb_error(EINVAL, "init_mod() is not found: %s", dlerror());
  }

  ctx_ = init_mod(num_gates_, rx_buf_ptrs_, tx_buf_ptrs_);
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

  for (size_t i = 0; i < num_gates_; i++) {
    alloc_.deallocate(rx_buf_ptrs_[i]->pkts, kBufSize);
    alloc_.deallocate(tx_buf_ptrs_[i]->pkts, kBufSize);

    mem_free(rx_buf_ptrs_[i]);
    mem_free(tx_buf_ptrs_[i]);
  }

  mem_free(rx_buf_ptrs_);
  mem_free(tx_buf_ptrs_);
}

void NetBricks::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t igate = get_igate();
  if (igate >= num_gates_) {
    bess::Packet::Free(batch);
    return;
  }

  PacketBuf *rx_buf = rx_buf_ptrs_[get_igate()];

  rx_buf->cnt = batch->cnt();
  rte_memcpy(rx_buf->pkts, batch->pkts(), sizeof(bess::Packet *) * rx_buf->cnt);

  func_run_(ctx_);

  for (size_t i = 0; i < num_gates_; i++) {
    PacketBuf *tx_buf = tx_buf_ptrs_[i];
    bess::Packet **ptr = tx_buf->pkts;
    while (tx_buf->cnt > 0) {
      int pkts_in_batch = std::min(bess::PacketBatch::kMaxBurst, tx_buf->cnt);

      rte_memcpy(batch->pkts(), ptr, sizeof(bess::Packet *) * pkts_in_batch);
      batch->set_cnt(pkts_in_batch);

      tx_buf->cnt -= pkts_in_batch;
      ptr += pkts_in_batch;

      RunChooseModule(i, batch);
    }
  }
}

ADD_MODULE(NetBricks, "netbricks", "embeds a Netbricks datapath")
