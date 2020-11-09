// (c) Copyright 2019 Xilinx Inc. All Rights Reserved.
#include "AIEDialect.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/DenseSet.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Transforms/InliningUtils.h"

using namespace mlir;

namespace {
struct AIEInlinerInterface : public DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;
  // We don't have any special restrictions on what can be inlined into
  // destination regions. Always allow it.
  bool isLegalToInline(Region *dest, Region *src,
                       BlockAndValueMapping &valueMapping) const {
    return true;
  }
  // Operations in aie dialect are always legal to inline since they are
  // pure.
  bool isLegalToInline(Operation *, Region *,
                       BlockAndValueMapping &) const {
    return true;
  }
  // Handle the given inlined terminator by replacing it with a new operation
  // as necessary. Required when the inlined region has more than one block. 
  virtual void handleTerminator(Operation *op, Block *newDest) const {
    return;
  }
  // Handle the given inlined terminator by replacing it with a new operation
  // as necessary. Required when the region has only one block.
  void handleTerminator(Operation *op,
                        ArrayRef<Value> valuesToRepl) const final {
    return;
  }
};
} // end anonymous namespace

namespace xilinx {
namespace AIE {

// FIXME: use Tablegen'd dialect class
AIEDialect::AIEDialect(mlir::MLIRContext *ctx) : mlir::Dialect("AIE", ctx,
    ::mlir::TypeID::get<AIEDialect>()) {
  //addTypes<AIEListType>();
  addOperations<
#define GET_OP_LIST
#include "AIE.cpp.inc"
    >();
  addInterfaces<AIEInlinerInterface>();
}

} // namespace AIE
} // namespace xilinx

static LogicalResult verify(xilinx::AIE::TileOp op) {
  auto users = op.result().getUsers();
  bool found = false;
  for(auto user : users) {
    if(llvm::isa<xilinx::AIE::SwitchboxOp>(*user)) {
      assert(!found && "Tile can only have one switchbox");
      found = true;
    }
  }
  // assert((users.begin() == users.end() || users.begin().next() == users.end()) &&
  //   "Tile can only have one switchbox");

  return success();
}

static LogicalResult verify(xilinx::AIE::SwitchboxOp op) {
  Region &body = op.connections();
  DenseSet<xilinx::AIE::Port> sourceset;
  DenseSet<xilinx::AIE::Port> destset;
  assert(op.getOperation()->getNumRegions());
  assert(!body.empty());
  for (auto &ops : body.front()) {
    if(auto connectOp = dyn_cast<xilinx::AIE::ConnectOp>(ops)) {
      xilinx::AIE::Port source = std::make_pair(connectOp.sourceBundle(),
                                                connectOp.sourceIndex());
      sourceset.insert(source);

      xilinx::AIE::Port dest = std::make_pair(connectOp.destBundle(),
                                              connectOp.destIndex());
      if(destset.count(dest)) {
        return connectOp.emitOpError("targets same destination ") <<
          stringifyWireBundle(dest.first) << dest.second <<
          " as another connect operation";
      } else {
        destset.insert(dest);
      }
      if(connectOp.sourceIndex() < 0) {
        connectOp.emitOpError("source index cannot be less than zero");
      }
      if(connectOp.sourceIndex() >=
         op.getNumSourceConnections(connectOp.sourceBundle())) {
        connectOp.emitOpError("source index for source bundle ") <<
          stringifyWireBundle(connectOp.sourceBundle()) <<
          " must be less than " <<
          op.getNumSourceConnections(connectOp.sourceBundle());
      }
      if(connectOp.destIndex() < 0) {
        connectOp.emitOpError("dest index cannot be less than zero");
      }
      if(connectOp.destIndex() >=
         op.getNumDestConnections(connectOp.destBundle())) {
        connectOp.emitOpError("dest index for dest bundle ") <<
          stringifyWireBundle(connectOp.destBundle()) <<
          " must be less than " <<
          op.getNumDestConnections(connectOp.destBundle());
      }
    } else if(auto connectOp = dyn_cast<xilinx::AIE::MasterSetOp>(ops)) {
      xilinx::AIE::Port dest = std::make_pair(connectOp.destBundle(),
                                              connectOp.destIndex());
      if(destset.count(dest)) {
        return connectOp.emitOpError("targets same destination ") <<
          stringifyWireBundle(dest.first) << dest.second <<
          " as another connect or masterset operation";
      } else {
        destset.insert(dest);
      }
      if(connectOp.destIndex() < 0) {
        connectOp.emitOpError("dest index cannot be less than zero");
      }
      if(connectOp.destIndex() >=
         op.getNumDestConnections(connectOp.destBundle())) {
        connectOp.emitOpError("dest index for dest bundle ") <<
          stringifyWireBundle(connectOp.destBundle()) <<
          " must be less than " <<
          op.getNumDestConnections(connectOp.destBundle());
      }

      int arbiter = -1;
      for (auto val : connectOp.amsels()) {
        auto amsel = dyn_cast<xilinx::AIE::AMSelOp>(val.getDefiningOp());
        if ((arbiter != -1) && (arbiter != amsel.arbiterIndex()))
          connectOp.emitOpError("a master port can only be tied to one arbiter");
        arbiter = amsel.arbiterIndex();
      }
    } else if(auto connectOp = dyn_cast<xilinx::AIE::PacketRulesOp>(ops)) {
      xilinx::AIE::Port source = std::make_pair(connectOp.sourceBundle(),
                                                connectOp.sourceIndex());
      if(sourceset.count(source)) {
        return connectOp.emitOpError("packet switched source ") <<
          stringifyWireBundle(source.first) << source.second <<
          " cannot match another connect or masterset operation";
      } else {
        sourceset.insert(source);
      }
    } else if (auto amselOp = dyn_cast<xilinx::AIE::AMSelOp>(ops)) {
    } else if(auto endswitchOp = dyn_cast<xilinx::AIE::EndOp>(ops)) {
    } else {
      return ops.emitOpError("cannot be contained in a Switchbox op");
    }
  }

  return success();
}

static LogicalResult verify(xilinx::AIE::ShimSwitchboxOp op) {
  Region &body = op.connections();
  DenseSet<xilinx::AIE::Port> destset;
  assert(op.getOperation()->getNumRegions());
  assert(!body.empty());
  for (auto &ops : body.front()) {
    if(auto connectOp = dyn_cast<xilinx::AIE::ConnectOp>(ops)) {
      xilinx::AIE::Port dest = std::make_pair(connectOp.destBundle(),
                                              connectOp.destIndex());
      if(destset.count(dest)) {
        return connectOp.emitOpError("targets same destination ") <<
          stringifyWireBundle(dest.first) << dest.second << " as another connect operation";
      } else {
        destset.insert(dest);
      }
    } else if(auto endswitchOp = dyn_cast<xilinx::AIE::EndOp>(ops)) {
    } else {
      return ops.emitOpError("cannot be contained in a Switchbox op");
    }
  }

  return success();
}

static LogicalResult verify(xilinx::AIE::PacketFlowOp op) {
  Region &body = op.ports();
  //DenseSet<xilinx::AIE::Port> destset;
  assert(op.getOperation()->getNumRegions());
  assert(!body.empty());
  for (auto &ops : body.front()) {
    if(auto Op = dyn_cast<xilinx::AIE::PacketSourceOp>(ops)) {
    } else if(auto Op = dyn_cast<xilinx::AIE::PacketDestOp>(ops)) {
    } else if(auto endswitchOp = dyn_cast<xilinx::AIE::EndOp>(ops)) {
    } else {
      return ops.emitOpError("cannot be contained in a PacketFlow op");
    }
  }

  return success();
}

// CoreOp
static LogicalResult verify(xilinx::AIE::CoreOp op) {
  Region &body = op.body();
  assert(op.getOperation()->getNumRegions() == 1 && "CoreOp has zero region!");
  assert(!body.empty() && "CoreOp should have non-empty body");

  return success();
}

int xilinx::AIE::CoreOp::colIndex() {
  Operation *Op = tile().getDefiningOp();
  xilinx::AIE::TileOp tile = dyn_cast<xilinx::AIE::TileOp>(Op);

  return tile.colIndex();
}

int xilinx::AIE::CoreOp::rowIndex() {
  Operation *Op = tile().getDefiningOp();
  xilinx::AIE::TileOp tile = dyn_cast<xilinx::AIE::TileOp>(Op);

  return tile.rowIndex();
}

// MemOp
static LogicalResult verify(xilinx::AIE::MemOp op) {
  Region &body = op.body();
  assert(op.getOperation()->getNumRegions() == 1 && "MemOp has zero region!");
  assert(!body.empty() && "MemOp should have non-empty body");

  for (auto &bodyOp : body.getOps()) {
    if (auto allocOp = dyn_cast<AllocOp>(bodyOp)) {
      if (!allocOp.getAttr("id"))
        op.emitOpError() << "allocOp in MemOp region should have an id attribute\n";
    }
  }

  return success();
}

int xilinx::AIE::MemOp::colIndex() {
  Operation *Op = tile().getDefiningOp();
  xilinx::AIE::TileOp tile = dyn_cast<xilinx::AIE::TileOp>(Op);

  return tile.colIndex();
}

int xilinx::AIE::MemOp::rowIndex() {
  Operation *Op = tile().getDefiningOp();
  xilinx::AIE::TileOp tile = dyn_cast<xilinx::AIE::TileOp>(Op);

  return tile.rowIndex();
}

  /// Returns the region on the current operation that is callable. This may
  /// return null in the case of an external callable object, e.g. an external
  /// function.
  Region * xilinx::AIE::MemOp::getCallableRegion() { return &(body()); }

  /// Returns the results types that the callable region produces when executed.
  ArrayRef<Type> xilinx::AIE::MemOp::getCallableResults() { return getType(); }


int xilinx::AIE::SwitchboxOp::colIndex() {
  Operation *Op = tile().getDefiningOp();
  xilinx::AIE::TileOp tile = dyn_cast<xilinx::AIE::TileOp>(Op);

  return tile.colIndex();
}

int xilinx::AIE::SwitchboxOp::rowIndex() {
  Operation *Op = tile().getDefiningOp();
  xilinx::AIE::TileOp tile = dyn_cast<xilinx::AIE::TileOp>(Op);

  return tile.rowIndex();
}

static LogicalResult verify(xilinx::AIE::UseLockOp op) {
  xilinx::AIE::LockOp lockOp = dyn_cast_or_null<xilinx::AIE::LockOp>(op.lock().getDefiningOp());
//   if (!lockOp) {
//     op.emitOpError() << "Expected LockOp!\n";
// //    return failure();
//   }

  return success();
}

#include "AIEEnums.cpp.inc"

namespace xilinx {
#define GET_OP_CLASSES
#include "AIE.cpp.inc"
  namespace AIE {

    // void CoreOp::build(Builder *odsBuilder, OperationState &odsState, Type resultType0, int col, int row) {
    //   odsState.addOperands(colValue);
    //   odsState.addOperands(rowValue);
    //   odsState.addTypes(resultType0);
    // }

  //#include "ATenOpInterfaces.cpp.inc"

    int SwitchboxOp::getNumSourceConnections(WireBundle bundle) {
      switch(bundle) {
      case WireBundle::ME: return 2;
      case WireBundle::DMA: return 2;
      case WireBundle::North: return 4;
      case WireBundle::West: return 4;
      case WireBundle::South: return 6;
      case WireBundle::East: return 4;
      default: return 0;
      }
    }
    int SwitchboxOp::getNumDestConnections(WireBundle bundle) {
      switch(bundle) {
      case WireBundle::ME: return 2;
      case WireBundle::DMA: return 2;
      case WireBundle::North: return 6;
      case WireBundle::West: return 4;
      case WireBundle::South: return 4;
      case WireBundle::East: return 4;
      default: return 0;
      }
    }
    int TileOp::getNumSourceConnections(WireBundle bundle) {
      switch(bundle) {
      case WireBundle::ME: return 2;
      case WireBundle::DMA: return 2;
      default: return 0;
      }
    }
    int TileOp::getNumDestConnections(WireBundle bundle) {
      switch(bundle) {
      case WireBundle::ME: return 2;
      case WireBundle::DMA: return 2;
      default: return 0;
      }
    }

  } // namespace AIE
} // namespace xilinx
