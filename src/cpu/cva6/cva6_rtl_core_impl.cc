// Copyright 2026 Antigravity
// Description: Implementation of the CVA6 RTL Core interface wrapping
// Verilated Vcva6_top.

#include "Vcva6_top.h"
#include "Vcva6_top__Syms.h"
#include "cva6_rtl_core_interface.hh"
#include "verilated.h"

#if VM_TRACE
#include "verilated_vcd_c.h"

#endif

namespace gem5
{

class CVA6RtlCoreImpl : public CVA6RtlCoreInterface
{
  private:
    Vcva6_top *core;
#if VM_TRACE
    VerilatedVcdC *tfp;
#endif

  public:
    CVA6RtlCoreImpl()
        : core(new Vcva6_top())
#if VM_TRACE
          ,
          tfp(nullptr)
#endif
    {}

    ~CVA6RtlCoreImpl() override
    {
        close_trace();
        delete core;
    }

    // Inputs to core
    void
    set_clk_i(uint8_t val) override
    {
        core->clk_i = val;
    }
    void
    set_rst_ni(uint8_t val) override
    {
        core->rst_ni = val;
    }
    void
    set_debug_req_i(uint8_t val) override
    {
        core->debug_req_i = val;
    }
    void
    set_noc_resp_aw_ready_i(uint8_t val) override
    {
        core->noc_resp_aw_ready_i = val;
    }
    void
    set_noc_resp_w_ready_i(uint8_t val) override
    {
        core->noc_resp_w_ready_i = val;
    }
    void
    set_noc_resp_ar_ready_i(uint8_t val) override
    {
        core->noc_resp_ar_ready_i = val;
    }
    void
    set_irq_i(uint8_t val) override
    {
        core->irq_i = val;
    }
    void
    set_ipi_i(uint8_t val) override
    {
        core->ipi_i = val;
    }
    void
    set_time_irq_i(uint8_t val) override
    {
        core->time_irq_i = val;
    }
    void
    set_noc_resp_b_valid_i(uint8_t val) override
    {
        core->noc_resp_b_valid_i = val;
    }
    void
    set_noc_resp_b_id_i(uint8_t val) override
    {
        core->noc_resp_b_id_i = val;
    }
    void
    set_noc_resp_b_resp_i(uint8_t val) override
    {
        core->noc_resp_b_resp_i = val;
    }
    void
    set_noc_resp_r_valid_i(uint8_t val) override
    {
        core->noc_resp_r_valid_i = val;
    }
    void
    set_noc_resp_r_id_i(uint8_t val) override
    {
        core->noc_resp_r_id_i = val;
    }
    void
    set_noc_resp_r_last_i(uint8_t val) override
    {
        core->noc_resp_r_last_i = val;
    }
    void
    set_noc_resp_r_resp_i(uint8_t val) override
    {
        core->noc_resp_r_resp_i = val;
    }
    void
    set_boot_addr_i(uint64_t val) override
    {
        core->boot_addr_i = val;
    }
    void
    set_hart_id_i(uint64_t val) override
    {
        core->hart_id_i = val;
    }
    void
    set_init_a1_i(uint64_t val) override
    {
        auto& regfile = core->rootp->vlSymsp->TOP__cva6_top__i_ariane__i_cva6__issue_stage_i__i_issue_read_operands__gen_asic_regfile__DOT__i_ariane_regfile;
        regfile.mem[22] = (uint32_t)(val & 0xFFFFFFFF);
        regfile.mem[23] = (uint32_t)((val >> 32) & 0xFFFFFFFF);
    }
    void
    set_noc_resp_r_data_i(uint64_t val) override
    {
        core->noc_resp_r_data_i = val;
    }

    // Outputs from core
    uint8_t
    get_noc_req_w_last_o() override
    {
        return core->noc_req_w_last_o;
    }
    uint8_t
    get_noc_req_aw_valid_o() override
    {
        return core->noc_req_aw_valid_o;
    }
    uint8_t
    get_noc_req_aw_id_o() override
    {
        return core->noc_req_aw_id_o;
    }
    uint8_t
    get_noc_req_aw_len_o() override
    {
        return core->noc_req_aw_len_o;
    }
    uint8_t
    get_noc_req_aw_size_o() override
    {
        return core->noc_req_aw_size_o;
    }
    uint8_t
    get_noc_req_w_valid_o() override
    {
        return core->noc_req_w_valid_o;
    }
    uint8_t
    get_noc_req_b_ready_o() override
    {
        return core->noc_req_b_ready_o;
    }
    uint8_t
    get_noc_req_ar_valid_o() override
    {
        return core->noc_req_ar_valid_o;
    }
    uint8_t
    get_noc_req_ar_id_o() override
    {
        return core->noc_req_ar_id_o;
    }
    uint8_t
    get_noc_req_ar_len_o() override
    {
        return core->noc_req_ar_len_o;
    }
    uint8_t
    get_noc_req_ar_size_o() override
    {
        return core->noc_req_ar_size_o;
    }
    uint8_t
    get_noc_req_ar_prot_o() override
    {
        return core->noc_req_ar_prot_o;
    }
    uint8_t
    get_noc_req_r_ready_o() override
    {
        return core->noc_req_r_ready_o;
    }
    uint64_t
    get_noc_req_aw_addr_o() override
    {
        return core->noc_req_aw_addr_o;
    }
    uint64_t
    get_noc_req_w_data_o() override
    {
        return core->noc_req_w_data_o;
    }
    uint64_t
    get_noc_req_ar_addr_o() override
    {
        return core->noc_req_ar_addr_o;
    }
    uint8_t
    get_ebreak_o() override
    {
        return core->ebreak_o;
    }
    uint8_t
    get_illegal_instr_o() override
    {
        return core->illegal_instr_o;
    }
    [[deprecated]]
    uint64_t
    get_illegal_instr_pc_o() override
    {
        return core->program_counter;
    }
    uint64_t
    get_pc_o() override
    {
        //return core->rootp->vlSymsp->
        // TOP__cva6_top__i_ariane__i_cva6__i_frontend.npc_q;
        return core->program_counter;
    }

    // Control
    void
    eval() override
    {
        core->eval();
    }

    // Tracing
    void
    setup_trace(const std::string &trace_file) override
    {
#if VM_TRACE
        Verilated::traceEverOn(true);
        tfp = new VerilatedVcdC;
        core->trace(tfp, 99);
        tfp->open(trace_file.c_str());
#endif
    }

    void
    dump_trace(uint64_t time) override
    {
#if VM_TRACE
        if (tfp) {
            tfp->dump(time);
        }
#endif
    }

    void
    close_trace() override
    {
#if VM_TRACE
        if (tfp) {
            tfp->close();
            delete tfp;
            tfp = nullptr;
        }
#endif
    }
};

} // namespace gem5

extern "C" {
gem5::CVA6RtlCoreInterface *
create_core()
{
    return new gem5::CVA6RtlCoreImpl();
}
void
destroy_core(gem5::CVA6RtlCoreInterface *core)
{
    delete core;
}
}
