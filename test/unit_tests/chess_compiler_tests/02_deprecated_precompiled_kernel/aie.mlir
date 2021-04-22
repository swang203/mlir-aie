// UNSUPPORTED: arm
// RUN: xchessmk %S/chess_example/kernel.prx
// RUN: aiecc.py --sysroot=${VITIS_SYSROOT} %s -I%S/../../../../runtime_lib/ %S/../../../../runtime_lib/test_library.cpp %S/test.cpp -o test.elf

module @test_chess_02_deprecated_precompiled_kernel {
  %tile13 = AIE.tile(1, 3)

  %buf13_0 = AIE.buffer(%tile13) { sym_name = "a" } : memref<256xi32>
  %buf13_1 = AIE.buffer(%tile13) { sym_name = "b" } : memref<256xi32>

  %lock13_3 = AIE.lock(%tile13, 3)
  %lock13_5 = AIE.lock(%tile13, 5)

  %core13 = AIE.core(%tile13) { AIE.end } { elf_file = "aie.elf" }

}