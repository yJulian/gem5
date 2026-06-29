#include "cpu/cva6/cva6_rtl_cpu.hh"
#include "arch/riscv/interrupts.hh"
#include "arch/riscv/regs/int.hh"
#include "cpu/simple_thread.hh"

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
      ar_busy(false),
      r_data_ready(false),
      read_addr(0),
      aw_received(false),
      write_addr(0),
      write_id(0),
      write_size(0),
      write_len(0),
      w_received_beats(0),
      tickEvent([this] { tick(); }, name() + ".tick"),
      core(nullptr),
      libHandle(nullptr),
      destroyCorePointer(nullptr),
      retryPkt(nullptr),
      retryPort(nullptr),
      pendingWriteResponses(0),
      b_handshake_pending(false),
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
    if (pkt->isRead()) {
        read_data_buffer.clear();
        read_data_buffer.resize(pkt->getSize());
        std::memcpy(read_data_buffer.data(), pkt->getPtr<uint8_t>(),
                    pkt->getSize());
        r_data_ready = true;
    } else if (pkt->isWrite()) {
        if (pendingWriteResponses > 0) {
            pendingWriteResponses--;
        }
        // gem5 write responses arrive in issue order on a single port, so they
        // retire the beats of the oldest outstanding AXI write first. Once all
        // beats of that write are acknowledged, queue exactly one B response.
        if (!writeXacts.empty()) {
            if (writeXacts.front().second > 0) {
                writeXacts.front().second--;
            }
            if (writeXacts.front().second == 0) {
                bRespQueue.push_back(writeXacts.front().first);
                writeXacts.pop_front();
            }
        }
    }
    delete pkt;
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
        if (b_handshake_pending) {
            bRespQueue.pop_front();
            b_handshake_pending = false;
        }
        // Query pending interrupts from the CPU's interrupt controller
        auto riscv_interrupts = static_cast<RiscvISA::Interrupts*>(interrupts[0]);
        uint64_t ip = riscv_interrupts->readIP();

        // Drive to verilated RTL model pins
        core->set_time_irq_i((ip & (1ULL << 7)) != 0); // Machine timer (MTIP)
        core->set_ipi_i((ip & (1ULL << 3)) != 0);      // Machine software (MSIP)
        core->set_irq_i((ip & (1ULL << 9)) != 0 || (ip & (1ULL << 11)) != 0); // External (SEIP/MEIP)

        // Drive R channel outputs
        if (ar_busy && r_data_ready) {
            core->set_noc_resp_r_valid_i(1);
            core->set_noc_resp_r_id_i(read_id);
            uint32_t bytes_per_beat = 1 << read_size;
            uint64_t data_val = 0;
            std::memcpy(&data_val,
                        read_data_buffer.data() + read_beat * bytes_per_beat,
                        bytes_per_beat);
            uint64_t addr_beat = read_addr + read_beat * bytes_per_beat;
            uint32_t shift_bytes = addr_beat % 8;
            core->set_noc_resp_r_data_i(data_val << (shift_bytes * 8));
            core->set_noc_resp_r_last_i(read_beat == read_len);
            core->set_noc_resp_r_resp_i(0); // OKAY
        } else {
            core->set_noc_resp_r_valid_i(0);
            core->set_noc_resp_r_last_i(0);
            core->set_noc_resp_r_data_i(0);
            core->set_noc_resp_r_id_i(0);
            core->set_noc_resp_r_resp_i(0);
        }

        // Drive B channel outputs (one response per outstanding AXI write)
        if (!bRespQueue.empty()) {
            core->set_noc_resp_b_valid_i(1);
            core->set_noc_resp_b_id_i(bRespQueue.front());
            core->set_noc_resp_b_resp_i(0); // OKAY
        } else {
            core->set_noc_resp_b_valid_i(0);
            core->set_noc_resp_b_id_i(0);
            core->set_noc_resp_b_resp_i(0);
        }

        // Drive ready inputs
        bool can_accept = !retryPkt;
        core->set_noc_resp_ar_ready_i(!ar_busy && can_accept);
        core->set_noc_resp_aw_ready_i(!aw_received && can_accept);
        core->set_noc_resp_w_ready_i(
            (aw_received || core->get_noc_req_aw_valid_o()) && can_accept);
    } else {
        // Inputs during reset
        core->set_noc_resp_r_valid_i(0);
        core->set_noc_resp_r_last_i(0);
        core->set_noc_resp_r_data_i(0);
        core->set_noc_resp_r_id_i(0);
        core->set_noc_resp_r_resp_i(0);
        core->set_noc_resp_b_valid_i(0);
        core->set_noc_resp_b_id_i(0);
        core->set_noc_resp_b_resp_i(0);
        core->set_noc_resp_ar_ready_i(0);
        core->set_noc_resp_aw_ready_i(0);
        core->set_noc_resp_w_ready_i(0);
    }
    core->eval(); // Propagate falling edge and inputs combinationally
    core->dump_trace(cycleCount * 10);

    // 3. Check handshakes that will complete ON the upcoming rising edge.
    // We check this after eval() when the clock is low, so that combinational
    // paths (such as core->noc_req_ar_valid_o and our driven inputs) have settled.
    if (resetDone) {
        // A. Read address (AR channel) handshake
        if (!ar_busy && !retryPkt && core->get_noc_req_ar_valid_o()) {
            uint64_t addr = core->get_noc_req_ar_addr_o();
            uint32_t bytes_per_beat = 1 << core->get_noc_req_ar_size_o();
            uint32_t total_bytes =
                bytes_per_beat * (core->get_noc_req_ar_len_o() + 1);

            Request::Flags flags = 0;
            if (addr < 0x80000000) {
                flags.set(Request::UNCACHEABLE);
            }
            RequestPtr req = std::make_shared<Request>(addr, total_bytes,
                                                       flags, requestorId);
            PacketPtr pkt = Packet::createRead(req);
            pkt->allocate();

            // Route to instPort if prot indicates instruction fetch, else dataPort
            CpuPort &port =
                (core->get_noc_req_ar_prot_o() & 0x4) ? instPort : dataPort;

            read_id = core->get_noc_req_ar_id_o();
            read_addr = addr;
            read_len = core->get_noc_req_ar_len_o();
            read_size = core->get_noc_req_ar_size_o();
            read_beat = 0;
            ar_busy = true;
            r_data_ready = false;

            stats.numReadReqs++;
            if (core->get_noc_req_ar_prot_o() & 0x4) {
                stats.numReadReqsInst++;
            } else {
                stats.numReadReqsData++;
            }
            uint32_t ar_size = core->get_noc_req_ar_size_o();
            if (ar_size < 8) {
                stats.readReqSizes[ar_size]++;
            }

            if (!port.sendTimingReq(pkt)) {
                retryPkt = pkt;
                retryPort = &port;
                if (&port == &instPort) {
                    stats.numInstPortRetries++;
                } else {
                    stats.numDataPortRetries++;
                }
            }

#if DEBUG_CVA
            std::cout << "[AR Handshake] Cycle=" << std::dec << cycleCount
                      << " Addr=0x" << std::hex << addr << " id=0x" << read_id
                      << " size=" << std::dec << (1 << read_size)
                      << " len=" << read_len << std::endl;
#endif
        }

        // B. Read response (R channel) handshake
        if (ar_busy && r_data_ready && core->get_noc_req_r_ready_o()) {
            stats.numReadBeats++;
            uint32_t bytes_per_beat = 1 << read_size;
            uint64_t beat_val = 0;
            std::memcpy(&beat_val, read_data_buffer.data() + read_beat * bytes_per_beat, std::min(bytes_per_beat, 8u));
#if DEBUG_CVA
            std::cout << "[R Handshake] Cycle=" << std::dec << cycleCount
                      << " Beat=" << read_beat << "/" << read_len << " id=0x"
                      << std::hex << (int)read_id << " Data=0x" << beat_val
                      << " last=" << std::dec << (int)(read_beat == read_len)
                      << std::endl;
#endif

            if (read_beat == read_len) {
                ar_busy = false;
                r_data_ready = false;
            } else {
                read_beat++;
            }
        }

        // C. (Read latency is handled by timing response callback)

        // F. Write response (B channel) handshake
        if (!bRespQueue.empty() && core->get_noc_req_b_ready_o()) {
            stats.numWriteResps++;
#if DEBUG_CVA
            std::cout << "[B Handshake] Cycle=" << std::dec << cycleCount
                      << " id=0x" << std::hex
                      << (unsigned)bRespQueue.front()
                      << std::dec << std::endl;
#endif
            b_handshake_pending = true;
        }

        // D. Write address (AW channel) handshake
        bool aw_handshake =
            !aw_received && !retryPkt && core->get_noc_req_aw_valid_o();
        if (aw_handshake) {
            stats.numWriteReqs++;
            uint32_t aw_size = core->get_noc_req_aw_size_o();
            if (aw_size < 8) {
                stats.writeReqSizes[aw_size]++;
            }
            aw_received = true;
            write_addr = core->get_noc_req_aw_addr_o();
            write_id = core->get_noc_req_aw_id_o();
            write_size = core->get_noc_req_aw_size_o();
            write_len = core->get_noc_req_aw_len_o();
            w_received_beats = 0;

            // Track this write so it gets exactly one B response once all of
            // its (write_len + 1) beats are acknowledged by gem5.
            writeXacts.push_back(
                std::make_pair((uint8_t)write_id, write_len + 1));

#if DEBUG_CVA
            std::cout << "[AW Handshake] Cycle=" << std::dec << cycleCount
                      << " Addr=0x" << std::hex << write_addr
                      << " id=0x" << write_id
                      << " size=" << std::dec << (1 << write_size)
                      << " len=" << write_len << std::endl;
#endif
        }

        // E. Write data (W channel) handshake
        if (!retryPkt && (aw_received || core->get_noc_req_aw_valid_o()) &&
            core->get_noc_req_w_valid_o()) {

            stats.numWriteBeats++;
            uint32_t current_size =
                aw_received ? write_size : core->get_noc_req_aw_size_o();
            uint32_t bytes_per_beat = 1 << current_size;
            uint64_t addr =
                (aw_received ? write_addr : core->get_noc_req_aw_addr_o()) +
                w_received_beats * bytes_per_beat;
            uint64_t data_val = core->get_noc_req_w_data_o();

            // Check exit command
            if (addr == 0x80001000 && data_val != 0) {
                exitSimLoop(csprintf("CVA6 program completed successfully at PC: 0x%016llx",
                                     (unsigned long long)core->get_pc_o()));
                return;
            }

            // Perform timing write
            Request::Flags flags = 0;
            if (addr < 0x80000000) {
                flags.set(Request::UNCACHEABLE);
            }
            RequestPtr req = std::make_shared<Request>(addr, bytes_per_beat,
                                                       flags, requestorId);
            PacketPtr pkt = Packet::createWrite(req);
            pkt->allocate();
            uint32_t shift_bytes = addr % 8;
            uint64_t data_to_write = data_val >> (shift_bytes * 8);
            std::memcpy(pkt->getPtr<uint8_t>(), &data_to_write,
                        bytes_per_beat);

            pendingWriteResponses++;

            if (!dataPort.sendTimingReq(pkt)) {
                retryPkt = pkt;
                retryPort = &dataPort;
                stats.numDataPortRetries++;
            }

            uint32_t current_len =
                aw_received ? write_len : core->get_noc_req_aw_len_o();

#if DEBUG_CVA
            std::cout << "[W Handshake] Cycle=" << std::dec << cycleCount
                      << " Data=0x" << std::hex << data_val
                      << " Beat=" << w_received_beats << "/" << current_len
                      << " last=" << std::dec
                      << (w_received_beats == current_len ||
                          core->get_noc_req_w_last_o())
                      << std::endl;
#endif

            w_received_beats++;

            // Did we just finish the write burst? Release the W datapath so
            // the next AW can be accepted; the B response for this write is
            // emitted independently once gem5 acknowledges all of its beats.
            if (w_received_beats == current_len + 1 ||
                core->get_noc_req_w_last_o()) {
                aw_received = false;
                w_received_beats = 0;
            }
        }
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

} // namespace gem5
