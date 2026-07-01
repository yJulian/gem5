// Copyright 2026 Antigravity
// Description: Abstract base classes for the RTL CPU memory interfaces.

#ifndef __CPU_RTL_MEM_IFACE_BASE_HH__
#define __CPU_RTL_MEM_IFACE_BASE_HH__

#include <string>
#include "mem/packet.hh"

namespace gem5
{

class RtlCpuHelper
{
  public:
    virtual ~RtlCpuHelper() = default;

    // Send timing request to gem5 memory system. Returns true if successful.
    virtual bool sendTimingReq(PacketPtr pkt, bool is_inst) = 0;

    // Get gem5 requestor ID
    virtual RequestorID getRequestorId() const = 0;

    // Statistics recording
    virtual void recordReadReq(bool is_inst, uint32_t size) = 0;
    virtual void recordWriteReq(uint32_t size) = 0;
    virtual void recordReadBeat() = 0;
    virtual void recordWriteBeat() = 0;
    virtual void recordWriteResp() = 0;

    // Control and helper status
    virtual void exitSimulation(const std::string &reason) = 0;
    virtual bool isResetDone() const = 0;
    virtual uint64_t getCycleCount() const = 0;
    virtual bool isRetryPending() const = 0;

    // Synchronous (functional) write directly to physical memory backing store.
    // Use for tohost writes: bypasses timing so physProxy.read() reflects the
    // value immediately, even if ebreak fires in the same RTL cycle.
    virtual void writePhysMem(Addr addr, const uint8_t *data, size_t size) = 0;
};

class RtlMemIfaceBase
{
  protected:
    RtlCpuHelper *helper;

  public:
    RtlMemIfaceBase(RtlCpuHelper *_helper) : helper(_helper) {}
    virtual ~RtlMemIfaceBase() = default;

    // Drive inputs to the RTL core
    virtual void driveInputs() = 0;

    // Sample outputs from the RTL core and perform state machine updates
    virtual void sampleOutputs() = 0;

    // Handle incoming timing response from gem5 memory system
    virtual void acceptResp(PacketPtr pkt) = 0;
};

} // namespace gem5

#endif // __CPU_RTL_MEM_IFACE_BASE_HH__
