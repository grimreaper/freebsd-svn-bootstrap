//===-- CodeGenFunction.h - Per-Function state for LLVM CodeGen -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the internal per-function state used for llvm translation.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CODEGEN_CODEGENFUNCTION_H
#define CLANG_CODEGEN_CODEGENFUNCTION_H

#include "clang/AST/Type.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/CharUnits.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ValueHandle.h"
#include "CodeGenModule.h"
#include "CGBlocks.h"
#include "CGBuilder.h"
#include "CGCall.h"
#include "CGCXX.h"
#include "CGValue.h"

namespace llvm {
  class BasicBlock;
  class LLVMContext;
  class MDNode;
  class Module;
  class SwitchInst;
  class Twine;
  class Value;
}

namespace clang {
  class ASTContext;
  class CXXDestructorDecl;
  class CXXTryStmt;
  class Decl;
  class EnumConstantDecl;
  class FunctionDecl;
  class FunctionProtoType;
  class LabelStmt;
  class ObjCContainerDecl;
  class ObjCInterfaceDecl;
  class ObjCIvarDecl;
  class ObjCMethodDecl;
  class ObjCImplementationDecl;
  class ObjCPropertyImplDecl;
  class TargetInfo;
  class TargetCodeGenInfo;
  class VarDecl;
  class ObjCForCollectionStmt;
  class ObjCAtTryStmt;
  class ObjCAtThrowStmt;
  class ObjCAtSynchronizedStmt;

namespace CodeGen {
  class CodeGenTypes;
  class CGDebugInfo;
  class CGFunctionInfo;
  class CGRecordLayout;
  class CGBlockInfo;

/// CodeGenFunction - This class organizes the per-function state that is used
/// while generating LLVM code.
class CodeGenFunction : public BlockFunction {
  CodeGenFunction(const CodeGenFunction&); // DO NOT IMPLEMENT
  void operator=(const CodeGenFunction&);  // DO NOT IMPLEMENT
public:
  CodeGenModule &CGM;  // Per-module state.
  const TargetInfo &Target;

  typedef std::pair<llvm::Value *, llvm::Value *> ComplexPairTy;
  CGBuilderTy Builder;

  /// CurFuncDecl - Holds the Decl for the current function or ObjC method.
  /// This excludes BlockDecls.
  const Decl *CurFuncDecl;
  /// CurCodeDecl - This is the inner-most code context, which includes blocks.
  const Decl *CurCodeDecl;
  const CGFunctionInfo *CurFnInfo;
  QualType FnRetTy;
  llvm::Function *CurFn;

  /// CurGD - The GlobalDecl for the current function being compiled.
  GlobalDecl CurGD;

  /// ReturnBlock - Unified return block.
  llvm::BasicBlock *ReturnBlock;
  /// ReturnValue - The temporary alloca to hold the return value. This is null
  /// iff the function has no return value.
  llvm::Value *ReturnValue;

  /// AllocaInsertPoint - This is an instruction in the entry block before which
  /// we prefer to insert allocas.
  llvm::AssertingVH<llvm::Instruction> AllocaInsertPt;

  const llvm::Type *LLVMIntTy;
  uint32_t LLVMPointerWidth;

  bool Exceptions;
  bool CatchUndefined;
  
  /// \brief A mapping from NRVO variables to the flags used to indicate
  /// when the NRVO has been applied to this variable.
  llvm::DenseMap<const VarDecl *, llvm::Value *> NRVOFlags;
  
public:
  /// ObjCEHValueStack - Stack of Objective-C exception values, used for
  /// rethrows.
  llvm::SmallVector<llvm::Value*, 8> ObjCEHValueStack;

  /// PushCleanupBlock - Push a new cleanup entry on the stack and set the
  /// passed in block as the cleanup block.
  void PushCleanupBlock(llvm::BasicBlock *CleanupEntryBlock,
                        llvm::BasicBlock *CleanupExitBlock,
                        llvm::BasicBlock *PreviousInvokeDest,
                        bool EHOnly = false);
  void PushCleanupBlock(llvm::BasicBlock *CleanupEntryBlock) {
    PushCleanupBlock(CleanupEntryBlock, 0, getInvokeDest(), false);
  }

  /// CleanupBlockInfo - A struct representing a popped cleanup block.
  struct CleanupBlockInfo {
    /// CleanupEntryBlock - the cleanup entry block
    llvm::BasicBlock *CleanupBlock;

    /// SwitchBlock - the block (if any) containing the switch instruction used
    /// for jumping to the final destination.
    llvm::BasicBlock *SwitchBlock;

    /// EndBlock - the default destination for the switch instruction.
    llvm::BasicBlock *EndBlock;

    /// EHOnly - True iff this cleanup should only be performed on the
    /// exceptional edge.
    bool EHOnly;

    CleanupBlockInfo(llvm::BasicBlock *cb, llvm::BasicBlock *sb,
                     llvm::BasicBlock *eb, bool ehonly = false)
      : CleanupBlock(cb), SwitchBlock(sb), EndBlock(eb), EHOnly(ehonly) {}
  };

  /// EHCleanupBlock - RAII object that will create a cleanup block for the
  /// exceptional edge and set the insert point to that block.  When destroyed,
  /// it creates the cleanup edge and sets the insert point to the previous
  /// block.
  class EHCleanupBlock {
    CodeGenFunction& CGF;
    llvm::BasicBlock *PreviousInsertionBlock;
    llvm::BasicBlock *CleanupHandler;
    llvm::BasicBlock *PreviousInvokeDest;
  public:
    EHCleanupBlock(CodeGenFunction &cgf) 
      : CGF(cgf),
        PreviousInsertionBlock(CGF.Builder.GetInsertBlock()),
        CleanupHandler(CGF.createBasicBlock("ehcleanup", CGF.CurFn)),
        PreviousInvokeDest(CGF.getInvokeDest()) {
      llvm::BasicBlock *TerminateHandler = CGF.getTerminateHandler();
      CGF.Builder.SetInsertPoint(CleanupHandler);
      CGF.setInvokeDest(TerminateHandler);
    }
    ~EHCleanupBlock();
  };

  /// PopCleanupBlock - Will pop the cleanup entry on the stack, process all
  /// branch fixups and return a block info struct with the switch block and end
  /// block.  This will also reset the invoke handler to the previous value
  /// from when the cleanup block was created.
  CleanupBlockInfo PopCleanupBlock();

  /// DelayedCleanupBlock - RAII object that will create a cleanup block and set
  /// the insert point to that block. When destructed, it sets the insert point
  /// to the previous block and pushes a new cleanup entry on the stack.
  class DelayedCleanupBlock {
    CodeGenFunction& CGF;
    llvm::BasicBlock *CurBB;
    llvm::BasicBlock *CleanupEntryBB;
    llvm::BasicBlock *CleanupExitBB;
    llvm::BasicBlock *CurInvokeDest;
    bool EHOnly;
    
  public:
    DelayedCleanupBlock(CodeGenFunction &cgf, bool ehonly = false)
      : CGF(cgf), CurBB(CGF.Builder.GetInsertBlock()),
        CleanupEntryBB(CGF.createBasicBlock("cleanup")),
        CleanupExitBB(0),
        CurInvokeDest(CGF.getInvokeDest()),
        EHOnly(ehonly) {
      CGF.Builder.SetInsertPoint(CleanupEntryBB);
    }

    llvm::BasicBlock *getCleanupExitBlock() {
      if (!CleanupExitBB)
        CleanupExitBB = CGF.createBasicBlock("cleanup.exit");
      return CleanupExitBB;
    }
    
    ~DelayedCleanupBlock() {
      CGF.PushCleanupBlock(CleanupEntryBB, CleanupExitBB, CurInvokeDest,
                           EHOnly);
      // FIXME: This is silly, move this into the builder.
      if (CurBB)
        CGF.Builder.SetInsertPoint(CurBB);
      else
        CGF.Builder.ClearInsertionPoint();
    }
  };

  /// \brief Enters a new scope for capturing cleanups, all of which will be
  /// executed once the scope is exited.
  class CleanupScope {
    CodeGenFunction& CGF;
    size_t CleanupStackDepth;
    bool OldDidCallStackSave;
    bool PerformCleanup;

    CleanupScope(const CleanupScope &); // DO NOT IMPLEMENT
    CleanupScope &operator=(const CleanupScope &); // DO NOT IMPLEMENT

  public:
    /// \brief Enter a new cleanup scope.
    explicit CleanupScope(CodeGenFunction &CGF) 
      : CGF(CGF), PerformCleanup(true) 
    {
      CleanupStackDepth = CGF.CleanupEntries.size();
      OldDidCallStackSave = CGF.DidCallStackSave;
    }

    /// \brief Exit this cleanup scope, emitting any accumulated
    /// cleanups.
    ~CleanupScope() {
      if (PerformCleanup) {
        CGF.DidCallStackSave = OldDidCallStackSave;
        CGF.EmitCleanupBlocks(CleanupStackDepth);
      }
    }

    /// \brief Determine whether this scope requires any cleanups.
    bool requiresCleanups() const {
      return CGF.CleanupEntries.size() > CleanupStackDepth;
    }

    /// \brief Force the emission of cleanups now, instead of waiting
    /// until this object is destroyed.
    void ForceCleanup() {
      assert(PerformCleanup && "Already forced cleanup");
      CGF.DidCallStackSave = OldDidCallStackSave;
      CGF.EmitCleanupBlocks(CleanupStackDepth);
      PerformCleanup = false;
    }
  };

  /// CXXTemporariesCleanupScope - Enters a new scope for catching live
  /// temporaries, all of which will be popped once the scope is exited.
  class CXXTemporariesCleanupScope {
    CodeGenFunction &CGF;
    size_t NumLiveTemporaries;
    
    // DO NOT IMPLEMENT
    CXXTemporariesCleanupScope(const CXXTemporariesCleanupScope &); 
    CXXTemporariesCleanupScope &operator=(const CXXTemporariesCleanupScope &);
    
  public:
    explicit CXXTemporariesCleanupScope(CodeGenFunction &CGF)
      : CGF(CGF), NumLiveTemporaries(CGF.LiveTemporaries.size()) { }
    
    ~CXXTemporariesCleanupScope() {
      while (CGF.LiveTemporaries.size() > NumLiveTemporaries)
        CGF.PopCXXTemporary();
    }
  };


  /// EmitCleanupBlocks - Takes the old cleanup stack size and emits the cleanup
  /// blocks that have been added.
  void EmitCleanupBlocks(size_t OldCleanupStackSize);

  /// EmitBranchThroughCleanup - Emit a branch from the current insert block
  /// through the cleanup handling code (if any) and then on to \arg Dest.
  ///
  /// FIXME: Maybe this should really be in EmitBranch? Don't we always want
  /// this behavior for branches?
  void EmitBranchThroughCleanup(llvm::BasicBlock *Dest);

  /// BeginConditionalBranch - Should be called before a conditional part of an
  /// expression is emitted. For example, before the RHS of the expression below
  /// is emitted:
  ///
  /// b && f(T());
  ///
  /// This is used to make sure that any temporaries created in the conditional
  /// branch are only destroyed if the branch is taken.
  void BeginConditionalBranch() {
    ++ConditionalBranchLevel;
  }

  /// EndConditionalBranch - Should be called after a conditional part of an
  /// expression has been emitted.
  void EndConditionalBranch() {
    assert(ConditionalBranchLevel != 0 &&
           "Conditional branch mismatch!");
    
    --ConditionalBranchLevel;
  }

private:
  CGDebugInfo *DebugInfo;

  /// IndirectBranch - The first time an indirect goto is seen we create a block
  /// with an indirect branch.  Every time we see the address of a label taken,
  /// we add the label to the indirect goto.  Every subsequent indirect goto is
  /// codegen'd as a jump to the IndirectBranch's basic block.
  llvm::IndirectBrInst *IndirectBranch;

  /// LocalDeclMap - This keeps track of the LLVM allocas or globals for local C
  /// decls.
  llvm::DenseMap<const Decl*, llvm::Value*> LocalDeclMap;

  /// LabelMap - This keeps track of the LLVM basic block for each C label.
  llvm::DenseMap<const LabelStmt*, llvm::BasicBlock*> LabelMap;

  // BreakContinueStack - This keeps track of where break and continue
  // statements should jump to.
  struct BreakContinue {
    BreakContinue(llvm::BasicBlock *bb, llvm::BasicBlock *cb)
      : BreakBlock(bb), ContinueBlock(cb) {}

    llvm::BasicBlock *BreakBlock;
    llvm::BasicBlock *ContinueBlock;
  };
  llvm::SmallVector<BreakContinue, 8> BreakContinueStack;

  /// SwitchInsn - This is nearest current switch instruction. It is null if if
  /// current context is not in a switch.
  llvm::SwitchInst *SwitchInsn;

  /// CaseRangeBlock - This block holds if condition check for last case
  /// statement range in current switch instruction.
  llvm::BasicBlock *CaseRangeBlock;

  /// InvokeDest - This is the nearest exception target for calls
  /// which can unwind, when exceptions are being used.
  llvm::BasicBlock *InvokeDest;

  // VLASizeMap - This keeps track of the associated size for each VLA type.
  // We track this by the size expression rather than the type itself because
  // in certain situations, like a const qualifier applied to an VLA typedef,
  // multiple VLA types can share the same size expression.
  // FIXME: Maybe this could be a stack of maps that is pushed/popped as we
  // enter/leave scopes.
  llvm::DenseMap<const Expr*, llvm::Value*> VLASizeMap;

  /// DidCallStackSave - Whether llvm.stacksave has been called. Used to avoid
  /// calling llvm.stacksave for multiple VLAs in the same scope.
  bool DidCallStackSave;

  struct CleanupEntry {
    /// CleanupEntryBlock - The block of code that does the actual cleanup.
    llvm::BasicBlock *CleanupEntryBlock;

    /// CleanupExitBlock - The cleanup exit block.
    llvm::BasicBlock *CleanupExitBlock;
    
    /// Blocks - Basic blocks that were emitted in the current cleanup scope.
    std::vector<llvm::BasicBlock *> Blocks;

    /// BranchFixups - Branch instructions to basic blocks that haven't been
    /// inserted into the current function yet.
    std::vector<llvm::BranchInst *> BranchFixups;

    /// PreviousInvokeDest - The invoke handler from the start of the cleanup
    /// region.
    llvm::BasicBlock *PreviousInvokeDest;

    /// EHOnly - Perform this only on the exceptional edge, not the main edge.
    bool EHOnly;

    explicit CleanupEntry(llvm::BasicBlock *CleanupEntryBlock,
                          llvm::BasicBlock *CleanupExitBlock,
                          llvm::BasicBlock *PreviousInvokeDest,
                          bool ehonly)
      : CleanupEntryBlock(CleanupEntryBlock),
        CleanupExitBlock(CleanupExitBlock),
        PreviousInvokeDest(PreviousInvokeDest),
        EHOnly(ehonly) {}
  };

  /// CleanupEntries - Stack of cleanup entries.
  llvm::SmallVector<CleanupEntry, 8> CleanupEntries;

  typedef llvm::DenseMap<llvm::BasicBlock*, size_t> BlockScopeMap;

  /// BlockScopes - Map of which "cleanup scope" scope basic blocks have.
  BlockScopeMap BlockScopes;

  /// CXXThisDecl - When generating code for a C++ member function,
  /// this will hold the implicit 'this' declaration.
  ImplicitParamDecl *CXXThisDecl;
  llvm::Value *CXXThisValue;

  /// CXXVTTDecl - When generating code for a base object constructor or
  /// base object destructor with virtual bases, this will hold the implicit
  /// VTT parameter.
  ImplicitParamDecl *CXXVTTDecl;
  llvm::Value *CXXVTTValue;
  
  /// CXXLiveTemporaryInfo - Holds information about a live C++ temporary.
  struct CXXLiveTemporaryInfo {
    /// Temporary - The live temporary.
    const CXXTemporary *Temporary;

    /// ThisPtr - The pointer to the temporary.
    llvm::Value *ThisPtr;

    /// DtorBlock - The destructor block.
    llvm::BasicBlock *DtorBlock;

    /// CondPtr - If this is a conditional temporary, this is the pointer to the
    /// condition variable that states whether the destructor should be called
    /// or not.
    llvm::Value *CondPtr;

    CXXLiveTemporaryInfo(const CXXTemporary *temporary,
                         llvm::Value *thisptr, llvm::BasicBlock *dtorblock,
                         llvm::Value *condptr)
      : Temporary(temporary), ThisPtr(thisptr), DtorBlock(dtorblock),
      CondPtr(condptr) { }
  };

  llvm::SmallVector<CXXLiveTemporaryInfo, 4> LiveTemporaries;

  /// ConditionalBranchLevel - Contains the nesting level of the current
  /// conditional branch. This is used so that we know if a temporary should be
  /// destroyed conditionally.
  unsigned ConditionalBranchLevel;


  /// ByrefValueInfoMap - For each __block variable, contains a pair of the LLVM
  /// type as well as the field number that contains the actual data.
  llvm::DenseMap<const ValueDecl *, std::pair<const llvm::Type *, 
                                              unsigned> > ByRefValueInfo;
  
  /// getByrefValueFieldNumber - Given a declaration, returns the LLVM field
  /// number that holds the value.
  unsigned getByRefValueLLVMField(const ValueDecl *VD) const;

  llvm::BasicBlock *TerminateHandler;
  llvm::BasicBlock *TrapBB;

  int UniqueAggrDestructorCount;
public:
  CodeGenFunction(CodeGenModule &cgm);

  ASTContext &getContext() const;
  CGDebugInfo *getDebugInfo() { return DebugInfo; }

  llvm::BasicBlock *getInvokeDest() { return InvokeDest; }
  void setInvokeDest(llvm::BasicBlock *B) { InvokeDest = B; }

  llvm::LLVMContext &getLLVMContext() { return VMContext; }

  //===--------------------------------------------------------------------===//
  //                                  Objective-C
  //===--------------------------------------------------------------------===//

  void GenerateObjCMethod(const ObjCMethodDecl *OMD);

  void StartObjCMethod(const ObjCMethodDecl *MD,
                       const ObjCContainerDecl *CD);

  /// GenerateObjCGetter - Synthesize an Objective-C property getter function.
  void GenerateObjCGetter(ObjCImplementationDecl *IMP,
                          const ObjCPropertyImplDecl *PID);
  void GenerateObjCCtorDtorMethod(ObjCImplementationDecl *IMP,
                                  ObjCMethodDecl *MD, bool ctor);

  /// GenerateObjCSetter - Synthesize an Objective-C property setter function
  /// for the given property.
  void GenerateObjCSetter(ObjCImplementationDecl *IMP,
                          const ObjCPropertyImplDecl *PID);
  bool IndirectObjCSetterArg(const CGFunctionInfo &FI);
  bool IvarTypeWithAggrGCObjects(QualType Ty);

  //===--------------------------------------------------------------------===//
  //                                  Block Bits
  //===--------------------------------------------------------------------===//

  llvm::Value *BuildBlockLiteralTmp(const BlockExpr *);
  llvm::Constant *BuildDescriptorBlockDecl(const BlockExpr *,
                                           bool BlockHasCopyDispose,
                                           CharUnits Size,
                                           const llvm::StructType *,
                                           std::vector<HelperInfo> *);

  llvm::Function *GenerateBlockFunction(const BlockExpr *BExpr,
                                        CGBlockInfo &Info,
                                        const Decl *OuterFuncDecl,
                                  llvm::DenseMap<const Decl*, llvm::Value*> ldm);

  llvm::Value *LoadBlockStruct();

  void AllocateBlockCXXThisPointer(const CXXThisExpr *E);
  void AllocateBlockDecl(const BlockDeclRefExpr *E);
  llvm::Value *GetAddrOfBlockDecl(const BlockDeclRefExpr *E) {
    return GetAddrOfBlockDecl(E->getDecl(), E->isByRef());
  }
  llvm::Value *GetAddrOfBlockDecl(const ValueDecl *D, bool ByRef);
  const llvm::Type *BuildByRefType(const ValueDecl *D);

  void GenerateCode(GlobalDecl GD, llvm::Function *Fn);
  void StartFunction(GlobalDecl GD, QualType RetTy,
                     llvm::Function *Fn,
                     const FunctionArgList &Args,
                     SourceLocation StartLoc);

  void EmitConstructorBody(FunctionArgList &Args);
  void EmitDestructorBody(FunctionArgList &Args);
  void EmitFunctionBody(FunctionArgList &Args);

  /// EmitReturnBlock - Emit the unified return block, trying to avoid its
  /// emission when possible.
  void EmitReturnBlock();

  /// FinishFunction - Complete IR generation of the current function. It is
  /// legal to call this function even if there is no current insertion point.
  void FinishFunction(SourceLocation EndLoc=SourceLocation());

  /// GenerateThunk - Generate a thunk for the given method.
  void GenerateThunk(llvm::Function *Fn, GlobalDecl GD, const ThunkInfo &Thunk);
  
  void EmitCtorPrologue(const CXXConstructorDecl *CD, CXXCtorType Type,
                        FunctionArgList &Args);

  /// InitializeVTablePointer - Initialize the vtable pointer of the given
  /// subobject.
  ///
  void InitializeVTablePointer(BaseSubobject Base, 
                               const CXXRecordDecl *NearestVBase,
                               uint64_t OffsetFromNearestVBase,
                               llvm::Constant *VTable,
                               const CXXRecordDecl *VTableClass);

  typedef llvm::SmallPtrSet<const CXXRecordDecl *, 4> VisitedVirtualBasesSetTy;
  void InitializeVTablePointers(BaseSubobject Base, 
                                const CXXRecordDecl *NearestVBase,
                                uint64_t OffsetFromNearestVBase,
                                bool BaseIsNonVirtualPrimaryBase,
                                llvm::Constant *VTable,
                                const CXXRecordDecl *VTableClass,
                                VisitedVirtualBasesSetTy& VBases);

  void InitializeVTablePointers(const CXXRecordDecl *ClassDecl);


  /// EmitDtorEpilogue - Emit all code that comes at the end of class's
  /// destructor. This is to call destructors on members and base classes in
  /// reverse order of their construction.
  void EmitDtorEpilogue(const CXXDestructorDecl *Dtor,
                        CXXDtorType Type);

  /// EmitFunctionProlog - Emit the target specific LLVM code to load the
  /// arguments for the given function. This is also responsible for naming the
  /// LLVM function arguments.
  void EmitFunctionProlog(const CGFunctionInfo &FI,
                          llvm::Function *Fn,
                          const FunctionArgList &Args);

  /// EmitFunctionEpilog - Emit the target specific LLVM code to return the
  /// given temporary.
  void EmitFunctionEpilog(const CGFunctionInfo &FI, llvm::Value *ReturnValue);

  /// EmitStartEHSpec - Emit the start of the exception spec.
  void EmitStartEHSpec(const Decl *D);

  /// EmitEndEHSpec - Emit the end of the exception spec.
  void EmitEndEHSpec(const Decl *D);

  /// getTerminateHandler - Return a handler that just calls terminate.
  llvm::BasicBlock *getTerminateHandler();

  const llvm::Type *ConvertTypeForMem(QualType T);
  const llvm::Type *ConvertType(QualType T);
  const llvm::Type *ConvertType(const TypeDecl *T) {
    return ConvertType(getContext().getTypeDeclType(T));
  }

  /// LoadObjCSelf - Load the value of self. This function is only valid while
  /// generating code for an Objective-C method.
  llvm::Value *LoadObjCSelf();

  /// TypeOfSelfObject - Return type of object that this self represents.
  QualType TypeOfSelfObject();

  /// hasAggregateLLVMType - Return true if the specified AST type will map into
  /// an aggregate LLVM type or is void.
  static bool hasAggregateLLVMType(QualType T);

  /// createBasicBlock - Create an LLVM basic block.
  llvm::BasicBlock *createBasicBlock(const char *Name="",
                                     llvm::Function *Parent=0,
                                     llvm::BasicBlock *InsertBefore=0) {
#ifdef NDEBUG
    return llvm::BasicBlock::Create(VMContext, "", Parent, InsertBefore);
#else
    return llvm::BasicBlock::Create(VMContext, Name, Parent, InsertBefore);
#endif
  }

  /// getBasicBlockForLabel - Return the LLVM basicblock that the specified
  /// label maps to.
  llvm::BasicBlock *getBasicBlockForLabel(const LabelStmt *S);

  /// SimplifyForwardingBlocks - If the given basic block is only a branch to
  /// another basic block, simplify it. This assumes that no other code could
  /// potentially reference the basic block.
  void SimplifyForwardingBlocks(llvm::BasicBlock *BB);

  /// EmitBlock - Emit the given block \arg BB and set it as the insert point,
  /// adding a fall-through branch from the current insert block if
  /// necessary. It is legal to call this function even if there is no current
  /// insertion point.
  ///
  /// IsFinished - If true, indicates that the caller has finished emitting
  /// branches to the given block and does not expect to emit code into it. This
  /// means the block can be ignored if it is unreachable.
  void EmitBlock(llvm::BasicBlock *BB, bool IsFinished=false);

  /// EmitBranch - Emit a branch to the specified basic block from the current
  /// insert block, taking care to avoid creation of branches from dummy
  /// blocks. It is legal to call this function even if there is no current
  /// insertion point.
  ///
  /// This function clears the current insertion point. The caller should follow
  /// calls to this function with calls to Emit*Block prior to generation new
  /// code.
  void EmitBranch(llvm::BasicBlock *Block);

  /// HaveInsertPoint - True if an insertion point is defined. If not, this
  /// indicates that the current code being emitted is unreachable.
  bool HaveInsertPoint() const {
    return Builder.GetInsertBlock() != 0;
  }

  /// EnsureInsertPoint - Ensure that an insertion point is defined so that
  /// emitted IR has a place to go. Note that by definition, if this function
  /// creates a block then that block is unreachable; callers may do better to
  /// detect when no insertion point is defined and simply skip IR generation.
  void EnsureInsertPoint() {
    if (!HaveInsertPoint())
      EmitBlock(createBasicBlock());
  }

  /// ErrorUnsupported - Print out an error that codegen doesn't support the
  /// specified stmt yet.
  void ErrorUnsupported(const Stmt *S, const char *Type,
                        bool OmitOnError=false);

  //===--------------------------------------------------------------------===//
  //                                  Helpers
  //===--------------------------------------------------------------------===//

  Qualifiers MakeQualifiers(QualType T) {
    Qualifiers Quals = getContext().getCanonicalType(T).getQualifiers();
    Quals.setObjCGCAttr(getContext().getObjCGCAttrKind(T));
    return Quals;
  }

  /// CreateTempAlloca - This creates a alloca and inserts it into the entry
  /// block. The caller is responsible for setting an appropriate alignment on
  /// the alloca.
  llvm::AllocaInst *CreateTempAlloca(const llvm::Type *Ty,
                                     const llvm::Twine &Name = "tmp");

  /// InitTempAlloca - Provide an initial value for the given alloca.
  void InitTempAlloca(llvm::AllocaInst *Alloca, llvm::Value *Value);

  /// CreateIRTemp - Create a temporary IR object of the given type, with
  /// appropriate alignment. This routine should only be used when an temporary
  /// value needs to be stored into an alloca (for example, to avoid explicit
  /// PHI construction), but the type is the IR type, not the type appropriate
  /// for storing in memory.
  llvm::Value *CreateIRTemp(QualType T, const llvm::Twine &Name = "tmp");

  /// CreateMemTemp - Create a temporary memory object of the given type, with
  /// appropriate alignment.
  llvm::Value *CreateMemTemp(QualType T, const llvm::Twine &Name = "tmp");

  /// EvaluateExprAsBool - Perform the usual unary conversions on the specified
  /// expression and compare the result against zero, returning an Int1Ty value.
  llvm::Value *EvaluateExprAsBool(const Expr *E);

  /// EmitAnyExpr - Emit code to compute the specified expression which can have
  /// any type.  The result is returned as an RValue struct.  If this is an
  /// aggregate expression, the aggloc/agglocvolatile arguments indicate where
  /// the result should be returned.
  ///
  /// \param IgnoreResult - True if the resulting value isn't used.
  RValue EmitAnyExpr(const Expr *E, llvm::Value *AggLoc = 0,
                     bool IsAggLocVolatile = false, bool IgnoreResult = false,
                     bool IsInitializer = false);

  // EmitVAListRef - Emit a "reference" to a va_list; this is either the address
  // or the value of the expression, depending on how va_list is defined.
  llvm::Value *EmitVAListRef(const Expr *E);

  /// EmitAnyExprToTemp - Similary to EmitAnyExpr(), however, the result will
  /// always be accessible even if no aggregate location is provided.
  RValue EmitAnyExprToTemp(const Expr *E, bool IsAggLocVolatile = false,
                           bool IsInitializer = false);

  /// EmitsAnyExprToMem - Emits the code necessary to evaluate an
  /// arbitrary expression into the given memory location.
  void EmitAnyExprToMem(const Expr *E, llvm::Value *Location,
                        bool IsLocationVolatile = false,
                        bool IsInitializer = false);

  /// EmitAggregateCopy - Emit an aggrate copy.
  ///
  /// \param isVolatile - True iff either the source or the destination is
  /// volatile.
  void EmitAggregateCopy(llvm::Value *DestPtr, llvm::Value *SrcPtr,
                         QualType EltTy, bool isVolatile=false);

  /// StartBlock - Start new block named N. If insert block is a dummy block
  /// then reuse it.
  void StartBlock(const char *N);

  /// GetAddrOfStaticLocalVar - Return the address of a static local variable.
  llvm::Constant *GetAddrOfStaticLocalVar(const VarDecl *BVD);

  /// GetAddrOfLocalVar - Return the address of a local variable.
  llvm::Value *GetAddrOfLocalVar(const VarDecl *VD);

  /// getAccessedFieldNo - Given an encoded value and a result number, return
  /// the input field number being accessed.
  static unsigned getAccessedFieldNo(unsigned Idx, const llvm::Constant *Elts);

  llvm::BlockAddress *GetAddrOfLabel(const LabelStmt *L);
  llvm::BasicBlock *GetIndirectGotoBlock();

  /// EmitNullInitialization - Generate code to set a value of the given type to
  /// null, If the type contains data member pointers, they will be initialized
  /// to -1 in accordance with the Itanium C++ ABI.
  void EmitNullInitialization(llvm::Value *DestPtr, QualType Ty);

  // EmitVAArg - Generate code to get an argument from the passed in pointer
  // and update it accordingly. The return value is a pointer to the argument.
  // FIXME: We should be able to get rid of this method and use the va_arg
  // instruction in LLVM instead once it works well enough.
  llvm::Value *EmitVAArg(llvm::Value *VAListAddr, QualType Ty);

  /// EmitVLASize - Generate code for any VLA size expressions that might occur
  /// in a variably modified type. If Ty is a VLA, will return the value that
  /// corresponds to the size in bytes of the VLA type. Will return 0 otherwise.
  ///
  /// This function can be called with a null (unreachable) insert point.
  llvm::Value *EmitVLASize(QualType Ty);

  // GetVLASize - Returns an LLVM value that corresponds to the size in bytes
  // of a variable length array type.
  llvm::Value *GetVLASize(const VariableArrayType *);

  /// LoadCXXThis - Load the value of 'this'. This function is only valid while
  /// generating code for an C++ member function.
  llvm::Value *LoadCXXThis() {
    assert(CXXThisValue && "no 'this' value for this function");
    return CXXThisValue;
  }

  /// LoadCXXVTT - Load the VTT parameter to base constructors/destructors have
  /// virtual bases.
  llvm::Value *LoadCXXVTT() {
    assert(CXXVTTValue && "no VTT value for this function");
    return CXXVTTValue;
  }

  /// GetAddressOfBaseOfCompleteClass - Convert the given pointer to a
  /// complete class to the given direct base.
  llvm::Value *
  GetAddressOfDirectBaseInCompleteClass(llvm::Value *Value,
                                        const CXXRecordDecl *Derived,
                                        const CXXRecordDecl *Base,
                                        bool BaseIsVirtual);

  /// GetAddressOfBaseClass - This function will add the necessary delta to the
  /// load of 'this' and returns address of the base class.
  llvm::Value *GetAddressOfBaseClass(llvm::Value *Value, 
                                     const CXXRecordDecl *Derived,
                                     const CXXBaseSpecifierArray &BasePath, 
                                     bool NullCheckValue);

  llvm::Value *GetAddressOfDerivedClass(llvm::Value *Value,
                                        const CXXRecordDecl *Derived,
                                        const CXXBaseSpecifierArray &BasePath,
                                        bool NullCheckValue);

  llvm::Value *GetVirtualBaseClassOffset(llvm::Value *This,
                                         const CXXRecordDecl *ClassDecl,
                                         const CXXRecordDecl *BaseClassDecl);
    
  void EmitDelegateCXXConstructorCall(const CXXConstructorDecl *Ctor,
                                      CXXCtorType CtorType,
                                      const FunctionArgList &Args);
  void EmitCXXConstructorCall(const CXXConstructorDecl *D, CXXCtorType Type,
                              bool ForVirtualBase, llvm::Value *This,
                              CallExpr::const_arg_iterator ArgBeg,
                              CallExpr::const_arg_iterator ArgEnd);

  void EmitCXXAggrConstructorCall(const CXXConstructorDecl *D,
                                  const ConstantArrayType *ArrayTy,
                                  llvm::Value *ArrayPtr,
                                  CallExpr::const_arg_iterator ArgBeg,
                                  CallExpr::const_arg_iterator ArgEnd);
  
  void EmitCXXAggrConstructorCall(const CXXConstructorDecl *D,
                                  llvm::Value *NumElements,
                                  llvm::Value *ArrayPtr,
                                  CallExpr::const_arg_iterator ArgBeg,
                                  CallExpr::const_arg_iterator ArgEnd);

  void EmitCXXAggrDestructorCall(const CXXDestructorDecl *D,
                                 const ArrayType *Array,
                                 llvm::Value *This);

  void EmitCXXAggrDestructorCall(const CXXDestructorDecl *D,
                                 llvm::Value *NumElements,
                                 llvm::Value *This);

  llvm::Constant *GenerateCXXAggrDestructorHelper(const CXXDestructorDecl *D,
                                                const ArrayType *Array,
                                                llvm::Value *This);

  void EmitCXXDestructorCall(const CXXDestructorDecl *D, CXXDtorType Type,
                             bool ForVirtualBase, llvm::Value *This);

  void PushCXXTemporary(const CXXTemporary *Temporary, llvm::Value *Ptr);
  void PopCXXTemporary();

  llvm::Value *EmitCXXNewExpr(const CXXNewExpr *E);
  void EmitCXXDeleteExpr(const CXXDeleteExpr *E);

  void EmitDeleteCall(const FunctionDecl *DeleteFD, llvm::Value *Ptr,
                      QualType DeleteTy);

  llvm::Value* EmitCXXTypeidExpr(const CXXTypeidExpr *E);
  llvm::Value *EmitDynamicCast(llvm::Value *V, const CXXDynamicCastExpr *DCE);

  void EmitCheck(llvm::Value *, unsigned Size);

  llvm::Value *EmitScalarPrePostIncDec(const UnaryOperator *E, LValue LV,
                                       bool isInc, bool isPre);
  ComplexPairTy EmitComplexPrePostIncDec(const UnaryOperator *E, LValue LV,
                                         bool isInc, bool isPre);
  //===--------------------------------------------------------------------===//
  //                            Declaration Emission
  //===--------------------------------------------------------------------===//

  /// EmitDecl - Emit a declaration.
  ///
  /// This function can be called with a null (unreachable) insert point.
  void EmitDecl(const Decl &D);

  /// EmitBlockVarDecl - Emit a block variable declaration.
  ///
  /// This function can be called with a null (unreachable) insert point.
  void EmitBlockVarDecl(const VarDecl &D);

  /// EmitLocalBlockVarDecl - Emit a local block variable declaration.
  ///
  /// This function can be called with a null (unreachable) insert point.
  void EmitLocalBlockVarDecl(const VarDecl &D);

  void EmitStaticBlockVarDecl(const VarDecl &D,
                              llvm::GlobalValue::LinkageTypes Linkage);

  /// EmitParmDecl - Emit a ParmVarDecl or an ImplicitParamDecl.
  void EmitParmDecl(const VarDecl &D, llvm::Value *Arg);

  //===--------------------------------------------------------------------===//
  //                             Statement Emission
  //===--------------------------------------------------------------------===//

  /// EmitStopPoint - Emit a debug stoppoint if we are emitting debug info.
  void EmitStopPoint(const Stmt *S);

  /// EmitStmt - Emit the code for the statement \arg S. It is legal to call
  /// this function even if there is no current insertion point.
  ///
  /// This function may clear the current insertion point; callers should use
  /// EnsureInsertPoint if they wish to subsequently generate code without first
  /// calling EmitBlock, EmitBranch, or EmitStmt.
  void EmitStmt(const Stmt *S);

  /// EmitSimpleStmt - Try to emit a "simple" statement which does not
  /// necessarily require an insertion point or debug information; typically
  /// because the statement amounts to a jump or a container of other
  /// statements.
  ///
  /// \return True if the statement was handled.
  bool EmitSimpleStmt(const Stmt *S);

  RValue EmitCompoundStmt(const CompoundStmt &S, bool GetLast = false,
                          llvm::Value *AggLoc = 0, bool isAggVol = false);

  /// EmitLabel - Emit the block for the given label. It is legal to call this
  /// function even if there is no current insertion point.
  void EmitLabel(const LabelStmt &S); // helper for EmitLabelStmt.

  void EmitLabelStmt(const LabelStmt &S);
  void EmitGotoStmt(const GotoStmt &S);
  void EmitIndirectGotoStmt(const IndirectGotoStmt &S);
  void EmitIfStmt(const IfStmt &S);
  void EmitWhileStmt(const WhileStmt &S);
  void EmitDoStmt(const DoStmt &S);
  void EmitForStmt(const ForStmt &S);
  void EmitReturnStmt(const ReturnStmt &S);
  void EmitDeclStmt(const DeclStmt &S);
  void EmitBreakStmt(const BreakStmt &S);
  void EmitContinueStmt(const ContinueStmt &S);
  void EmitSwitchStmt(const SwitchStmt &S);
  void EmitDefaultStmt(const DefaultStmt &S);
  void EmitCaseStmt(const CaseStmt &S);
  void EmitCaseStmtRange(const CaseStmt &S);
  void EmitAsmStmt(const AsmStmt &S);

  void EmitObjCForCollectionStmt(const ObjCForCollectionStmt &S);
  void EmitObjCAtTryStmt(const ObjCAtTryStmt &S);
  void EmitObjCAtThrowStmt(const ObjCAtThrowStmt &S);
  void EmitObjCAtSynchronizedStmt(const ObjCAtSynchronizedStmt &S);

  llvm::Constant *getUnwindResumeOrRethrowFn();
  struct CXXTryStmtInfo {
    llvm::BasicBlock *SavedLandingPad;
    llvm::BasicBlock *HandlerBlock;
    llvm::BasicBlock *FinallyBlock;
  };
  CXXTryStmtInfo EnterCXXTryStmt(const CXXTryStmt &S);
  void ExitCXXTryStmt(const CXXTryStmt &S, CXXTryStmtInfo Info);

  void EmitCXXTryStmt(const CXXTryStmt &S);
  
  //===--------------------------------------------------------------------===//
  //                         LValue Expression Emission
  //===--------------------------------------------------------------------===//

  /// GetUndefRValue - Get an appropriate 'undef' rvalue for the given type.
  RValue GetUndefRValue(QualType Ty);

  /// EmitUnsupportedRValue - Emit a dummy r-value using the type of E
  /// and issue an ErrorUnsupported style diagnostic (using the
  /// provided Name).
  RValue EmitUnsupportedRValue(const Expr *E,
                               const char *Name);

  /// EmitUnsupportedLValue - Emit a dummy l-value using the type of E and issue
  /// an ErrorUnsupported style diagnostic (using the provided Name).
  LValue EmitUnsupportedLValue(const Expr *E,
                               const char *Name);

  /// EmitLValue - Emit code to compute a designator that specifies the location
  /// of the expression.
  ///
  /// This can return one of two things: a simple address or a bitfield
  /// reference.  In either case, the LLVM Value* in the LValue structure is
  /// guaranteed to be an LLVM pointer type.
  ///
  /// If this returns a bitfield reference, nothing about the pointee type of
  /// the LLVM value is known: For example, it may not be a pointer to an
  /// integer.
  ///
  /// If this returns a normal address, and if the lvalue's C type is fixed
  /// size, this method guarantees that the returned pointer type will point to
  /// an LLVM type of the same size of the lvalue's type.  If the lvalue has a
  /// variable length type, this is not possible.
  ///
  LValue EmitLValue(const Expr *E);

  /// EmitCheckedLValue - Same as EmitLValue but additionally we generate
  /// checking code to guard against undefined behavior.  This is only
  /// suitable when we know that the address will be used to access the
  /// object.
  LValue EmitCheckedLValue(const Expr *E);

  /// EmitLoadOfScalar - Load a scalar value from an address, taking
  /// care to appropriately convert from the memory representation to
  /// the LLVM value representation.
  llvm::Value *EmitLoadOfScalar(llvm::Value *Addr, bool Volatile,
                                QualType Ty);

  /// EmitStoreOfScalar - Store a scalar value to an address, taking
  /// care to appropriately convert from the memory representation to
  /// the LLVM value representation.
  void EmitStoreOfScalar(llvm::Value *Value, llvm::Value *Addr,
                         bool Volatile, QualType Ty);

  /// EmitLoadOfLValue - Given an expression that represents a value lvalue,
  /// this method emits the address of the lvalue, then loads the result as an
  /// rvalue, returning the rvalue.
  RValue EmitLoadOfLValue(LValue V, QualType LVType);
  RValue EmitLoadOfExtVectorElementLValue(LValue V, QualType LVType);
  RValue EmitLoadOfBitfieldLValue(LValue LV, QualType ExprType);
  RValue EmitLoadOfPropertyRefLValue(LValue LV, QualType ExprType);
  RValue EmitLoadOfKVCRefLValue(LValue LV, QualType ExprType);


  /// EmitStoreThroughLValue - Store the specified rvalue into the specified
  /// lvalue, where both are guaranteed to the have the same type, and that type
  /// is 'Ty'.
  void EmitStoreThroughLValue(RValue Src, LValue Dst, QualType Ty);
  void EmitStoreThroughExtVectorComponentLValue(RValue Src, LValue Dst,
                                                QualType Ty);
  void EmitStoreThroughPropertyRefLValue(RValue Src, LValue Dst, QualType Ty);
  void EmitStoreThroughKVCRefLValue(RValue Src, LValue Dst, QualType Ty);

  /// EmitStoreThroughLValue - Store Src into Dst with same constraints as
  /// EmitStoreThroughLValue.
  ///
  /// \param Result [out] - If non-null, this will be set to a Value* for the
  /// bit-field contents after the store, appropriate for use as the result of
  /// an assignment to the bit-field.
  void EmitStoreThroughBitfieldLValue(RValue Src, LValue Dst, QualType Ty,
                                      llvm::Value **Result=0);

  // Note: only availabe for agg return types
  LValue EmitBinaryOperatorLValue(const BinaryOperator *E);
  LValue EmitCompoundAssignOperatorLValue(const CompoundAssignOperator *E);
  // Note: only available for agg return types
  LValue EmitCallExprLValue(const CallExpr *E);
  // Note: only available for agg return types
  LValue EmitVAArgExprLValue(const VAArgExpr *E);
  LValue EmitDeclRefLValue(const DeclRefExpr *E);
  LValue EmitStringLiteralLValue(const StringLiteral *E);
  LValue EmitObjCEncodeExprLValue(const ObjCEncodeExpr *E);
  LValue EmitPredefinedFunctionName(unsigned Type);
  LValue EmitPredefinedLValue(const PredefinedExpr *E);
  LValue EmitUnaryOpLValue(const UnaryOperator *E);
  LValue EmitArraySubscriptExpr(const ArraySubscriptExpr *E);
  LValue EmitExtVectorElementExpr(const ExtVectorElementExpr *E);
  LValue EmitMemberExpr(const MemberExpr *E);
  LValue EmitObjCIsaExpr(const ObjCIsaExpr *E);
  LValue EmitCompoundLiteralLValue(const CompoundLiteralExpr *E);
  LValue EmitConditionalOperatorLValue(const ConditionalOperator *E);
  LValue EmitCastLValue(const CastExpr *E);
  LValue EmitNullInitializationLValue(const CXXZeroInitValueExpr *E);
  
  llvm::Value *EmitIvarOffset(const ObjCInterfaceDecl *Interface,
                              const ObjCIvarDecl *Ivar);
  LValue EmitLValueForAnonRecordField(llvm::Value* Base,
                                      const FieldDecl* Field,
                                      unsigned CVRQualifiers);
  LValue EmitLValueForField(llvm::Value* Base, const FieldDecl* Field,
                            unsigned CVRQualifiers);
  
  /// EmitLValueForFieldInitialization - Like EmitLValueForField, except that
  /// if the Field is a reference, this will return the address of the reference
  /// and not the address of the value stored in the reference.
  LValue EmitLValueForFieldInitialization(llvm::Value* Base, 
                                          const FieldDecl* Field,
                                          unsigned CVRQualifiers);
  
  LValue EmitLValueForIvar(QualType ObjectTy,
                           llvm::Value* Base, const ObjCIvarDecl *Ivar,
                           unsigned CVRQualifiers);

  LValue EmitLValueForBitfield(llvm::Value* Base, const FieldDecl* Field,
                                unsigned CVRQualifiers);

  LValue EmitBlockDeclRefLValue(const BlockDeclRefExpr *E);

  LValue EmitCXXConstructLValue(const CXXConstructExpr *E);
  LValue EmitCXXBindTemporaryLValue(const CXXBindTemporaryExpr *E);
  LValue EmitCXXExprWithTemporariesLValue(const CXXExprWithTemporaries *E);
  LValue EmitCXXTypeidLValue(const CXXTypeidExpr *E);
  
  LValue EmitObjCMessageExprLValue(const ObjCMessageExpr *E);
  LValue EmitObjCIvarRefLValue(const ObjCIvarRefExpr *E);
  LValue EmitObjCPropertyRefLValue(const ObjCPropertyRefExpr *E);
  LValue EmitObjCKVCRefLValue(const ObjCImplicitSetterGetterRefExpr *E);
  LValue EmitObjCSuperExprLValue(const ObjCSuperExpr *E);
  LValue EmitStmtExprLValue(const StmtExpr *E);
  LValue EmitPointerToDataMemberBinaryExpr(const BinaryOperator *E);
  
  //===--------------------------------------------------------------------===//
  //                         Scalar Expression Emission
  //===--------------------------------------------------------------------===//

  /// EmitCall - Generate a call of the given function, expecting the given
  /// result type, and using the given argument list which specifies both the
  /// LLVM arguments and the types they were derived from.
  ///
  /// \param TargetDecl - If given, the decl of the function in a direct call;
  /// used to set attributes on the call (noreturn, etc.).
  RValue EmitCall(const CGFunctionInfo &FnInfo,
                  llvm::Value *Callee,
                  ReturnValueSlot ReturnValue,
                  const CallArgList &Args,
                  const Decl *TargetDecl = 0,
                  llvm::Instruction **callOrInvoke = 0);

  RValue EmitCall(QualType FnType, llvm::Value *Callee,
                  ReturnValueSlot ReturnValue,
                  CallExpr::const_arg_iterator ArgBeg,
                  CallExpr::const_arg_iterator ArgEnd,
                  const Decl *TargetDecl = 0);
  RValue EmitCallExpr(const CallExpr *E, 
                      ReturnValueSlot ReturnValue = ReturnValueSlot());

  llvm::Value *BuildVirtualCall(const CXXMethodDecl *MD, llvm::Value *This,
                                const llvm::Type *Ty);
  llvm::Value *BuildVirtualCall(const CXXDestructorDecl *DD, CXXDtorType Type, 
                                llvm::Value *&This, const llvm::Type *Ty);

  RValue EmitCXXMemberCall(const CXXMethodDecl *MD,
                           llvm::Value *Callee,
                           ReturnValueSlot ReturnValue,
                           llvm::Value *This,
                           llvm::Value *VTT,
                           CallExpr::const_arg_iterator ArgBeg,
                           CallExpr::const_arg_iterator ArgEnd);
  RValue EmitCXXMemberCallExpr(const CXXMemberCallExpr *E,
                               ReturnValueSlot ReturnValue);
  RValue EmitCXXMemberPointerCallExpr(const CXXMemberCallExpr *E,
                                      ReturnValueSlot ReturnValue);

  RValue EmitCXXOperatorMemberCallExpr(const CXXOperatorCallExpr *E,
                                       const CXXMethodDecl *MD,
                                       ReturnValueSlot ReturnValue);

  
  RValue EmitBuiltinExpr(const FunctionDecl *FD,
                         unsigned BuiltinID, const CallExpr *E);

  RValue EmitBlockCallExpr(const CallExpr *E, ReturnValueSlot ReturnValue);

  /// EmitTargetBuiltinExpr - Emit the given builtin call. Returns 0 if the call
  /// is unhandled by the current target.
  llvm::Value *EmitTargetBuiltinExpr(unsigned BuiltinID, const CallExpr *E);

  llvm::Value *EmitARMBuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitX86BuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitPPCBuiltinExpr(unsigned BuiltinID, const CallExpr *E);

  llvm::Value *EmitObjCProtocolExpr(const ObjCProtocolExpr *E);
  llvm::Value *EmitObjCStringLiteral(const ObjCStringLiteral *E);
  llvm::Value *EmitObjCSelectorExpr(const ObjCSelectorExpr *E);
  RValue EmitObjCMessageExpr(const ObjCMessageExpr *E,
                             ReturnValueSlot Return = ReturnValueSlot());
  RValue EmitObjCPropertyGet(const Expr *E,
                             ReturnValueSlot Return = ReturnValueSlot());
  RValue EmitObjCSuperPropertyGet(const Expr *Exp, const Selector &S,
                                  ReturnValueSlot Return = ReturnValueSlot());
  void EmitObjCPropertySet(const Expr *E, RValue Src);
  void EmitObjCSuperPropertySet(const Expr *E, const Selector &S, RValue Src);


  /// EmitReferenceBindingToExpr - Emits a reference binding to the passed in
  /// expression. Will emit a temporary variable if E is not an LValue.
  RValue EmitReferenceBindingToExpr(const Expr* E, bool IsInitializer = false);

  //===--------------------------------------------------------------------===//
  //                           Expression Emission
  //===--------------------------------------------------------------------===//

  // Expressions are broken into three classes: scalar, complex, aggregate.

  /// EmitScalarExpr - Emit the computation of the specified expression of LLVM
  /// scalar type, returning the result.
  llvm::Value *EmitScalarExpr(const Expr *E , bool IgnoreResultAssign = false);

  /// EmitScalarConversion - Emit a conversion from the specified type to the
  /// specified destination type, both of which are LLVM scalar types.
  llvm::Value *EmitScalarConversion(llvm::Value *Src, QualType SrcTy,
                                    QualType DstTy);

  /// EmitComplexToScalarConversion - Emit a conversion from the specified
  /// complex type to the specified destination type, where the destination type
  /// is an LLVM scalar type.
  llvm::Value *EmitComplexToScalarConversion(ComplexPairTy Src, QualType SrcTy,
                                             QualType DstTy);


  /// EmitAggExpr - Emit the computation of the specified expression of
  /// aggregate type.  The result is computed into DestPtr.  Note that if
  /// DestPtr is null, the value of the aggregate expression is not needed.
  void EmitAggExpr(const Expr *E, llvm::Value *DestPtr, bool VolatileDest,
                   bool IgnoreResult = false, bool IsInitializer = false,
                   bool RequiresGCollection = false);

  /// EmitAggExprToLValue - Emit the computation of the specified expression of
  /// aggregate type into a temporary LValue.
  LValue EmitAggExprToLValue(const Expr *E);

  /// EmitGCMemmoveCollectable - Emit special API for structs with object
  /// pointers.
  void EmitGCMemmoveCollectable(llvm::Value *DestPtr, llvm::Value *SrcPtr,
                                QualType Ty);

  /// EmitComplexExpr - Emit the computation of the specified expression of
  /// complex type, returning the result.
  ComplexPairTy EmitComplexExpr(const Expr *E, bool IgnoreReal = false,
                                bool IgnoreImag = false,
                                bool IgnoreRealAssign = false,
                                bool IgnoreImagAssign = false);

  /// EmitComplexExprIntoAddr - Emit the computation of the specified expression
  /// of complex type, storing into the specified Value*.
  void EmitComplexExprIntoAddr(const Expr *E, llvm::Value *DestAddr,
                               bool DestIsVolatile);

  /// StoreComplexToAddr - Store a complex number into the specified address.
  void StoreComplexToAddr(ComplexPairTy V, llvm::Value *DestAddr,
                          bool DestIsVolatile);
  /// LoadComplexFromAddr - Load a complex number from the specified address.
  ComplexPairTy LoadComplexFromAddr(llvm::Value *SrcAddr, bool SrcIsVolatile);

  /// CreateStaticBlockVarDecl - Create a zero-initialized LLVM global for a
  /// static block var decl.
  llvm::GlobalVariable *CreateStaticBlockVarDecl(const VarDecl &D,
                                                 const char *Separator,
                                       llvm::GlobalValue::LinkageTypes Linkage);
  
  /// AddInitializerToGlobalBlockVarDecl - Add the initializer for 'D' to the
  /// global variable that has already been created for it.  If the initializer
  /// has a different type than GV does, this may free GV and return a different
  /// one.  Otherwise it just returns GV.
  llvm::GlobalVariable *
  AddInitializerToGlobalBlockVarDecl(const VarDecl &D,
                                     llvm::GlobalVariable *GV);
  

  /// EmitStaticCXXBlockVarDeclInit - Create the initializer for a C++ runtime
  /// initialized static block var decl.
  void EmitStaticCXXBlockVarDeclInit(const VarDecl &D,
                                     llvm::GlobalVariable *GV);

  /// EmitCXXGlobalVarDeclInit - Create the initializer for a C++
  /// variable with global storage.
  void EmitCXXGlobalVarDeclInit(const VarDecl &D, llvm::Constant *DeclPtr);

  /// EmitCXXGlobalDtorRegistration - Emits a call to register the global ptr
  /// with the C++ runtime so that its destructor will be called at exit.
  void EmitCXXGlobalDtorRegistration(llvm::Constant *DtorFn,
                                     llvm::Constant *DeclPtr);

  /// GenerateCXXGlobalInitFunc - Generates code for initializing global
  /// variables.
  void GenerateCXXGlobalInitFunc(llvm::Function *Fn,
                                 llvm::Constant **Decls,
                                 unsigned NumDecls);

  /// GenerateCXXGlobalDtorFunc - Generates code for destroying global
  /// variables.
  void GenerateCXXGlobalDtorFunc(llvm::Function *Fn,
                                 const std::vector<std::pair<llvm::Constant*,
                                   llvm::Constant*> > &DtorsAndObjects);

  void GenerateCXXGlobalVarDeclInitFunc(llvm::Function *Fn, const VarDecl *D);

  void EmitCXXConstructExpr(llvm::Value *Dest, const CXXConstructExpr *E);

  RValue EmitCXXExprWithTemporaries(const CXXExprWithTemporaries *E,
                                    llvm::Value *AggLoc = 0,
                                    bool IsAggLocVolatile = false,
                                    bool IsInitializer = false);

  void EmitCXXThrowExpr(const CXXThrowExpr *E);

  //===--------------------------------------------------------------------===//
  //                             Internal Helpers
  //===--------------------------------------------------------------------===//

  /// ContainsLabel - Return true if the statement contains a label in it.  If
  /// this statement is not executed normally, it not containing a label means
  /// that we can just remove the code.
  static bool ContainsLabel(const Stmt *S, bool IgnoreCaseStmts = false);

  /// ConstantFoldsToSimpleInteger - If the specified expression does not fold
  /// to a constant, or if it does but contains a label, return 0.  If it
  /// constant folds to 'true' and does not contain a label, return 1, if it
  /// constant folds to 'false' and does not contain a label, return -1.
  int ConstantFoldsToSimpleInteger(const Expr *Cond);

  /// EmitBranchOnBoolExpr - Emit a branch on a boolean condition (e.g. for an
  /// if statement) to the specified blocks.  Based on the condition, this might
  /// try to simplify the codegen of the conditional based on the branch.
  void EmitBranchOnBoolExpr(const Expr *Cond, llvm::BasicBlock *TrueBlock,
                            llvm::BasicBlock *FalseBlock);

  /// getTrapBB - Create a basic block that will call the trap intrinsic.  We'll
  /// generate a branch around the created basic block as necessary.
  llvm::BasicBlock* getTrapBB();
  
  /// EmitCallArg - Emit a single call argument.
  RValue EmitCallArg(const Expr *E, QualType ArgType);

  /// EmitDelegateCallArg - We are performing a delegate call; that
  /// is, the current function is delegating to another one.  Produce
  /// a r-value suitable for passing the given parameter.
  RValue EmitDelegateCallArg(const VarDecl *Param);

private:

  void EmitReturnOfRValue(RValue RV, QualType Ty);

  /// ExpandTypeFromArgs - Reconstruct a structure of type \arg Ty
  /// from function arguments into \arg Dst. See ABIArgInfo::Expand.
  ///
  /// \param AI - The first function argument of the expansion.
  /// \return The argument following the last expanded function
  /// argument.
  llvm::Function::arg_iterator
  ExpandTypeFromArgs(QualType Ty, LValue Dst,
                     llvm::Function::arg_iterator AI);

  /// ExpandTypeToArgs - Expand an RValue \arg Src, with the LLVM type for \arg
  /// Ty, into individual arguments on the provided vector \arg Args. See
  /// ABIArgInfo::Expand.
  void ExpandTypeToArgs(QualType Ty, RValue Src,
                        llvm::SmallVector<llvm::Value*, 16> &Args);

  llvm::Value* EmitAsmInput(const AsmStmt &S,
                            const TargetInfo::ConstraintInfo &Info,
                            const Expr *InputExpr, std::string &ConstraintStr);

  /// EmitCleanupBlock - emits a single cleanup block.
  void EmitCleanupBlock();

  /// AddBranchFixup - adds a branch instruction to the list of fixups for the
  /// current cleanup scope.
  void AddBranchFixup(llvm::BranchInst *BI);

  /// EmitCallArgs - Emit call arguments for a function.
  /// The CallArgTypeInfo parameter is used for iterating over the known
  /// argument types of the function being called.
  template<typename T>
  void EmitCallArgs(CallArgList& Args, const T* CallArgTypeInfo,
                    CallExpr::const_arg_iterator ArgBeg,
                    CallExpr::const_arg_iterator ArgEnd) {
      CallExpr::const_arg_iterator Arg = ArgBeg;

    // First, use the argument types that the type info knows about
    if (CallArgTypeInfo) {
      for (typename T::arg_type_iterator I = CallArgTypeInfo->arg_type_begin(),
           E = CallArgTypeInfo->arg_type_end(); I != E; ++I, ++Arg) {
        assert(Arg != ArgEnd && "Running over edge of argument list!");
        QualType ArgType = *I;

        assert(getContext().getCanonicalType(ArgType.getNonReferenceType()).
               getTypePtr() ==
               getContext().getCanonicalType(Arg->getType()).getTypePtr() &&
               "type mismatch in call argument!");

        Args.push_back(std::make_pair(EmitCallArg(*Arg, ArgType),
                                      ArgType));
      }

      // Either we've emitted all the call args, or we have a call to a
      // variadic function.
      assert((Arg == ArgEnd || CallArgTypeInfo->isVariadic()) &&
             "Extra arguments in non-variadic function!");

    }

    // If we still have any arguments, emit them using the type of the argument.
    for (; Arg != ArgEnd; ++Arg) {
      QualType ArgType = Arg->getType();
      Args.push_back(std::make_pair(EmitCallArg(*Arg, ArgType),
                                    ArgType));
    }
  }

  const TargetCodeGenInfo &getTargetHooks() const {
    return CGM.getTargetCodeGenInfo();
  }
};


}  // end namespace CodeGen
}  // end namespace clang

#endif
