# Copyright 2026 Antigravity
# Description: Python declaration for RtlCPU SimObject.

from m5.objects.BaseCPU import BaseCPU
from m5.params import *
from m5.proxy import *


class RtlCPU(BaseCPU):
    type = "RtlCPU"
    cxx_header = "cpu/rtl/axi/rtl_cpu.hh"
    cxx_class = "gem5::RtlCPU"

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

    trace_enable = Param.Bool(False, "Enable VCD tracing of RTL signals")
    trace_file = Param.String("rtl_trace.vcd", "VCD trace filename")
    rtl_library = Param.String(
        "cva_verilate/libVcva6_top.so",
        "Path to the RTL shared library",
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
