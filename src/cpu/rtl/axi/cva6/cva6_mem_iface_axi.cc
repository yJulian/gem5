// Copyright 2026 Antigravity
// Description: CVA6 specific AXI4 Memory Interface class implementation.

#include "cpu/rtl/axi/cva6/cva6_mem_iface_axi.hh"

namespace gem5
{

CVA6MemIfaceAxi::CVA6MemIfaceAxi(RtlCpuHelper *_helper, CVA6RtlCoreInterface *_core)
    : RtlMemIfaceAxi(_helper), core(_core)
{
}

} // namespace gem5
