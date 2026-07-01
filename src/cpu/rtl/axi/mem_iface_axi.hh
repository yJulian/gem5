// Copyright 2026 Antigravity
// Description: General AXI4 Memory Interface class declaration.

#ifndef __CPU_RTL_AXI_MEM_IFACE_AXI_HH__
#define __CPU_RTL_AXI_MEM_IFACE_AXI_HH__

#include <deque>
#include <vector>
#include <utility>

#include "cpu/rtl/axi/rtl_core_interface.hh"
#include "cpu/rtl/mem_iface_base.hh"

namespace gem5
{

class RtlMemIfaceAxi : public RtlMemIfaceBase
{
  protected:
    RtlCoreInterface *core;

    // AXI state buffers for co-simulation
    bool ar_busy;
    bool r_data_ready;
    uint64_t read_addr;
    uint32_t read_id;
    uint32_t read_len;
    uint32_t read_size;
    uint32_t read_beat;
    std::vector<uint8_t> read_data_buffer;

    bool aw_received;
    uint64_t write_addr;
    uint32_t write_id;
    uint32_t write_size;
    uint32_t write_len;
    uint32_t w_received_beats;

    std::deque<std::pair<uint8_t, uint32_t>> writeXacts;
    std::deque<uint8_t> bRespQueue;
    bool b_handshake_pending;

    // Set when a tohost write is in-flight. exitSimulation is triggered in
    // acceptResp() once SimpleMemory ACKs the write (data is now in physProxy).
    bool tohost_exit_pending;
    std::string tohost_exit_msg;

  public:
    RtlMemIfaceAxi(RtlCpuHelper *_helper, RtlCoreInterface *_core);
    virtual ~RtlMemIfaceAxi() = default;

    void driveInputs() override;
    void sampleOutputs() override;
    void acceptResp(PacketPtr pkt) override;
};

} // namespace gem5

#endif // __CPU_RTL_AXI_MEM_IFACE_AXI_HH__
