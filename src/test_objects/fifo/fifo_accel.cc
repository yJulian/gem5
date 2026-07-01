// Copyright 2026 Antigravity
// Description: C++ implementation for FifoAccelerator gem5 SimObject.

#include "test_objects/fifo/fifo_accel.hh"

#include <dlfcn.h>

#include <cstring>
#include <iostream>

#include "base/logging.hh"
#include "sim/system.hh"

namespace gem5
{

FifoAccelerator::FifoAccelerator(const Params &params)
    : DmaDevice(params),
      pioAddr(params.pio_addr),
      pioSize(params.pio_size),
      pioDelay(params.pio_latency),
      accel(nullptr),
      libHandle(nullptr),
      destroyAccelPointer(nullptr),
      cycleCount(0),
      resetDone(false),
      dma_read_pending(false),
      dma_read_data_buf(0),
      dma_write_pending(false),
      tickEvent([this] { tick(); }, name() + ".tick"),
      stats(this)
{
    // Load RTL Shared Library using dlopen
    libHandle = dlopen(params.accel_library.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!libHandle) {
        fatal("Failed to load RTL library '%s': %s\n", params.accel_library,
              dlerror());
    }

    create_accel_t create_accel =
        (create_accel_t)dlsym(libHandle, "create_accel");
    destroyAccelPointer = (destroy_accel_t)dlsym(libHandle, "destroy_accel");

    const char *dlsym_error = dlerror();
    if (dlsym_error || !create_accel || !destroyAccelPointer) {
        fatal(
            "Cannot load symbol 'create_accel' or 'destroy_accel' from '%s': "
            "%s\n",
            params.accel_library, dlsym_error ? dlsym_error : "unknown error");
    }

    // Instantiate Verilated FIFO Accelerator from shared library
    accel = create_accel();

    if (params.trace_enable) {
        accel->setup_trace(params.trace_file);
    }

    // Initialize inputs
    accel->set_clk_i(0);
    accel->set_rst_ni(0);

    accel->set_s_axi_awaddr(0);
    accel->set_s_axi_awvalid(0);
    accel->set_s_axi_wdata(0);
    accel->set_s_axi_wstrb(0);
    accel->set_s_axi_wvalid(0);
    accel->set_s_axi_bready(0);

    accel->set_s_axi_araddr(0);
    accel->set_s_axi_arvalid(0);
    accel->set_s_axi_rready(0);

    accel->set_m_axi_awready(0);
    accel->set_m_axi_wready(0);
    accel->set_m_axi_bvalid(0);
    accel->set_m_axi_bresp(0);

    accel->set_m_axi_arready(0);
    accel->set_m_axi_rdata(0);
    accel->set_m_axi_rresp(0);
    accel->set_m_axi_rlast(0);
    accel->set_m_axi_rvalid(0);

    accel->eval();

    requestorId = params.system->getRequestorId(this);
}

FifoAccelerator::~FifoAccelerator()
{
    if (accel) {
        accel->close_trace();
        if (destroyAccelPointer) {
            destroyAccelPointer(accel);
        }
    }
    if (libHandle) {
        dlclose(libHandle);
    }
}

FifoAccelerator::AccelStats::AccelStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(numCycles, statistics::units::Cycle::get(),
               "Number of Accelerator cycles simulated"),
      ADD_STAT(numSlaveReads, statistics::units::Count::get(),
               "Number of AXI slave MMIO read requests completed"),
      ADD_STAT(numSlaveWrites, statistics::units::Count::get(),
               "Number of AXI slave MMIO write requests completed"),
      ADD_STAT(numMasterReads, statistics::units::Count::get(),
               "Number of AXI master DMA read requests started"),
      ADD_STAT(numMasterWrites, statistics::units::Count::get(),
               "Number of AXI master DMA write requests started")
{}

void
FifoAccelerator::startup()
{
    // Schedule first tick event
    schedule(tickEvent, clockEdge(Cycles(1)));
}

AddrRangeList
FifoAccelerator::getAddrRanges() const
{
    AddrRangeList ranges = {RangeSize(pioAddr, pioSize)};
    return ranges;
}

void
FifoAccelerator::performDmaRead(Addr addr, uint8_t *data, size_t size)
{
    RequestPtr req = std::make_shared<Request>(addr, size, 0, requestorId);
    PacketPtr pkt = Packet::createRead(req);
    pkt->allocate();
    dmaPort.sendFunctional(pkt);
    std::memcpy(data, pkt->getPtr<uint8_t>(), size);
    std::cout << "[GEM5 DMA READ] addr=0x" << std::hex << addr << " data=0x"
              << *(uint64_t *)data << std::dec << std::endl;
    delete pkt;
}

void
FifoAccelerator::performDmaWrite(Addr addr, const uint8_t *data, size_t size)
{
    RequestPtr req = std::make_shared<Request>(addr, size, 0, requestorId);
    PacketPtr pkt = Packet::createWrite(req);
    pkt->allocate();
    std::memcpy(pkt->getPtr<uint8_t>(), data, size);
    dmaPort.sendFunctional(pkt);
    std::cout << "[GEM5 DMA WRITE] addr=0x" << std::hex << addr << " data=0x"
              << *(uint64_t *)data << std::dec << std::endl;
    delete pkt;
}

Tick
FifoAccelerator::read(PacketPtr pkt)
{
    stats.numSlaveReads++;
    Addr addr = pkt->getAddr() - pioAddr;

    // Phase 1: Address Handshake
    accel->set_s_axi_araddr(addr);
    accel->set_s_axi_arvalid(1);
    accel->set_s_axi_rready(0);
    accel->eval();

    // Toggle clock until address is captured
    stepClock();

    accel->set_s_axi_arvalid(0);
    accel->set_s_axi_rready(1);
    accel->eval();

    // Toggle clock until RVALID is asserted
    int timeout = 1000;
    while (!accel->get_s_axi_rvalid() && timeout-- > 0) {
        stepClock();
    }

    uint64_t data = accel->get_s_axi_rdata();
    pkt->setUintX(data, ByteOrder::little);

    // Toggle clock to finish the handshake (clears rvalid)
    stepClock();

    accel->set_s_axi_rready(0);
    accel->eval();

    pkt->makeResponse();
    return pioDelay;
}

Tick
FifoAccelerator::write(PacketPtr pkt)
{
    stats.numSlaveWrites++;
    Addr addr = pkt->getAddr() - pioAddr;
    uint64_t data = pkt->getUintX(ByteOrder::little);

    // Phase 1: AW and W handshake
    accel->set_s_axi_awaddr(addr);
    accel->set_s_axi_awvalid(1);
    accel->set_s_axi_wdata(data);
    accel->set_s_axi_wstrb(0xFF);
    accel->set_s_axi_wvalid(1);
    accel->set_s_axi_bready(0);
    accel->eval();

    // Toggle clock until awready and wready are asserted (usually immediately
    // in Verilator)
    int timeout = 1000;
    while ((!accel->get_s_axi_awready() || !accel->get_s_axi_wready()) &&
           timeout-- > 0) {
        stepClock();
    }

    // Toggle clock to capture address and data
    stepClock();

    accel->set_s_axi_awvalid(0);
    accel->set_s_axi_wvalid(0);
    accel->set_s_axi_bready(1);
    accel->eval();

    // Toggle clock until bvalid is asserted
    timeout = 1000;
    while (!accel->get_s_axi_bvalid() && timeout-- > 0) {
        stepClock();
    }

    // Toggle clock to finish the handshake
    stepClock();

    accel->set_s_axi_bready(0);
    accel->eval();

    pkt->makeResponse();
    return pioDelay;
}

void
FifoAccelerator::stepClock()
{
    cycleCount++;
    stats.numCycles++;

    // 1. Falling Edge
    accel->set_clk_i(0);
    accel->eval();

    bool r_handshake_done = false;
    bool b_handshake_done = false;

    if (resetDone) {
        // AR Handshake logic (Accept request)
        if (accel->get_m_axi_arvalid() && !dma_read_pending) {
            stats.numMasterReads++;
            Addr addr = accel->get_m_axi_araddr();
            dma_read_data_buf = 0;
            performDmaRead(addr, (uint8_t *)&dma_read_data_buf, 8);
            dma_read_pending = true;
            accel->set_m_axi_arready(1);
        } else {
            accel->set_m_axi_arready(0);
        }

        // AW and W Handshake logic (Accept request)
        if (accel->get_m_axi_awvalid() && accel->get_m_axi_wvalid() &&
            !dma_write_pending) {
            stats.numMasterWrites++;
            Addr addr = accel->get_m_axi_awaddr();
            uint64_t data = accel->get_m_axi_wdata();
            performDmaWrite(addr, (const uint8_t *)&data, 8);
            dma_write_pending = true;
            accel->set_m_axi_awready(1);
            accel->set_m_axi_wready(1);
        } else {
            accel->set_m_axi_awready(0);
            accel->set_m_axi_wready(0);
        }

        // Drive response channels
        if (dma_read_pending) {
            accel->set_m_axi_rdata(dma_read_data_buf);
            accel->set_m_axi_rlast(1);
            accel->set_m_axi_rresp(0);
            accel->set_m_axi_rvalid(1);
        } else {
            accel->set_m_axi_rvalid(0);
            accel->set_m_axi_rlast(0);
            accel->set_m_axi_rdata(0);
            accel->set_m_axi_rresp(0);
        }

        if (dma_write_pending) {
            accel->set_m_axi_bresp(0);
            accel->set_m_axi_bvalid(1);
        } else {
            accel->set_m_axi_bvalid(0);
            accel->set_m_axi_bresp(0);
        }

        accel->eval();

        // Check if handshakes will complete on the upcoming rising edge
        r_handshake_done = dma_read_pending && accel->get_m_axi_rready();
        b_handshake_done = dma_write_pending && accel->get_m_axi_bready();
    } else {
        // Inputs during reset
        accel->set_m_axi_arready(0);
        accel->set_m_axi_rvalid(0);
        accel->set_m_axi_rlast(0);
        accel->set_m_axi_rdata(0);
        accel->set_m_axi_rresp(0);
        accel->set_m_axi_awready(0);
        accel->set_m_axi_wready(0);
        accel->set_m_axi_bvalid(0);
        accel->set_m_axi_bresp(0);
        accel->eval();
    }

    if (params().trace_enable) {
        accel->dump_trace(cycleCount * 10);
    }

    // 2. Rising Edge
    accel->set_clk_i(1);
    accel->eval();

    if (params().trace_enable) {
        accel->dump_trace(cycleCount * 10 + 5);
    }

    if (resetDone) {
        if (r_handshake_done) {
            dma_read_pending = false;
        }
        if (b_handshake_done) {
            dma_write_pending = false;
        }
    }
}

void
FifoAccelerator::tick()
{
    // Handle reset phase at start of simulation
    if (cycleCount < 10) {
        accel->set_rst_ni(0);
        resetDone = false;
        dma_read_pending = false;
        dma_write_pending = false;
    } else {
        accel->set_rst_ni(1);
        resetDone = true;
    }

    stepClock();

    // Schedule next cycle
    schedule(tickEvent, clockEdge(Cycles(1)));
}

} // namespace gem5
