# Copyright 2026 Antigravity
# Description: Python declaration for CVA6RtlCPU SimObject.

from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *


class CVA6RtlCPU(ClockedObject):
    type = "CVA6RtlCPU"
    cxx_header = "cpu/cva6/cva6_rtl_cpu.hh"
    cxx_class = "gem5::CVA6RtlCPU"

    inst_port = RequestPort("Instruction request port to memory")
    data_port = RequestPort("Data request port to memory")
    system = Param.System(Parent.any, "System this CPU belongs to")

    trace_enable = Param.Bool(False, "Enable VCD tracing of CVA6 RTL signals")
    trace_file = Param.String("cva6_trace.vcd", "VCD trace filename")
    rtl_library = Param.String(
        "cva6/work-ver-core/libVcva6_top.so",
        "Path to the Verilator CVA6 shared library",
    )
