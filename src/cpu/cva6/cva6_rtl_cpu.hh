#ifndef __CPU_CVA6_CVA6_RTL_CPU_HH__
#define __CPU_CVA6_CVA6_RTL_CPU_HH__

#include <vector>

#include "base/statistics.hh"
#include "cpu/cva6/cva6_rtl_core_interface.hh"
#include "mem/port.hh"
#include "cpu/base.hh"
#include "cpu/simple_thread.hh"
#include "params/CVA6RtlCPU.hh"

namespace gem5
{

class CVA6RtlCPU : public BaseCPU
{
  private:
    class CpuPort : public RequestPort
    {
      private:
        CVA6RtlCPU *cpu;
      public:
        CpuPort(const std::string& name, CVA6RtlCPU *cpu) :
            RequestPort(name), cpu(cpu)
        { }
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
    };

    CpuPort instPort;
    CpuPort dataPort;

    CVA6RtlCoreInterface *core;
    void *libHandle;
    destroy_core_t destroyCorePointer;

    uint64_t cycleCount;
    bool resetDone;

    // AXI state buffers for co-simulation
    bool ar_busy;
    bool r_data_ready;
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
    bool write_resp_pending;

    void tick();
    EventFunctionWrapper tickEvent;

    RequestorID requestorId;

    void handleTimingResp(PacketPtr pkt, CpuPort *port);
    void handleReqRetry(CpuPort *port);

    PacketPtr retryPkt;
    CpuPort *retryPort;
    uint32_t pendingWriteResponses;
    bool writeBurstFinished;

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
    CVA6RtlCPU(const CVA6RtlCPUParams &params);
    ~CVA6RtlCPU();

    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;

    void startup() override;

    // BaseCPU virtual overrides
    Port &getDataPort() override { return dataPort; }
    Port &getInstPort() override { return instPort; }
    void wakeup(ThreadID tid) override {}
    Counter totalInsts() const override { return cycleCount; }
    Counter totalOps() const override { return cycleCount; }
};

} // namespace gem5

#endif // __CPU_CVA6_CVA6_RTL_CPU_HH__
