#include "cpu/cva6/cva6_rtl_cpu.hh"
#include "sim/system.hh"
#include "sim/sim_exit.hh"
#include "base/logging.hh"
#include <iostream>
#include <cstring>

namespace gem5
{

CVA6RtlCPU::CVA6RtlCPU(const CVA6RtlCPUParams &params) :
    ClockedObject(params),
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
    tickEvent([this]{ tick(); }, name() + ".tick")
{
    // Instantiate Verilated CVA6 model
    core = new Vcva6_top();

    // Initialize inputs
    core->clk_i = 0;
    core->rst_ni = 0;
    core->boot_addr_i = 0x80000000;
    core->hart_id_i = 0;
    core->irq_i = 0;
    core->ipi_i = 0;
    core->time_irq_i = 0;
    core->debug_req_i = 0;
    core->eval();

    requestorId = params.system->getRequestorId(this);
}

CVA6RtlCPU::~CVA6RtlCPU()
{
    delete core;
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

    if (cycleCount % 1000 == 0) {
        std::cout << "[CVA6 CPU] Simulated clock cycles: " << std::dec << cycleCount << std::endl;
    }

    // 1. Reset logic
    if (cycleCount < 10) {
        core->rst_ni = 0;
        resetDone = false;
    } else {
        core->rst_ni = 1;
        resetDone = true;
    }

    // 2. Falling Edge & Setup Inputs
    core->clk_i = 0;
    if (resetDone) {
        // Drive R channel outputs
        if (ar_busy && r_data_ready) {
            core->noc_resp_r_valid_i = 1;
            core->noc_resp_r_id_i = read_id;
            uint32_t bytes_per_beat = 1 << read_size;
            core->noc_resp_r_data_i = 0; // Clear first
            std::memcpy(&core->noc_resp_r_data_i, read_data_buffer.data() + read_beat * bytes_per_beat, bytes_per_beat);
            core->noc_resp_r_last_i = (read_beat == read_len);
            core->noc_resp_r_resp_i = 0; // OKAY
        } else {
            core->noc_resp_r_valid_i = 0;
            core->noc_resp_r_last_i = 0;
            core->noc_resp_r_data_i = 0;
            core->noc_resp_r_id_i = 0;
            core->noc_resp_r_resp_i = 0;
        }

        // Drive B channel outputs
        if (write_resp_pending) {
            core->noc_resp_b_valid_i = 1;
            core->noc_resp_b_id_i = write_id;
            core->noc_resp_b_resp_i = 0; // OKAY
        } else {
            core->noc_resp_b_valid_i = 0;
            core->noc_resp_b_id_i = 0;
            core->noc_resp_b_resp_i = 0;
        }

        // Drive ready inputs
        core->noc_resp_ar_ready_i = !ar_busy;
        core->noc_resp_aw_ready_i = !aw_received && !write_resp_pending;
        core->noc_resp_w_ready_i = (aw_received || core->noc_req_aw_valid_o) && !write_resp_pending;
    } else {
        // Inputs during reset
        core->noc_resp_r_valid_i = 0;
        core->noc_resp_r_last_i = 0;
        core->noc_resp_r_data_i = 0;
        core->noc_resp_r_id_i = 0;
        core->noc_resp_r_resp_i = 0;
        core->noc_resp_b_valid_i = 0;
        core->noc_resp_b_id_i = 0;
        core->noc_resp_b_resp_i = 0;
        core->noc_resp_ar_ready_i = 0;
        core->noc_resp_aw_ready_i = 0;
        core->noc_resp_w_ready_i = 0;
    }
    core->eval(); // Propagate falling edge and inputs combinationally

    // 3. Check handshakes that will complete ON the upcoming rising edge.
    // We check this after eval() when the clock is low, so that combinational
    // paths (such as core->noc_req_ar_valid_o and our driven inputs) have settled.
    if (resetDone) {
        // A. Read address (AR channel) handshake
        if (!ar_busy && core->noc_req_ar_valid_o && core->noc_resp_ar_ready_i) {
            uint64_t addr = core->noc_req_ar_addr_o;
            uint32_t bytes_per_beat = 1 << core->noc_req_ar_size_o;
            uint32_t total_bytes = bytes_per_beat * (core->noc_req_ar_len_o + 1);

            RequestPtr req = std::make_shared<Request>(addr, total_bytes, 0, requestorId);
            PacketPtr pkt = Packet::createRead(req);
            pkt->allocate();

            // Route to instPort if prot indicates instruction fetch, else dataPort
            CpuPort &port = (core->noc_req_ar_prot_o & 0x4) ? instPort : dataPort;
            port.sendFunctional(pkt);

            read_data_buffer.clear();
            read_data_buffer.resize(total_bytes);
            std::memcpy(read_data_buffer.data(), pkt->getPtr<uint8_t>(), total_bytes);

            uint64_t first_word = 0;
            std::memcpy(&first_word, read_data_buffer.data(), std::min(total_bytes, 8u));
            std::cout << "[AR] Cycle=" << std::dec << cycleCount
                      << " Addr=0x" << std::hex << addr
                      << " id=0x" << (int)core->noc_req_ar_id_o
                      << " size=" << std::dec << bytes_per_beat
                      << " len=" << (int)core->noc_req_ar_len_o
                      << " prot=" << (int)core->noc_req_ar_prot_o
                      << " -> first_word=0x" << std::hex << first_word << std::dec << std::endl;

            read_id = core->noc_req_ar_id_o;
            read_len = core->noc_req_ar_len_o;
            read_size = core->noc_req_ar_size_o;
            read_beat = 0;
            ar_busy = true;
            r_data_ready = false; // Will trigger 1-cycle delay next cycle

            delete pkt;
        }

        // B. Read response (R channel) handshake
        if (ar_busy && r_data_ready && core->noc_resp_r_valid_i && core->noc_req_r_ready_o) {
            uint32_t bytes_per_beat = 1 << read_size;
            uint64_t beat_val = 0;
            std::memcpy(&beat_val, read_data_buffer.data() + read_beat * bytes_per_beat, std::min(bytes_per_beat, 8u));
            std::cout << "[R Handshake] Cycle=" << std::dec << cycleCount
                      << " Beat=" << read_beat << "/" << read_len
                      << " id=0x" << std::hex << (int)core->noc_resp_r_id_i
                      << " Data=0x" << beat_val
                      << " last=" << std::dec << (int)(read_beat == read_len) << std::endl;

            if (read_beat == read_len) {
                ar_busy = false;
                r_data_ready = false;
            } else {
                read_beat++;
            }
        }

        // C. Update read latency state for next cycle
        if (ar_busy && !r_data_ready) {
            r_data_ready = true;
        }

        // D. Write address (AW channel) handshake
        bool aw_handshake = !aw_received && core->noc_req_aw_valid_o && core->noc_resp_aw_ready_i;
        if (aw_handshake) {
            aw_received = true;
            write_addr = core->noc_req_aw_addr_o;
            write_id = core->noc_req_aw_id_o;
            write_size = core->noc_req_aw_size_o;
            write_len = core->noc_req_aw_len_o;
            w_received_beats = 0;

            std::cout << "[AW Handshake] Cycle=" << std::dec << cycleCount
                      << " Addr=0x" << std::hex << write_addr
                      << " id=0x" << write_id
                      << " size=" << std::dec << (1 << write_size)
                      << " len=" << write_len << std::endl;
        }

        // E. Write data (W channel) handshake
        if ((aw_received || aw_handshake) && core->noc_req_w_valid_o && core->noc_resp_w_ready_i) {

            uint32_t current_size = aw_received ? write_size : core->noc_req_aw_size_o;
            uint32_t bytes_per_beat = 1 << current_size;
            uint64_t addr = (aw_received ? write_addr : core->noc_req_aw_addr_o)
                            + w_received_beats * bytes_per_beat;
            uint64_t data_val = core->noc_req_w_data_o;

            std::cout << "[W Handshake] Cycle=" << std::dec << cycleCount
                      << " Addr=0x" << std::hex << addr
                      << " Beat=" << std::dec << w_received_beats
                      << " Data=0x" << std::hex << data_val << std::dec << std::endl;

            // Check exit command
            if (addr == 0x80001000 && data_val != 0) {
                std::cout << "\n============================================\n";
                std::cout << "CVA6 Simulation finished! tohost = " << data_val
                          << " (exit code = " << (data_val >> 1) << ")" << std::endl;
                if (data_val == 1) {
                    std::cout << "SUCCESS: Sum of 1 to 9 is correct!" << std::endl;
                } else {
                    std::cout << "FAILURE: program exited with code " << data_val << std::endl;
                }
                std::cout << "Total simulated clock cycles: " << cycleCount << std::endl;
                std::cout << "============================================\n";
                exitSimLoop("CVA6 program completed successfully");
                return;
            }

            // Perform functional write
            RequestPtr req = std::make_shared<Request>(addr, bytes_per_beat, 0, requestorId);
            PacketPtr pkt = Packet::createWrite(req);
            pkt->allocate();
            std::memcpy(pkt->getPtr<uint8_t>(), &data_val, bytes_per_beat);
            dataPort.sendFunctional(pkt);
            delete pkt;

            w_received_beats++;

            // Wait, did we just finish the write burst?
            uint32_t current_len = aw_received ? write_len : core->noc_req_aw_len_o;
            if (w_received_beats == current_len + 1 || core->noc_req_w_last_o) {
                write_resp_pending = true;
                aw_received = false;
                w_received_beats = 0;
            }
        }

        // F. Write response (B channel) handshake
        if (write_resp_pending && core->noc_resp_b_valid_i && core->noc_req_b_ready_o) {
            write_resp_pending = false;
            std::cout << "[B Handshake] Cycle=" << std::dec << cycleCount
                      << " id=0x" << std::hex << write_id << std::dec << std::endl;
        }
    }

    // 4. Rising Edge
    core->clk_i = 1;
    core->eval();

    // 5. Schedule next cycle
    schedule(tickEvent, clockEdge(Cycles(1)));
}

} // namespace gem5
