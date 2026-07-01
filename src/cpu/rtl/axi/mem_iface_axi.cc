// Copyright 2026 Antigravity
// Description: General AXI4 Memory Interface class implementation.

#include "cpu/rtl/axi/mem_iface_axi.hh"
#include <cstring>
#include "base/str.hh"

namespace gem5
{

RtlMemIfaceAxi::RtlMemIfaceAxi(RtlCpuHelper *_helper)
    : RtlMemIfaceBase(_helper),
      ar_busy(false),
      r_data_ready(false),
      read_addr(0),
      read_id(0),
      read_len(0),
      read_size(0),
      read_beat(0),
      aw_received(false),
      write_addr(0),
      write_id(0),
      write_size(0),
      write_len(0),
      w_received_beats(0),
      b_handshake_pending(false),
      tohost_exit_pending(false),
      tohost_exit_msg("")
{
}

void
RtlMemIfaceAxi::driveInputs()
{
    if (helper->isResetDone()) {
        if (b_handshake_pending) {
            bRespQueue.pop_front();
            b_handshake_pending = false;
        }

        // Drive R channel outputs
        if (ar_busy && r_data_ready) {
            set_noc_resp_r_valid_i(1);
            set_noc_resp_r_id_i(read_id);
            uint32_t bytes_per_beat = 1 << read_size;
            uint64_t data_val = 0;
            std::memcpy(&data_val,
                        read_data_buffer.data() + read_beat * bytes_per_beat,
                        bytes_per_beat);
            uint64_t addr_beat = read_addr + read_beat * bytes_per_beat;
            uint32_t shift_bytes = addr_beat % 8;
            set_noc_resp_r_data_i(data_val << (shift_bytes * 8));
            set_noc_resp_r_last_i(read_beat == read_len);
            set_noc_resp_r_resp_i(0); // OKAY
        } else {
            set_noc_resp_r_valid_i(0);
            set_noc_resp_r_last_i(0);
            set_noc_resp_r_data_i(0);
            set_noc_resp_r_id_i(0);
            set_noc_resp_r_resp_i(0);
        }

        // Drive B channel outputs
        if (!bRespQueue.empty()) {
            set_noc_resp_b_valid_i(1);
            set_noc_resp_b_id_i(bRespQueue.front());
            set_noc_resp_b_resp_i(0); // OKAY
        } else {
            set_noc_resp_b_valid_i(0);
            set_noc_resp_b_id_i(0);
            set_noc_resp_b_resp_i(0);
        }

        // Drive ready inputs
        bool can_accept = !helper->isRetryPending();
        set_noc_resp_ar_ready_i(!ar_busy && can_accept);
        set_noc_resp_aw_ready_i(!aw_received && can_accept);
        set_noc_resp_w_ready_i(
            (aw_received || get_noc_req_aw_valid_o()) && can_accept);
    } else {
        // Inputs during reset
        set_noc_resp_r_valid_i(0);
        set_noc_resp_r_last_i(0);
        set_noc_resp_r_data_i(0);
        set_noc_resp_r_id_i(0);
        set_noc_resp_r_resp_i(0);
        set_noc_resp_b_valid_i(0);
        set_noc_resp_b_id_i(0);
        set_noc_resp_b_resp_i(0);
        set_noc_resp_ar_ready_i(0);
        set_noc_resp_aw_ready_i(0);
        set_noc_resp_w_ready_i(0);
    }
}

void
RtlMemIfaceAxi::sampleOutputs()
{
    if (!helper->isResetDone()) {
        return;
    }

    // A. Read address (AR channel) handshake
    if (!ar_busy && !helper->isRetryPending() && get_noc_req_ar_valid_o()) {
        uint64_t addr = get_noc_req_ar_addr_o();
        uint32_t bytes_per_beat = 1 << get_noc_req_ar_size_o();
        uint32_t total_bytes =
            bytes_per_beat * (get_noc_req_ar_len_o() + 1);

        Request::Flags flags = 0;
        if (addr < 0x80000000) {
            flags.set(Request::UNCACHEABLE);
        }
        RequestPtr req = std::make_shared<Request>(addr, total_bytes,
                                                   flags, helper->getRequestorId());
        PacketPtr pkt = Packet::createRead(req);
        pkt->allocate();

        bool is_inst = (get_noc_req_ar_prot_o() & 0x4) != 0;

        read_id = get_noc_req_ar_id_o();
        read_addr = addr;
        read_len = get_noc_req_ar_len_o();
        read_size = get_noc_req_ar_size_o();
        read_beat = 0;
        ar_busy = true;
        r_data_ready = false;

        helper->recordReadReq(is_inst, read_size);

        helper->sendTimingReq(pkt, is_inst);
    }

    // B. Read response (R channel) handshake
    if (ar_busy && r_data_ready && get_noc_req_r_ready_o()) {
        helper->recordReadBeat();

        if (read_beat == read_len) {
            ar_busy = false;
            r_data_ready = false;
        } else {
            read_beat++;
        }
    }

    // F. Write response (B channel) handshake
    if (!bRespQueue.empty() && get_noc_req_b_ready_o()) {
        helper->recordWriteResp();
        b_handshake_pending = true;
    }

    // D. Write address (AW channel) handshake
    bool aw_handshake =
        !aw_received && !helper->isRetryPending() && get_noc_req_aw_valid_o();
    if (aw_handshake) {
        helper->recordWriteReq(get_noc_req_aw_size_o());
        aw_received = true;
        write_addr = get_noc_req_aw_addr_o();
        write_id = get_noc_req_aw_id_o();
        write_size = get_noc_req_aw_size_o();
        write_len = get_noc_req_aw_len_o();
        w_received_beats = 0;

        writeXacts.push_back(
            std::make_pair((uint8_t)write_id, write_len + 1));
    }

    // E. Write data (W channel) handshake
    if (!helper->isRetryPending() && (aw_received || get_noc_req_aw_valid_o()) &&
        get_noc_req_w_valid_o()) {

        helper->recordWriteBeat();
        uint32_t current_size =
            aw_received ? write_size : get_noc_req_aw_size_o();
        uint32_t bytes_per_beat = 1 << current_size;
        uint64_t addr =
            (aw_received ? write_addr : get_noc_req_aw_addr_o()) +
            w_received_beats * bytes_per_beat;
        uint64_t data_val = get_noc_req_w_data_o();

        // Detect tohost write (HTIF exit protocol).
        // The CVA6 pipeline can commit `sd tohost` and `ebreak` in the same
        // RTL evaluation tick. A timing write (sendTimingReq) has a 10ns async
        // latency, so physProxy.read() would return 0 if ebreak fires first.
        // Fix: use writePhysMem (functional/synchronous) to commit tohost to
        // physical memory immediately, then schedule the exit. This guarantees
        // physProxy.read(0x80001000) returns the correct value regardless of
        // when the ebreak exit event fires.
        //if (addr == 0x80001000 && data_val != 0) {
        //    uint32_t shift_bytes = addr % 8;
        //    uint64_t data_to_write = data_val >> (shift_bytes * 8);
        //    helper->writePhysMem(addr,
        //                         reinterpret_cast<const uint8_t*>(&data_to_write),
        //                         bytes_per_beat);
        //    helper->exitSimulation(csprintf(
        //        "CVA6 program completed with tohost=0x%llx at PC: 0x%016llx",
        //        (unsigned long long)data_val,
        //        (unsigned long long)get_pc_o()));
        //    return;
        //}

        // Perform timing write (non-tohost addresses)
        Request::Flags flags = 0;
        if (addr < 0x80000000) {
            flags.set(Request::UNCACHEABLE);
        }
        RequestPtr req = std::make_shared<Request>(addr, bytes_per_beat,
                                                   flags, helper->getRequestorId());
        PacketPtr pkt = Packet::createWrite(req);
        pkt->allocate();
        uint32_t shift_bytes = addr % 8;
        uint64_t data_to_write = data_val >> (shift_bytes * 8);
        std::memcpy(pkt->getPtr<uint8_t>(), &data_to_write,
                    bytes_per_beat);

        helper->sendTimingReq(pkt, false);

        uint32_t current_len =
            aw_received ? write_len : get_noc_req_aw_len_o();

        w_received_beats++;

        if (w_received_beats == current_len + 1 ||
            get_noc_req_w_last_o()) {
            aw_received = false;
            w_received_beats = 0;
        }
    }
}

void
RtlMemIfaceAxi::acceptResp(PacketPtr pkt)
{
    if (pkt->isRead()) {
        read_data_buffer.clear();
        read_data_buffer.resize(pkt->getSize());
        std::memcpy(read_data_buffer.data(), pkt->getPtr<uint8_t>(),
                    pkt->getSize());
        r_data_ready = true;
    } else if (pkt->isWrite()) {
        if (!writeXacts.empty()) {
            if (writeXacts.front().second > 0) {
                writeXacts.front().second--;
            }
            if (writeXacts.front().second == 0) {
                bRespQueue.push_back(writeXacts.front().first);
                writeXacts.pop_front();
            }
        }
        // If this ACK is for the tohost write, SimpleMemory has now committed
        // the value. physProxy.read(0x80001000) will return the correct result.
        if (tohost_exit_pending) {
            tohost_exit_pending = false;
            helper->exitSimulation(tohost_exit_msg);
        }
    }
    delete pkt;
}

} // namespace gem5
