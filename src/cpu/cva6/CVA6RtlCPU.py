# Copyright 2026 Antigravity
# Description: Python declaration for CVA6RtlCPU SimObject.

from m5.objects.BaseCPU import BaseCPU
from m5.params import *
from m5.proxy import *


class CVA6RtlCPU(BaseCPU):
    type = "CVA6RtlCPU"
    cxx_header = "cpu/cva6/cva6_rtl_cpu.hh"
    cxx_class = "gem5::CVA6RtlCPU"

    # Define architecture-specific classes required by BaseCPU
    from m5.objects.RiscvDecoder import RiscvDecoder
    from m5.objects.RiscvInterrupts import RiscvInterrupts
    from m5.objects.RiscvISA import RiscvISA
    from m5.objects.RiscvMMU import RiscvMMU

    ArchMMU = RiscvMMU
    ArchInterrupts = RiscvInterrupts
    ArchISA = RiscvISA
    ArchDecoder = RiscvDecoder

    inst_port = RequestPort("Instruction request port to memory")
    data_port = RequestPort("Data request port to memory")

    trace_enable = Param.Bool(False, "Enable VCD tracing of CVA6 RTL signals")
    trace_file = Param.String("cva6_trace.vcd", "VCD trace filename")
    rtl_library = Param.String(
        "cva6/work-ver-core/libVcva6_top.so",
        "Path to the Verilator CVA6 shared library",
    )

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        if not self.mmu:
            self.mmu = self.ArchMMU()
        if len(self.isa) == 0:
            self.isa = [self.ArchISA()]
        if len(self.decoder) == 0:
            self.decoder = [self.ArchDecoder(isa=self.isa[0])]
        if len(self.interrupts) == 0:
            self.createInterruptController()
