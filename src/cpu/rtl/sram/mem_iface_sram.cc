// Copyright 2026 Antigravity
// Description: General SRAM/Native Memory Interface class implementation.

#include "cpu/rtl/sram/mem_iface_sram.hh"
#include <cstring>
#include <algorithm>
#include "base/str.hh"

namespace gem5
{

RtlMemIfaceSram::RtlMemIfaceSram(RtlCpuHelper *_helper)
    : RtlMemIfaceBase(_helper),
      pending_read(false),
      pending_write(false),
      read_data(0),
      read_data_ready(false),
      write_done(false)
{
}

void
RtlMemIfaceSram::driveInputs()
{
    if (helper->isResetDone()) {
        if (read_data_ready) {
            set_resp_valid(1);
            set_resp_rdata(read_data);
            read_data_ready = false;
            pending_read = false;
        } else if (write_done) {
            set_resp_valid(1);
            set_resp_rdata(0);
            write_done = false;
            pending_write = false;
        } else {
            set_resp_valid(0);
            set_resp_rdata(0);
        }

        // Deassert resp_ready if we are busy handling a request or retry is pending
        bool can_accept = !helper->isRetryPending() && !pending_read && !pending_write;
        set_resp_ready(can_accept);
    } else {
        set_resp_ready(0);
        set_resp_valid(0);
        set_resp_rdata(0);
        pending_read = false;
        pending_write = false;
        read_data_ready = false;
        write_done = false;
    }
}

void
RtlMemIfaceSram::sampleOutputs()
{
    if (!helper->isResetDone()) {
        return;
    }

    // Check if there is an incoming request and we are ready to accept it
    bool can_accept = !helper->isRetryPending() && !pending_read && !pending_write;

    if (can_accept && get_req_valid()) {
        uint64_t addr = get_req_addr();
        bool is_write = get_req_write();

        if (is_write) {
            uint64_t wdata = get_req_wdata();
            uint8_t wstrb = get_req_wstrb();

            // Perform write request
            Request::Flags flags = 0;
            if (addr < 0x80000000) {
                flags.set(Request::UNCACHEABLE);
            }
            uint32_t size = 8;
            if (wstrb != 0) {
                if (wstrb <= 0x0F) size = 4;
            }

            // Check exit command via tohost address.
            // Important: do NOT return early. Fall through to sendTimingReq so
            // the data is committed to physical memory (SimpleMemory) before
            // the exit event fires. physProxy.read(tohost_addr) in the Python
            // sim loop will then return the correct value (1=success, 3=fail).
            bool is_tohost_exit = (addr == 0x80001000 && wdata != 0);

            RequestPtr req = std::make_shared<Request>(addr, size, flags, helper->getRequestorId());
            PacketPtr pkt = Packet::createWrite(req);
            pkt->allocate();
            std::memcpy(pkt->getPtr<uint8_t>(), &wdata, size);

            pending_write = true;
            helper->recordWriteReq(size);
            helper->recordWriteBeat();
            helper->sendTimingReq(pkt, false);

            // Schedule exit after write is in-flight (exitSimLoop uses simQuantum delay)
            if (is_tohost_exit) {
                helper->exitSimulation(csprintf(
                    "CVA6 program completed with tohost=0x%llx",
                    (unsigned long long)wdata));
                return;
            }

        } else {
            // Perform read request
            Request::Flags flags = 0;
            if (addr < 0x80000000) {
                flags.set(Request::UNCACHEABLE);
            }
            uint32_t size = 8;

            RequestPtr req = std::make_shared<Request>(addr, size, flags, helper->getRequestorId());
            PacketPtr pkt = Packet::createRead(req);
            pkt->allocate();

            pending_read = true;
            bool is_inst = false;
            helper->recordReadReq(is_inst, size);
            helper->sendTimingReq(pkt, is_inst);
        }
    }
}

void
RtlMemIfaceSram::acceptResp(PacketPtr pkt)
{
    if (pkt->isRead()) {
        read_data = 0;
        std::memcpy(&read_data, pkt->getPtr<uint8_t>(), std::min((uint32_t)sizeof(read_data), (uint32_t)pkt->getSize()));
        read_data_ready = true;
        helper->recordReadBeat();
    } else if (pkt->isWrite()) {
        write_done = true;
        helper->recordWriteResp();
    }
    delete pkt;
}

} // namespace gem5
