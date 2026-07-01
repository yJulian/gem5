// Copyright 2026 Antigravity
// Description: General SRAM/Native Memory Interface class declaration.

#ifndef __CPU_RTL_SRAM_MEM_IFACE_SRAM_HH__
#define __CPU_RTL_SRAM_MEM_IFACE_SRAM_HH__

#include "cpu/rtl/mem_iface_base.hh"

namespace gem5
{

class RtlMemIfaceSram : public RtlMemIfaceBase
{
  protected:
    // SRAM protocol states
    bool pending_read;
    bool pending_write;
    uint64_t read_data;
    bool read_data_ready;
    bool write_done;

    // Pure virtual methods to connect with physical RTL signals on a core wrapper (e.g. PicoRV32)
    // Request Channel (Core -> Mem)
    virtual uint8_t get_req_valid() = 0;
    virtual uint64_t get_req_addr() = 0;
    virtual uint8_t get_req_write() = 0;
    virtual uint64_t get_req_wdata() = 0;
    virtual uint8_t get_req_wstrb() = 0;

    // Response Channel (Mem -> Core)
    virtual void set_resp_ready(uint8_t ready) = 0;
    virtual void set_resp_valid(uint8_t valid) = 0;
    virtual void set_resp_rdata(uint64_t rdata) = 0;

  public:
    RtlMemIfaceSram(RtlCpuHelper *_helper);
    virtual ~RtlMemIfaceSram() = default;

    void driveInputs() override;
    void sampleOutputs() override;
    void acceptResp(PacketPtr pkt) override;
};

// A dummy implementation of the core side of the SRAM interface (PicoRV32 stub)
// so that this module can be compiled and instantiated without a real RTL model.
class DummySramCoreInterface
{
  public:
    uint8_t req_valid = 0;
    uint64_t req_addr = 0;
    uint8_t req_write = 0;
    uint64_t req_wdata = 0;
    uint8_t req_wstrb = 0;

    uint8_t resp_ready = 0;
    uint8_t resp_valid = 0;
    uint64_t resp_rdata = 0;
};

class PicoRV32MemIfaceSram : public RtlMemIfaceSram
{
  private:
    DummySramCoreInterface *dummyCore;

  protected:
    uint8_t get_req_valid() override { return dummyCore->req_valid; }
    uint64_t get_req_addr() override { return dummyCore->req_addr; }
    uint8_t get_req_write() override { return dummyCore->req_write; }
    uint64_t get_req_wdata() override { return dummyCore->req_wdata; }
    uint8_t get_req_wstrb() override { return dummyCore->req_wstrb; }

    void set_resp_ready(uint8_t ready) override { dummyCore->resp_ready = ready; }
    void set_resp_valid(uint8_t valid) override { dummyCore->resp_valid = valid; }
    void set_resp_rdata(uint64_t rdata) override { dummyCore->resp_rdata = rdata; }

  public:
    PicoRV32MemIfaceSram(RtlCpuHelper *_helper, DummySramCoreInterface *_dummyCore)
        : RtlMemIfaceSram(_helper), dummyCore(_dummyCore)
    {}
    virtual ~PicoRV32MemIfaceSram() = default;
};

} // namespace gem5

#endif // __CPU_RTL_SRAM_MEM_IFACE_SRAM_HH__
