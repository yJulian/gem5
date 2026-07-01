// Copyright 2026 Antigravity
// Description: CVA6 specific AXI4 Memory Interface class declaration.

#ifndef __CPU_RTL_AXI_CVA6_CVA6_MEM_IFACE_AXI_HH__
#define __CPU_RTL_AXI_CVA6_CVA6_MEM_IFACE_AXI_HH__

#include "cpu/rtl/axi/cva6/cva6_rtl_core_interface.hh"
#include "cpu/rtl/axi/mem_iface_axi.hh"

namespace gem5
{

class CVA6MemIfaceAxi : public RtlMemIfaceAxi
{
  private:
    CVA6RtlCoreInterface *core;

  protected:
    // Core physical RTL signals overrides for AR Channel
    uint8_t get_noc_req_ar_valid_o() override { return core->get_noc_req_ar_valid_o(); }
    uint64_t get_noc_req_ar_addr_o() override { return core->get_noc_req_ar_addr_o(); }
    uint8_t get_noc_req_ar_id_o() override { return core->get_noc_req_ar_id_o(); }
    uint8_t get_noc_req_ar_len_o() override { return core->get_noc_req_ar_len_o(); }
    uint8_t get_noc_req_ar_size_o() override { return core->get_noc_req_ar_size_o(); }
    uint8_t get_noc_req_ar_prot_o() override { return core->get_noc_req_ar_prot_o(); }
    void set_noc_resp_ar_ready_i(uint8_t val) override { core->set_noc_resp_ar_ready_i(val); }

    // Core physical RTL signals overrides for R Channel
    uint8_t get_noc_req_r_ready_o() override { return core->get_noc_req_r_ready_o(); }
    void set_noc_resp_r_valid_i(uint8_t val) override { core->set_noc_resp_r_valid_i(val); }
    void set_noc_resp_r_last_i(uint8_t val) override { core->set_noc_resp_r_last_i(val); }
    void set_noc_resp_r_data_i(uint64_t val) override { core->set_noc_resp_r_data_i(val); }
    void set_noc_resp_r_id_i(uint8_t val) override { core->set_noc_resp_r_id_i(val); }
    void set_noc_resp_r_resp_i(uint8_t val) override { core->set_noc_resp_r_resp_i(val); }

    // Core physical RTL signals overrides for AW Channel
    uint8_t get_noc_req_aw_valid_o() override { return core->get_noc_req_aw_valid_o(); }
    uint64_t get_noc_req_aw_addr_o() override { return core->get_noc_req_aw_addr_o(); }
    uint8_t get_noc_req_aw_id_o() override { return core->get_noc_req_aw_id_o(); }
    uint8_t get_noc_req_aw_len_o() override { return core->get_noc_req_aw_len_o(); }
    uint8_t get_noc_req_aw_size_o() override { return core->get_noc_req_aw_size_o(); }
    void set_noc_resp_aw_ready_i(uint8_t val) override { core->set_noc_resp_aw_ready_i(val); }

    // Core physical RTL signals overrides for W Channel
    uint8_t get_noc_req_w_valid_o() override { return core->get_noc_req_w_valid_o(); }
    uint64_t get_noc_req_w_data_o() override { return core->get_noc_req_w_data_o(); }
    uint8_t get_noc_req_w_last_o() override { return core->get_noc_req_w_last_o(); }
    void set_noc_resp_w_ready_i(uint8_t val) override { core->set_noc_resp_w_ready_i(val); }

    // Core physical RTL signals overrides for B Channel
    uint8_t get_noc_req_b_ready_o() override { return core->get_noc_req_b_ready_o(); }
    void set_noc_resp_b_valid_i(uint8_t val) override { core->set_noc_resp_b_valid_i(val); }
    void set_noc_resp_b_id_i(uint8_t val) override { core->set_noc_resp_b_id_i(val); }
    void set_noc_resp_b_resp_i(uint8_t val) override { core->set_noc_resp_b_resp_i(val); }

    // Core override for PC
    uint64_t get_pc_o() override { return core->get_pc_o(); }

  public:
    CVA6MemIfaceAxi(RtlCpuHelper *_helper, CVA6RtlCoreInterface *_core);
    virtual ~CVA6MemIfaceAxi() = default;
};

} // namespace gem5

#endif // __CPU_RTL_AXI_CVA6_CVA6_MEM_IFACE_AXI_HH__
