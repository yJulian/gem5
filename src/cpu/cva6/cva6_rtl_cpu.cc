#include "cpu/cva6/cva6_rtl_cpu.hh"

#ifndef DEBUG_CVA
#define DEBUG_CVA 1
#endif

#include <dlfcn.h>

#include <cstring>
#include <iostream>

#include "base/logging.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

namespace gem5
{

CVA6RtlCPU::CVA6RtlCPU(const CVA6RtlCPUParams &params)
    : ClockedObject(params),
      instPort(params.name + ".inst_port", this),
      dataPort(params.name + ".data_port", this),
      cycleCount(0),
      resetDone(false),
      ar_busy(false),
      r_data_ready(false),
      aw_received(false),
      write_addr(0),
      write_id(0),
      write_size(0),
      write_len(0),
      w_received_beats(0),
      write_resp_pending(false),
      tickEvent([this] { tick(); }, name() + ".tick"),
      core(nullptr),
      libHandle(nullptr),
      destroyCorePointer(nullptr),
      retryPkt(nullptr),
      retryPort(nullptr),
      pendingWriteResponses(0),
      writeBurstFinished(false)
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
        if (writeBurstFinished && pendingWriteResponses == 0) {
            write_resp_pending = true;
            writeBurstFinished = false;
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
        return ClockedObject::getPort(if_name, idx);
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

#if DEBUG_CVA
    if (cycleCount % 100000 == 0) {
        std::cout << "[CVA6 CPU] Simulated clock cycles: " << std::dec
                  << cycleCount << std::endl;
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

    // 2. Falling Edge & Setup Inputs
    core->set_clk_i(0);
    if (resetDone) {
        // Drive R channel outputs
        if (ar_busy && r_data_ready) {
            core->set_noc_resp_r_valid_i(1);
            core->set_noc_resp_r_id_i(read_id);
            uint32_t bytes_per_beat = 1 << read_size;
            uint64_t data_val = 0;
            std::memcpy(&data_val,
                        read_data_buffer.data() + read_beat * bytes_per_beat,
                        bytes_per_beat);
            core->set_noc_resp_r_data_i(data_val);
            core->set_noc_resp_r_last_i(read_beat == read_len);
            core->set_noc_resp_r_resp_i(0); // OKAY
        } else {
            core->set_noc_resp_r_valid_i(0);
            core->set_noc_resp_r_last_i(0);
            core->set_noc_resp_r_data_i(0);
            core->set_noc_resp_r_id_i(0);
            core->set_noc_resp_r_resp_i(0);
        }

        // Drive B channel outputs
        if (write_resp_pending) {
            core->set_noc_resp_b_valid_i(1);
            core->set_noc_resp_b_id_i(write_id);
            core->set_noc_resp_b_resp_i(0); // OKAY
        } else {
            core->set_noc_resp_b_valid_i(0);
            core->set_noc_resp_b_id_i(0);
            core->set_noc_resp_b_resp_i(0);
        }

        // Drive ready inputs
        bool can_accept = !retryPkt;
        core->set_noc_resp_ar_ready_i(!ar_busy && can_accept);
        core->set_noc_resp_aw_ready_i(!aw_received && !write_resp_pending &&
                                      can_accept);
        core->set_noc_resp_w_ready_i(
            (aw_received || core->get_noc_req_aw_valid_o()) &&
            !write_resp_pending && can_accept);
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
            read_len = core->get_noc_req_ar_len_o();
            read_size = core->get_noc_req_ar_size_o();
            read_beat = 0;
            ar_busy = true;
            r_data_ready = false;

            if (!port.sendTimingReq(pkt)) {
                retryPkt = pkt;
                retryPort = &port;
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
        if (write_resp_pending && core->get_noc_req_b_ready_o()) {
            write_resp_pending = false;
#if DEBUG_CVA
            std::cout << "[B Handshake] Cycle=" << std::dec << cycleCount
                      << " id=0x" << std::hex << write_id << std::dec
                      << std::endl;
#endif
            // Re-drive AW/W ready inputs now that write_resp_pending is false
            bool can_accept = !retryPkt;
            core->set_noc_resp_aw_ready_i(!aw_received && can_accept);
            core->set_noc_resp_w_ready_i(
                (aw_received || core->get_noc_req_aw_valid_o()) && can_accept);
            core->eval();
        }

        // D. Write address (AW channel) handshake
        bool aw_handshake = !aw_received && !write_resp_pending && !retryPkt &&
                            core->get_noc_req_aw_valid_o();
        if (aw_handshake) {
            aw_received = true;
            write_addr = core->get_noc_req_aw_addr_o();
            write_id = core->get_noc_req_aw_id_o();
            write_size = core->get_noc_req_aw_size_o();
            write_len = core->get_noc_req_aw_len_o();
            w_received_beats = 0;

#if DEBUG_CVA
            std::cout << "[AW Handshake] Cycle=" << std::dec << cycleCount
                      << " Addr=0x" << std::hex << write_addr
                      << " id=0x" << write_id
                      << " size=" << std::dec << (1 << write_size)
                      << " len=" << write_len << std::endl;
#endif
        }

        // E. Write data (W channel) handshake
        if (!write_resp_pending && !retryPkt &&
            (aw_received || core->get_noc_req_aw_valid_o()) &&
            core->get_noc_req_w_valid_o()) {

            uint32_t current_size =
                aw_received ? write_size : core->get_noc_req_aw_size_o();
            uint32_t bytes_per_beat = 1 << current_size;
            uint64_t addr =
                (aw_received ? write_addr : core->get_noc_req_aw_addr_o()) +
                w_received_beats * bytes_per_beat;
            uint64_t data_val = core->get_noc_req_w_data_o();

            // Check exit command
            if (addr == 0x80001000 && data_val != 0) {
                exitSimLoop("CVA6 program completed successfully");
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
            std::memcpy(pkt->getPtr<uint8_t>(), &data_val, bytes_per_beat);

            pendingWriteResponses++;

            if (!dataPort.sendTimingReq(pkt)) {
                retryPkt = pkt;
                retryPort = &dataPort;
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

            // Did we just finish the write burst?
            if (w_received_beats == current_len + 1 ||
                core->get_noc_req_w_last_o()) {
                writeBurstFinished = true;
                aw_received = false;
                w_received_beats = 0;

                if (pendingWriteResponses == 0) {
                    write_resp_pending = true;
                    writeBurstFinished = false;
                }
            }
        }
    }

    // 4. Rising Edge
    core->set_clk_i(1);
    core->eval();
    core->dump_trace(cycleCount * 10 + 5);

    if (resetDone && core->get_ebreak_o()) {
        exitSimLoop("CVA6 program hit ebreak instruction");
        return;
    }

    // 5. Schedule next cycle
    schedule(tickEvent, clockEdge(Cycles(1)));
}

} // namespace gem5
