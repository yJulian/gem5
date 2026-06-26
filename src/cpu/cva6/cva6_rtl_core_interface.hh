// Copyright 2026 Antigravity
// Description: Abstract interface for the CVA6 RTL Verilated core.

#ifndef __CPU_CVA6_CVA6_RTL_CORE_INTERFACE_HH__
#define __CPU_CVA6_CVA6_RTL_CORE_INTERFACE_HH__

#include <cstdint>
#include <string>

namespace gem5
{

class CVA6RtlCoreInterface
{
  public:
    virtual ~CVA6RtlCoreInterface() {}

    // Inputs to core
    virtual void set_clk_i(uint8_t val) = 0;
    virtual void set_rst_ni(uint8_t val) = 0;
    virtual void set_debug_req_i(uint8_t val) = 0;
    virtual void set_noc_resp_aw_ready_i(uint8_t val) = 0;
    virtual void set_noc_resp_w_ready_i(uint8_t val) = 0;
    virtual void set_noc_resp_ar_ready_i(uint8_t val) = 0;
    virtual void set_irq_i(uint8_t val) = 0;
    virtual void set_ipi_i(uint8_t val) = 0;
    virtual void set_time_irq_i(uint8_t val) = 0;
    virtual void set_noc_resp_b_valid_i(uint8_t val) = 0;
    virtual void set_noc_resp_b_id_i(uint8_t val) = 0;
    virtual void set_noc_resp_b_resp_i(uint8_t val) = 0;
    virtual void set_noc_resp_r_valid_i(uint8_t val) = 0;
    virtual void set_noc_resp_r_id_i(uint8_t val) = 0;
    virtual void set_noc_resp_r_last_i(uint8_t val) = 0;
    virtual void set_noc_resp_r_resp_i(uint8_t val) = 0;
    virtual void set_boot_addr_i(uint64_t val) = 0;
    virtual void set_hart_id_i(uint64_t val) = 0;
    virtual void set_noc_resp_r_data_i(uint64_t val) = 0;

    // Outputs from core
    virtual uint8_t get_noc_req_w_last_o() = 0;
    virtual uint8_t get_noc_req_aw_valid_o() = 0;
    virtual uint8_t get_noc_req_aw_id_o() = 0;
    virtual uint8_t get_noc_req_aw_len_o() = 0;
    virtual uint8_t get_noc_req_aw_size_o() = 0;
    virtual uint8_t get_noc_req_w_valid_o() = 0;
    virtual uint8_t get_noc_req_b_ready_o() = 0;
    virtual uint8_t get_noc_req_ar_valid_o() = 0;
    virtual uint8_t get_noc_req_ar_id_o() = 0;
    virtual uint8_t get_noc_req_ar_len_o() = 0;
    virtual uint8_t get_noc_req_ar_size_o() = 0;
    virtual uint8_t get_noc_req_ar_prot_o() = 0;
    virtual uint8_t get_noc_req_r_ready_o() = 0;
    virtual uint64_t get_noc_req_aw_addr_o() = 0;
    virtual uint64_t get_noc_req_w_data_o() = 0;
    virtual uint64_t get_noc_req_ar_addr_o() = 0;
    virtual uint8_t get_ebreak_o() = 0;
    virtual uint8_t get_illegal_instr_o() = 0;
    virtual uint64_t get_illegal_instr_pc_o() = 0;

    // Control
    virtual void eval() = 0;

    // Tracing
    virtual void setup_trace(const std::string &trace_file) = 0;
    virtual void dump_trace(uint64_t time) = 0;
    virtual void close_trace() = 0;
};

} // namespace gem5

// DLL factory function type signatures
typedef gem5::CVA6RtlCoreInterface *(*create_core_t)();
typedef void (*destroy_core_t)(gem5::CVA6RtlCoreInterface *);

#endif // __CPU_CVA6_CVA6_RTL_CORE_INTERFACE_HH__
