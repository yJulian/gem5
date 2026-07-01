// Copyright 2026 Antigravity
// Description: General AXI4 Memory Interface class declaration.

#ifndef __CPU_RTL_AXI_MEM_IFACE_AXI_HH__
#define __CPU_RTL_AXI_MEM_IFACE_AXI_HH__

#include <deque>
#include <vector>
#include <utility>

#include "cpu/rtl/mem_iface_base.hh"

namespace gem5
{

class RtlMemIfaceAxi : public RtlMemIfaceBase
{
  protected:
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

    // Pure virtual methods to connect with physical RTL signals on the core wrapper
    // AR Channel
    virtual uint8_t get_noc_req_ar_valid_o() = 0;
    virtual uint64_t get_noc_req_ar_addr_o() = 0;
    virtual uint8_t get_noc_req_ar_id_o() = 0;
    virtual uint8_t get_noc_req_ar_len_o() = 0;
    virtual uint8_t get_noc_req_ar_size_o() = 0;
    virtual uint8_t get_noc_req_ar_prot_o() = 0;
    virtual void set_noc_resp_ar_ready_i(uint8_t val) = 0;

    // R Channel
    virtual uint8_t get_noc_req_r_ready_o() = 0;
    virtual void set_noc_resp_r_valid_i(uint8_t val) = 0;
    virtual void set_noc_resp_r_last_i(uint8_t val) = 0;
    virtual void set_noc_resp_r_data_i(uint64_t val) = 0;
    virtual void set_noc_resp_r_id_i(uint8_t val) = 0;
    virtual void set_noc_resp_r_resp_i(uint8_t val) = 0;

    // AW Channel
    virtual uint8_t get_noc_req_aw_valid_o() = 0;
    virtual uint64_t get_noc_req_aw_addr_o() = 0;
    virtual uint8_t get_noc_req_aw_id_o() = 0;
    virtual uint8_t get_noc_req_aw_len_o() = 0;
    virtual uint8_t get_noc_req_aw_size_o() = 0;
    virtual void set_noc_resp_aw_ready_i(uint8_t val) = 0;

    // W Channel
    virtual uint8_t get_noc_req_w_valid_o() = 0;
    virtual uint64_t get_noc_req_w_data_o() = 0;
    virtual uint8_t get_noc_req_w_last_o() = 0;
    virtual void set_noc_resp_w_ready_i(uint8_t val) = 0;

    // B Channel
    virtual uint8_t get_noc_req_b_ready_o() = 0;
    virtual void set_noc_resp_b_valid_i(uint8_t val) = 0;
    virtual void set_noc_resp_b_id_i(uint8_t val) = 0;
    virtual void set_noc_resp_b_resp_i(uint8_t val) = 0;

    // Hook for concrete cores to get PC if they need it for simulation loop exit
    virtual uint64_t get_pc_o() = 0;

  public:
    RtlMemIfaceAxi(RtlCpuHelper *_helper);
    virtual ~RtlMemIfaceAxi() = default;

    void driveInputs() override;
    void sampleOutputs() override;
    void acceptResp(PacketPtr pkt) override;
};

} // namespace gem5

#endif // __CPU_RTL_AXI_MEM_IFACE_AXI_HH__
