// Copyright 2026 Antigravity
// Description: Abstract interface for the Verilated FIFO Accelerator.

#ifndef __TEST_OBJECTS_FIFO_FIFO_ACCEL_INTERFACE_HH__
#define __TEST_OBJECTS_FIFO_FIFO_ACCEL_INTERFACE_HH__

#include <cstdint>
#include <string>

namespace gem5
{

class FifoAccelInterface
{
  public:
    virtual ~FifoAccelInterface() {}

    // Inputs to accelerator clock/reset
    virtual void set_clk_i(uint8_t val) = 0;
    virtual void set_rst_ni(uint8_t val) = 0;

    // AXI4-Lite Slave Interface (MMIO Registers)
    virtual void set_s_axi_awaddr(uint32_t val) = 0;
    virtual void set_s_axi_awvalid(uint8_t val) = 0;
    virtual uint8_t get_s_axi_awready() = 0;

    virtual void set_s_axi_wdata(uint64_t val) = 0;
    virtual void set_s_axi_wstrb(uint8_t val) = 0;
    virtual void set_s_axi_wvalid(uint8_t val) = 0;
    virtual uint8_t get_s_axi_wready() = 0;

    virtual uint8_t get_s_axi_bresp() = 0;
    virtual uint8_t get_s_axi_bvalid() = 0;
    virtual void set_s_axi_bready(uint8_t val) = 0;

    virtual void set_s_axi_araddr(uint32_t val) = 0;
    virtual void set_s_axi_arvalid(uint8_t val) = 0;
    virtual uint8_t get_s_axi_arready() = 0;

    virtual uint64_t get_s_axi_rdata() = 0;
    virtual uint8_t get_s_axi_rresp() = 0;
    virtual uint8_t get_s_axi_rvalid() = 0;
    virtual void set_s_axi_rready(uint8_t val) = 0;

    // AXI4 Master Interface (DMA Read and Write)
    virtual uint64_t get_m_axi_awaddr() = 0;
    virtual uint8_t get_m_axi_awlen() = 0;
    virtual uint8_t get_m_axi_awsize() = 0;
    virtual uint8_t get_m_axi_awburst() = 0;
    virtual uint8_t get_m_axi_awvalid() = 0;
    virtual void set_m_axi_awready(uint8_t val) = 0;

    virtual uint64_t get_m_axi_wdata() = 0;
    virtual uint8_t get_m_axi_wstrb() = 0;
    virtual uint8_t get_m_axi_wlast() = 0;
    virtual uint8_t get_m_axi_wvalid() = 0;
    virtual void set_m_axi_wready(uint8_t val) = 0;

    virtual void set_m_axi_bresp(uint8_t val) = 0;
    virtual void set_m_axi_bvalid(uint8_t val) = 0;
    virtual uint8_t get_m_axi_bready() = 0;

    virtual uint64_t get_m_axi_araddr() = 0;
    virtual uint8_t get_m_axi_arlen() = 0;
    virtual uint8_t get_m_axi_arsize() = 0;
    virtual uint8_t get_m_axi_arburst() = 0;
    virtual uint8_t get_m_axi_arvalid() = 0;
    virtual void set_m_axi_arready(uint8_t val) = 0;

    virtual void set_m_axi_rdata(uint64_t val) = 0;
    virtual void set_m_axi_rresp(uint8_t val) = 0;
    virtual void set_m_axi_rlast(uint8_t val) = 0;
    virtual void set_m_axi_rvalid(uint8_t val) = 0;
    virtual uint8_t get_m_axi_rready() = 0;

    // Status/DMA Flags
    virtual uint8_t get_dma_busy_o() = 0;
    virtual uint8_t get_dma_done_o() = 0;

    // Control
    virtual void eval() = 0;

    // Tracing
    virtual void setup_trace(const std::string &trace_file) = 0;
    virtual void dump_trace(uint64_t time) = 0;
    virtual void close_trace() = 0;
};

} // namespace gem5

// DLL factory function type signatures
typedef gem5::FifoAccelInterface *(*create_accel_t)();
typedef void (*destroy_accel_t)(gem5::FifoAccelInterface *);

#endif // __CPU_CVA6_FIFO_ACCEL_INTERFACE_HH__
