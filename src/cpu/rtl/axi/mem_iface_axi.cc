// Copyright 2026 Antigravity
// Description: General AXI4 Memory Interface class implementation.

#include "cpu/rtl/axi/mem_iface_axi.hh"
#include <cstring>
#include "base/str.hh"

namespace gem5
{

RtlMemIfaceAxi::RtlMemIfaceAxi(RtlCpuHelper *_helper, RtlCoreInterface *_core)
    : RtlMemIfaceBase(_helper),
      core(_core),
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
            core->set_r_valid(1);
            core->set_r_id(read_id);
            uint32_t bytes_per_beat = 1 << read_size;
            uint64_t data_val = 0;
            std::memcpy(&data_val,
                        read_data_buffer.data() + read_beat * bytes_per_beat,
                        bytes_per_beat);
            uint64_t addr_beat = read_addr + read_beat * bytes_per_beat;
            uint32_t shift_bytes = addr_beat % 8;
            core->set_r_data(data_val << (shift_bytes * 8));
            core->set_r_last(read_beat == read_len);
            core->set_r_resp(0); // OKAY
        } else {
            core->set_r_valid(0);
            core->set_r_last(0);
            core->set_r_data(0);
            core->set_r_id(0);
            core->set_r_resp(0);
        }

        // Drive B channel outputs
        if (!bRespQueue.empty()) {
            core->set_b_valid(1);
            core->set_b_id(bRespQueue.front());
            core->set_b_resp(0); // OKAY
        } else {
            core->set_b_valid(0);
            core->set_b_id(0);
            core->set_b_resp(0);
        }

        // Drive ready inputs
        bool can_accept = !helper->isRetryPending();
        core->set_ar_ready(!ar_busy && can_accept);
        core->set_aw_ready(!aw_received && can_accept);
        core->set_w_ready((aw_received || core->get_aw_valid()) && can_accept);
    } else {
        // Inputs during reset
        core->set_r_valid(0);
        core->set_r_last(0);
        core->set_r_data(0);
        core->set_r_id(0);
        core->set_r_resp(0);
        core->set_b_valid(0);
        core->set_b_id(0);
        core->set_b_resp(0);
        core->set_ar_ready(0);
        core->set_aw_ready(0);
        core->set_w_ready(0);
    }
}

void
RtlMemIfaceAxi::sampleOutputs()
{
    if (!helper->isResetDone()) {
        return;
    }

    // A. Read address (AR channel) handshake
    if (!ar_busy && !helper->isRetryPending() && core->get_ar_valid()) {
        uint64_t addr = core->get_ar_addr();
        uint32_t bytes_per_beat = 1 << core->get_ar_size();
        uint32_t total_bytes = bytes_per_beat * (core->get_ar_len() + 1);

        Request::Flags flags = 0;
        if (addr < 0x80000000) {
            flags.set(Request::UNCACHEABLE);
        }
        RequestPtr req = std::make_shared<Request>(addr, total_bytes,
                                                   flags, helper->getRequestorId());
        PacketPtr pkt = Packet::createRead(req);
        pkt->allocate();

        bool is_inst = (core->get_ar_prot() & 0x4) != 0;

        read_id = core->get_ar_id();
        read_addr = addr;
        read_len = core->get_ar_len();
        read_size = core->get_ar_size();
        read_beat = 0;
        ar_busy = true;
        r_data_ready = false;

        helper->recordReadReq(is_inst, read_size);

        helper->sendTimingReq(pkt, is_inst);
    }

    // B. Read response (R channel) handshake
    if (ar_busy && r_data_ready && core->get_r_ready()) {
        helper->recordReadBeat();

        if (read_beat == read_len) {
            ar_busy = false;
            r_data_ready = false;
        } else {
            read_beat++;
        }
    }

    // F. Write response (B channel) handshake
    if (!bRespQueue.empty() && core->get_b_ready()) {
        helper->recordWriteResp();
        b_handshake_pending = true;
    }

    // D. Write address (AW channel) handshake
    bool aw_handshake =
        !aw_received && !helper->isRetryPending() && core->get_aw_valid();
    if (aw_handshake) {
        helper->recordWriteReq(core->get_aw_size());
        aw_received = true;
        write_addr = core->get_aw_addr();
        write_id = core->get_aw_id();
        write_size = core->get_aw_size();
        write_len = core->get_aw_len();
        w_received_beats = 0;

        writeXacts.push_back(
            std::make_pair((uint8_t)write_id, write_len + 1));
    }

    // E. Write data (W channel) handshake
    if (!helper->isRetryPending() && (aw_received || core->get_aw_valid()) &&
        core->get_w_valid()) {

        helper->recordWriteBeat();
        uint32_t current_size = aw_received ? write_size : core->get_aw_size();
        uint32_t bytes_per_beat = 1 << current_size;
        uint64_t addr = (aw_received ? write_addr : core->get_aw_addr()) +
                        w_received_beats * bytes_per_beat;
        uint64_t data_val = core->get_w_data();

        // Perform timing write
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

        uint32_t current_len = aw_received ? write_len : core->get_aw_len();

        w_received_beats++;

        if (w_received_beats == current_len + 1 || core->get_w_last()) {
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
