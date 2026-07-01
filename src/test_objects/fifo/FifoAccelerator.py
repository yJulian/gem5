# Copyright 2026 Antigravity
# Description: Python declaration for FifoAccelerator SimObject.

from m5.objects.Device import DmaDevice
from m5.params import *
from m5.proxy import *


class FifoAccelerator(DmaDevice):
    type = "FifoAccelerator"
    cxx_header = "test_objects/fifo/fifo_accel.hh"
    cxx_class = "gem5::FifoAccelerator"

    pio_addr = Param.Addr("Address of MMIO registers")
    pio_size = Param.Addr(0x100, "Size of MMIO register space")
    pio_latency = Param.Latency("10ns", "Programmed IO latency")
    accel_library = Param.String(
        "accelerator/libfifo_accel.so",
        "Path to the Verilated accelerator shared library",
    )
    trace_enable = Param.Bool(False, "Enable VCD trace of accelerator")
    trace_file = Param.String("m5out/fifo_accel_trace.vcd", "Trace file name")
