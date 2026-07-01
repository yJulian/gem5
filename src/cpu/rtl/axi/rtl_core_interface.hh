// Copyright 2026 Antigravity
// Description: Abstract generic interface for the RTL Verilated core.

#ifndef __CPU_RTL_AXI_RTL_CORE_INTERFACE_HH__
#define __CPU_RTL_AXI_RTL_CORE_INTERFACE_HH__

#include <cstdint>
#include <string>

namespace gem5
{

class RtlCoreInterface
{
  public:
    virtual ~RtlCoreInterface() {}

    // Inputs to core
    virtual void set_clk(uint8_t val) = 0;
    virtual void set_rst_n(uint8_t val) = 0;
    virtual void set_debug_req(uint8_t val) = 0;
    virtual void set_aw_ready(uint8_t val) = 0;
    virtual void set_w_ready(uint8_t val) = 0;
    virtual void set_ar_ready(uint8_t val) = 0;
    virtual void set_irq(uint8_t val) = 0;
    virtual void set_ipi(uint8_t val) = 0;
    virtual void set_time_irq(uint8_t val) = 0;
    virtual void set_b_valid(uint8_t val) = 0;
    virtual void set_b_id(uint8_t val) = 0;
    virtual void set_b_resp(uint8_t val) = 0;
    virtual void set_r_valid(uint8_t val) = 0;
    virtual void set_r_id(uint8_t val) = 0;
    virtual void set_r_last(uint8_t val) = 0;
    virtual void set_r_resp(uint8_t val) = 0;
    virtual void set_boot_addr(uint64_t val) = 0;
    virtual void set_hart_id(uint64_t val) = 0;
    virtual void set_r_data(uint64_t val) = 0;
    virtual void set_boot_reg(uint64_t val) = 0;

    // Outputs from core
    virtual uint8_t get_w_last() = 0;
    virtual uint8_t get_aw_valid() = 0;
    virtual uint8_t get_aw_id() = 0;
    virtual uint8_t get_aw_len() = 0;
    virtual uint8_t get_aw_size() = 0;
    virtual uint8_t get_w_valid() = 0;
    virtual uint8_t get_b_ready() = 0;
    virtual uint8_t get_ar_valid() = 0;
    virtual uint8_t get_ar_id() = 0;
    virtual uint8_t get_ar_len() = 0;
    virtual uint8_t get_ar_size() = 0;
    virtual uint8_t get_ar_prot() = 0;
    virtual uint8_t get_r_ready() = 0;
    virtual uint64_t get_aw_addr() = 0;
    virtual uint64_t get_w_data() = 0;
    virtual uint64_t get_ar_addr() = 0;
    virtual uint8_t get_ebreak() = 0;
    virtual uint8_t get_illegal_instr() = 0;
    virtual uint64_t get_illegal_instr_pc() = 0;
    virtual uint64_t get_pc() = 0;

    // Control
    virtual void eval() = 0;

    // Tracing
    virtual void setup_trace(const std::string &trace_file) = 0;
    virtual void dump_trace(uint64_t time) = 0;
    virtual void close_trace() = 0;
};

} // namespace gem5

// DLL factory function type signatures
typedef gem5::RtlCoreInterface *(*create_core_t)();
typedef void (*destroy_core_t)(gem5::RtlCoreInterface *);

#endif // __CPU_RTL_AXI_RTL_CORE_INTERFACE_HH__
