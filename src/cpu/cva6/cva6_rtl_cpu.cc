#include "cpu/cva6/cva6_rtl_cpu.hh"
#include "arch/riscv/interrupts.hh"
#include "arch/riscv/regs/int.hh"
#include "cpu/simple_thread.hh"
#include "cpu/rtl/axi/cva6/cva6_mem_iface_axi.hh"
#include "mem/port_proxy.hh"

// #ifndef DEBUG_CVA
// #define DEBUG_CVA 0
// #endif

#include <dlfcn.h>

#include <cstring>
#include <iostream>

#include "base/logging.hh"
#include "base/str.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

#define DEBUG_CVA 1

namespace gem5
{

CVA6RtlCPU::CVA6RtlCPU(const CVA6RtlCPUParams &params)
    : BaseCPU(params),
      instPort(params.name + ".inst_port", this),
      dataPort(params.name + ".data_port", this),
      cycleCount(0),
      resetDone(false),
      tickEvent([this] { tick(); }, name() + ".tick"),
      core(nullptr),
      libHandle(nullptr),
      destroyCorePointer(nullptr),
      retryPkt(nullptr),
      retryPort(nullptr),
      pendingWriteResponses(0),
      stats(this)
{
    // Load RTL Shared Library using dlopen
    libHandle = dlopen(params.rtl_library.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!libHandle) {
        fatal("Failed to load RTL library '%s': %s\n", params.rtl_library,
              dlerror());
    }

    create_core_t create_core = (create_core_t)dlsym(libHandle, "create_core");
    destroyCorePointer = (destroy_core_t)dlsym(libHandle, "destroy_core");

    const char *dlsym_error = dlerror();
    if (dlsym_error || !create_core || !destroyCorePointer) {
        fatal("Cannot load symbol 'create_core' or 'destroy_core' from '%s': "
              "%s\n",
              params.rtl_library, dlsym_error ? dlsym_error : "unknown error");
    }

    // Instantiate Verilated CVA6 model from shared library
    core = create_core();

    memIface = std::make_unique<CVA6MemIfaceAxi>(this, core);

    if (params.trace_enable) {
        core->setup_trace(params.trace_file);
#if DEBUG_CVA
        std::cout
            << "[CVA6 CPU] Tracing enabled (delegated to library), writing to "
            << params.trace_file << std::endl;
#endif
    }

    // Initialize inputs
    core->set_clk_i(0);
    core->set_rst_ni(0);
    core->set_boot_addr_i(0x80000000);
    core->set_hart_id_i(0);
    core->set_irq_i(0);
    core->set_ipi_i(0);
    core->set_time_irq_i(0);
    core->set_debug_req_i(0);
    core->eval();

    requestorId = params.system->getRequestorId(this);

    // Create SimpleThread for the single hardware thread context
    SimpleThread *thread = new SimpleThread(this, 0, params.system, params.mmu, params.isa[0], params.decoder[0]);
    threadContexts.push_back(thread->getTC());
}

CVA6RtlCPU::~CVA6RtlCPU()
{
    if (core) {
        core->close_trace();
        if (destroyCorePointer) {
            destroyCorePointer(core);
        }
    }
    if (libHandle) {
        dlclose(libHandle);
    }
}

CVA6RtlCPU::CPUStats::CPUStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(numCycles, statistics::units::Cycle::get(),
               "Number of CPU cycles simulated"),
      ADD_STAT(numIllegalInst, statistics::units::Count::get(),
               "Number of illegal instruction exception commits"),
      ADD_STAT(numEbreak, statistics::units::Count::get(),
               "Number of ebreak instruction commits"),
      ADD_STAT(numReadReqs, statistics::units::Count::get(),
               "Total AXI read requests (AR handshakes) completed"),
      ADD_STAT(numReadReqsInst, statistics::units::Count::get(),
               "Number of AXI instruction read requests completed"),
      ADD_STAT(numReadReqsData, statistics::units::Count::get(),
               "Number of AXI data read requests completed"),
      ADD_STAT(numWriteReqs, statistics::units::Count::get(),
               "Total AXI write requests (AW handshakes) completed"),
      ADD_STAT(numReadBeats, statistics::units::Count::get(),
               "Total read beats received (R handshakes)"),
      ADD_STAT(numWriteBeats, statistics::units::Count::get(),
               "Total write beats sent (W handshakes)"),
      ADD_STAT(numWriteResps, statistics::units::Count::get(),
               "Total write responses received (B handshakes)"),
      ADD_STAT(numInstPortRetries, statistics::units::Count::get(),
               "Number of instruction port timing request retries"),
      ADD_STAT(numDataPortRetries, statistics::units::Count::get(),
               "Number of data port timing request retries"),
      ADD_STAT(readReqSizes, statistics::units::Count::get(),
               "Breakdown of read requests by transaction size in bytes"),
      ADD_STAT(writeReqSizes, statistics::units::Count::get(),
               "Breakdown of write requests by transaction size in bytes"),
      ADD_STAT(avgReadBurstLen,
               statistics::units::Rate<statistics::units::Count,
                                       statistics::units::Count>::get(),
               "Average read burst length (beats per request)",
               numReadBeats / numReadReqs),
      ADD_STAT(avgWriteBurstLen,
               statistics::units::Rate<statistics::units::Count,
                                       statistics::units::Count>::get(),
               "Average write burst length (beats per request)",
               numWriteBeats / numWriteReqs)
{
    readReqSizes
        .init(8) // indices 0 to 7 represent 1 << index bytes (1 to 128 bytes)
        .flags(statistics::total | statistics::pdf | statistics::nozero);
    writeReqSizes.init(8).flags(statistics::total | statistics::pdf |
                                statistics::nozero);

    for (int i = 0; i < 8; ++i) {
        std::string size_str = std::to_string(1 << i) + "B";
        readReqSizes.subname(i, size_str);
        writeReqSizes.subname(i, size_str);
    }
}

bool
CVA6RtlCPU::CpuPort::recvTimingResp(PacketPtr pkt)
{
    cpu->handleTimingResp(pkt, this);
    return true;
}

void
CVA6RtlCPU::CpuPort::recvReqRetry()
{
    cpu->handleReqRetry(this);
}

void
CVA6RtlCPU::handleTimingResp(PacketPtr pkt, CpuPort *port)
{
    if (pkt->isWrite()) {
        if (pendingWriteResponses > 0) {
            pendingWriteResponses--;
        }
    }
    memIface->acceptResp(pkt);
}

void
CVA6RtlCPU::handleReqRetry(CpuPort *port)
{
    if (retryPkt && retryPort == port) {
        PacketPtr pkt = retryPkt;
        retryPkt = nullptr;
        retryPort = nullptr;
        if (!port->sendTimingReq(pkt)) {
            retryPkt = pkt;
            retryPort = port;
            if (port == &instPort) {
                stats.numInstPortRetries++;
            } else {
                stats.numDataPortRetries++;
            }
        }
    }
}

Port &
CVA6RtlCPU::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "inst_port") {
        return instPort;
    } else if (if_name == "data_port") {
        return dataPort;
    } else {
        return BaseCPU::getPort(if_name, idx);
    }
}

void
CVA6RtlCPU::startup()
{
    // Schedule first tick event
    schedule(tickEvent, clockEdge(Cycles(1)));
}

void
CVA6RtlCPU::tick()
{
    cycleCount++;
    stats.numCycles++;

#if DEBUG_CVA
    if (cycleCount % 100000 == 0) {
        std::cout << "[CVA6 CPU] Simulated clock cycles: " << std::dec
                  << cycleCount << " PC=0x" << std::hex << core->get_pc_o()
                  << std::dec << std::endl;
    }
#endif

    // 1. Reset logic
    if (cycleCount < 10) {
        core->set_rst_ni(0);
        resetDone = false;
    } else {
        core->set_rst_ni(1);
        resetDone = true;
    }

    if (cycleCount <= 10) {
        // Read DTB address from thread context and pass to Verilator's register file
        uint64_t init_a1 = threadContexts[0]->getReg(RiscvISA::int_reg::A1);
        if (init_a1 == 0) {
            init_a1 = 0x87E00000;
        }
#if DEBUG_CVA
        std::cout << "[CVA6 CPU DEBUG] Cycle=" << std::dec << cycleCount
                  << " init_a1=0x" << std::hex << init_a1 << std::endl;
#endif
        core->set_init_a1_i(init_a1);
    }

    // 2. Falling Edge & Setup Inputs
    core->set_clk_i(0);
    if (resetDone) {
        // Query pending interrupts from the CPU's interrupt controller
        auto riscv_interrupts = static_cast<RiscvISA::Interrupts*>(interrupts[0]);
        uint64_t ip = riscv_interrupts->readIP();

        // Drive to verilated RTL model pins
        core->set_time_irq_i((ip & (1ULL << 7)) != 0); // Machine timer (MTIP)
        core->set_ipi_i((ip & (1ULL << 3)) != 0);      // Machine software (MSIP)
        core->set_irq_i((ip & (1ULL << 9)) != 0 || (ip & (1ULL << 11)) != 0); // External (SEIP/MEIP)

        // Drive memory interface inputs
        memIface->driveInputs();
    } else {
        memIface->driveInputs();
    }
    core->eval(); // Propagate falling edge and inputs combinationally
    core->dump_trace(cycleCount * 10);

    // 3. Check handshakes that will complete ON the upcoming rising edge.
    if (resetDone) {
        memIface->sampleOutputs();
    }

    // 4. Rising Edge
    core->set_clk_i(1);
    core->eval();
    core->dump_trace(cycleCount * 10 + 5);

    if (resetDone && core->get_illegal_instr_o()) {
        stats.numIllegalInst++;
        warn("CVA6 RTL CPU: Illegal instruction detected at PC: 0x%016llx\n",
             (unsigned long long)core->get_illegal_instr_pc_o());
    }

    if (resetDone && core->get_ebreak_o()) {
        stats.numEbreak++;
        exitSimLoop(csprintf("CVA6 program hit ebreak instruction at PC: 0x%016llx",
                             (unsigned long long)core->get_pc_o()));
        return;
    }

    // 5. Schedule next cycle
    schedule(tickEvent, clockEdge(Cycles(1)));
}

bool
CVA6RtlCPU::sendTimingReq(PacketPtr pkt, bool is_inst)
{
    CpuPort &port = is_inst ? instPort : dataPort;
    if (pkt->isWrite()) {
        pendingWriteResponses++;
    }
    if (!port.sendTimingReq(pkt)) {
        retryPkt = pkt;
        retryPort = &port;
        if (&port == &instPort) {
            stats.numInstPortRetries++;
        } else {
            stats.numDataPortRetries++;
        }
        return false;
    }
    return true;
}

void
CVA6RtlCPU::recordReadReq(bool is_inst, uint32_t size)
{
    stats.numReadReqs++;
    if (is_inst) {
        stats.numReadReqsInst++;
    } else {
        stats.numReadReqsData++;
    }
    if (size < 8) {
        stats.readReqSizes[size]++;
    }
}

void
CVA6RtlCPU::recordWriteReq(uint32_t size)
{
    stats.numWriteReqs++;
    if (size < 8) {
        stats.writeReqSizes[size]++;
    }
}

void
CVA6RtlCPU::recordReadBeat()
{
    stats.numReadBeats++;
}

void
CVA6RtlCPU::recordWriteBeat()
{
    stats.numWriteBeats++;
}

void
CVA6RtlCPU::recordWriteResp()
{
    stats.numWriteResps++;
}

void
CVA6RtlCPU::exitSimulation(const std::string &reason)
{
    exitSimLoop(reason);
}

void
CVA6RtlCPU::writePhysMem(Addr addr, const uint8_t *data, size_t size)
{
    // Functional (synchronous) write that bypasses the timing path and commits
    // data directly to the physical memory backing store. After this call,
    // physProxy.read(addr) will immediately return the written value.
    system->physProxy.writeBlob(addr, data, size);
}

} // namespace gem5
