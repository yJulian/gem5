// Copyright 2026 Antigravity
// Description: Header file for FifoAccelerator gem5 SimObject.

#ifndef __TEST_OBJECTS_FIFO_FIFO_ACCEL_HH__
#define __TEST_OBJECTS_FIFO_FIFO_ACCEL_HH__

#include <string>

#include "base/statistics.hh"
#include "dev/dma_device.hh"
#include "params/FifoAccelerator.hh"
#include "test_objects/fifo/fifo_accel_interface.hh"

namespace gem5
{

class FifoAccelerator : public DmaDevice
{
  private:
    Addr pioAddr;
    Addr pioSize;
    Tick pioDelay;

    FifoAccelInterface *accel;
    void *libHandle;
    destroy_accel_t destroyAccelPointer;

    uint64_t cycleCount;
    bool resetDone;

    bool dma_read_pending;
    uint64_t dma_read_data_buf;
    bool dma_write_pending;

    EventFunctionWrapper tickEvent;
    void tick();
    void stepClock();

    RequestorID requestorId;

    struct AccelStats : public statistics::Group
    {
        AccelStats(statistics::Group *parent);

        statistics::Scalar numCycles;
        statistics::Scalar numSlaveReads;
        statistics::Scalar numSlaveWrites;
        statistics::Scalar numMasterReads;
        statistics::Scalar numMasterWrites;
    } stats;

    void performDmaRead(Addr addr, uint8_t *data, size_t size);
    void performDmaWrite(Addr addr, const uint8_t *data, size_t size);

  public:
    PARAMS(FifoAccelerator);
    FifoAccelerator(const Params &params);
    ~FifoAccelerator();

    AddrRangeList getAddrRanges() const override;
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    void startup() override;
};

} // namespace gem5

#endif // __CPU_CVA6_FIFO_ACCEL_HH__
