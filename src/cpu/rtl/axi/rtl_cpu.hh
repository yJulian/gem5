// Copyright 2026 Antigravity
// Description: Core-agnostic CPU SimObject for RTL co-simulation.

#ifndef __CPU_RTL_AXI_RTL_CPU_HH__
#define __CPU_RTL_AXI_RTL_CPU_HH__

#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "base/statistics.hh"
#include "cpu/base.hh"
#include "cpu/rtl/axi/rtl_core_interface.hh"
#include "cpu/rtl/mem_iface_base.hh"
#include "cpu/simple_thread.hh"
#include "mem/port.hh"
#include "params/RtlCPU.hh"

namespace gem5
{

class RtlCPU : public BaseCPU, public RtlCpuHelper
{
  private:
    class CpuPort : public RequestPort
    {
      private:
        RtlCPU *cpu;

      public:
        CpuPort(const std::string &name, RtlCPU *cpu)
            : RequestPort(name), cpu(cpu)
        {}

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
    };

    CpuPort instPort;
    CpuPort dataPort;

    RtlCoreInterface *core;
    void *libHandle;
    destroy_core_t destroyCorePointer;

    uint64_t cycleCount;
    bool resetDone;

    std::unique_ptr<RtlMemIfaceBase> memIface;

    void tick();
    EventFunctionWrapper tickEvent;

    RequestorID requestorId;

    void handleTimingResp(PacketPtr pkt, CpuPort *port);
    void handleReqRetry(CpuPort *port);

    PacketPtr retryPkt;
    CpuPort *retryPort;
    uint32_t pendingWriteResponses;

    struct CPUStats : public statistics::Group
    {
        CPUStats(statistics::Group *parent);

        // RTL/Simulation Stats
        statistics::Scalar numCycles;
        statistics::Scalar numIllegalInst;
        statistics::Scalar numEbreak;

        // AXI Request Stats
        statistics::Scalar numReadReqs;
        statistics::Scalar numReadReqsInst;
        statistics::Scalar numReadReqsData;
        statistics::Scalar numWriteReqs;

        // AXI Beat Stats
        statistics::Scalar numReadBeats;
        statistics::Scalar numWriteBeats;
        statistics::Scalar numWriteResps;

        // AXI Retry/Stall Stats
        statistics::Scalar numInstPortRetries;
        statistics::Scalar numDataPortRetries;

        // AXI Breakdowns by Size (Vector stats)
        statistics::Vector readReqSizes;
        statistics::Vector writeReqSizes;

        // Formulas
        statistics::Formula avgReadBurstLen;
        statistics::Formula avgWriteBurstLen;
    } stats;

  public:
    RtlCPU(const RtlCPUParams &params);
    ~RtlCPU();

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    void startup() override;

    // BaseCPU virtual overrides
    Port &
    getDataPort() override
    {
        return dataPort;
    }
    Port &
    getInstPort() override
    {
        return instPort;
    }
    void
    wakeup(ThreadID tid) override
    {}
    Counter
    totalInsts() const override
    {
        return cycleCount;
    }
    Counter
    totalOps() const override
    {
        return cycleCount;
    }

    // RtlCpuHelper virtual overrides
    bool sendTimingReq(PacketPtr pkt, bool is_inst) override;
    RequestorID
    getRequestorId() const override
    {
        return requestorId;
    }
    void recordReadReq(bool is_inst, uint32_t size) override;
    void recordWriteReq(uint32_t size) override;
    void recordReadBeat() override;
    void recordWriteBeat() override;
    void recordWriteResp() override;
    void exitSimulation(const std::string &reason) override;
    bool
    isResetDone() const override
    {
        return resetDone;
    }
    uint64_t
    getCycleCount() const override
    {
        return cycleCount;
    }
    bool
    isRetryPending() const override
    {
        return retryPkt != nullptr;
    }
    void writePhysMem(Addr addr, const uint8_t *data, size_t size) override;
};

} // namespace gem5

#endif // __CPU_RTL_AXI_RTL_CPU_HH__
