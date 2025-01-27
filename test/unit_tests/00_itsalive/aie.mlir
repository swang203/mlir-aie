//===- aie.mlir ------------------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2021 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

// RUN: aiecc.py %s

module @test00_itsalive {
  %tile12 = AIE.tile(1, 2)

  %buf12_0 = AIE.buffer(%tile12) { sym_name = "a", address = 0 } : memref<256xi32>

  %core12 = AIE.core(%tile12) {
    %val1 = constant 1 : i32
    %idx1 = constant 3 : index
    %2 = addi %val1, %val1 : i32
    AIE.end
  }
}
