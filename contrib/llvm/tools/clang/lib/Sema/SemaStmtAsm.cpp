//===--- SemaStmtAsm.cpp - Semantic Analysis for Asm Statements -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for inline asm statements.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
using namespace clang;
using namespace sema;

/// CheckAsmLValue - GNU C has an extremely ugly extension whereby they silently
/// ignore "noop" casts in places where an lvalue is required by an inline asm.
/// We emulate this behavior when -fheinous-gnu-extensions is specified, but
/// provide a strong guidance to not use it.
///
/// This method checks to see if the argument is an acceptable l-value and
/// returns false if it is a case we can handle.
static bool CheckAsmLValue(const Expr *E, Sema &S) {
  // Type dependent expressions will be checked during instantiation.
  if (E->isTypeDependent())
    return false;

  if (E->isLValue())
    return false;  // Cool, this is an lvalue.

  // Okay, this is not an lvalue, but perhaps it is the result of a cast that we
  // are supposed to allow.
  const Expr *E2 = E->IgnoreParenNoopCasts(S.Context);
  if (E != E2 && E2->isLValue()) {
    if (!S.getLangOpts().HeinousExtensions)
      S.Diag(E2->getLocStart(), diag::err_invalid_asm_cast_lvalue)
          << E->getSourceRange();
    else
      S.Diag(E2->getLocStart(), diag::warn_invalid_asm_cast_lvalue)
          << E->getSourceRange();
    // Accept, even if we emitted an error diagnostic.
    return false;
  }

  // None of the above, just randomly invalid non-lvalue.
  return true;
}

/// isOperandMentioned - Return true if the specified operand # is mentioned
/// anywhere in the decomposed asm string.
static bool
isOperandMentioned(unsigned OpNo,
                   ArrayRef<GCCAsmStmt::AsmStringPiece> AsmStrPieces) {
  for (unsigned p = 0, e = AsmStrPieces.size(); p != e; ++p) {
    const GCCAsmStmt::AsmStringPiece &Piece = AsmStrPieces[p];
    if (!Piece.isOperand())
      continue;

    // If this is a reference to the input and if the input was the smaller
    // one, then we have to reject this asm.
    if (Piece.getOperandNo() == OpNo)
      return true;
  }
  return false;
}

static bool CheckNakedParmReference(Expr *E, Sema &S) {
  FunctionDecl *Func = dyn_cast<FunctionDecl>(S.CurContext);
  if (!Func)
    return false;
  if (!Func->hasAttr<NakedAttr>())
    return false;

  SmallVector<Expr*, 4> WorkList;
  WorkList.push_back(E);
  while (WorkList.size()) {
    Expr *E = WorkList.pop_back_val();
    if (isa<CXXThisExpr>(E)) {
      S.Diag(E->getLocStart(), diag::err_asm_naked_this_ref);
      S.Diag(Func->getAttr<NakedAttr>()->getLocation(), diag::note_attribute);
      return true;
    }
    if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (isa<ParmVarDecl>(DRE->getDecl())) {
        S.Diag(DRE->getLocStart(), diag::err_asm_naked_parm_ref);
        S.Diag(Func->getAttr<NakedAttr>()->getLocation(), diag::note_attribute);
        return true;
      }
    }
    for (Stmt *Child : E->children()) {
      if (Expr *E = dyn_cast_or_null<Expr>(Child))
        WorkList.push_back(E);
    }
  }
  return false;
}

/// \brief Returns true if given expression is not compatible with inline
/// assembly's memory constraint; false otherwise.
static bool checkExprMemoryConstraintCompat(Sema &S, Expr *E,
                                            TargetInfo::ConstraintInfo &Info,
                                            bool is_input_expr) {
  enum {
    ExprBitfield = 0,
    ExprVectorElt,
    ExprGlobalRegVar,
    ExprSafeType
  } EType = ExprSafeType;

  // Bitfields, vector elements and global register variables are not
  // compatible.
  if (E->refersToBitField())
    EType = ExprBitfield;
  else if (E->refersToVectorElement())
    EType = ExprVectorElt;
  else if (E->refersToGlobalRegisterVar())
    EType = ExprGlobalRegVar;

  if (EType != ExprSafeType) {
    S.Diag(E->getLocStart(), diag::err_asm_non_addr_value_in_memory_constraint)
        << EType << is_input_expr << Info.getConstraintStr()
        << E->getSourceRange();
    return true;
  }

  return false;
}

// Extracting the register name from the Expression value,
// if there is no register name to extract, returns ""
static StringRef extractRegisterName(const Expr *Expression,
                                     const TargetInfo &Target) {
  Expression = Expression->IgnoreImpCasts();
  if (const DeclRefExpr *AsmDeclRef = dyn_cast<DeclRefExpr>(Expression)) {
    // Handle cases where the expression is a variable
    const VarDecl *Variable = dyn_cast<VarDecl>(AsmDeclRef->getDecl());
    if (Variable && Variable->getStorageClass() == SC_Register) {
      if (AsmLabelAttr *Attr = Variable->getAttr<AsmLabelAttr>())
        if (Target.isValidGCCRegisterName(Attr->getLabel()))
          return Target.getNormalizedGCCRegisterName(Attr->getLabel(), true);
    }
  }
  return "";
}

// Checks if there is a conflict between the input and output lists with the
// clobbers list. If there's a conflict, returns the location of the
// conflicted clobber, else returns nullptr
static SourceLocation
getClobberConflictLocation(MultiExprArg Exprs, StringLiteral **Constraints,
                           StringLiteral **Clobbers, int NumClobbers,
                           const TargetInfo &Target, ASTContext &Cont) {
  llvm::StringSet<> InOutVars;
  // Collect all the input and output registers from the extended asm
  // statement in order to check for conflicts with the clobber list
  for (unsigned int i = 0; i < Exprs.size(); ++i) {
    StringRef Constraint = Constraints[i]->getString();
    StringRef InOutReg = Target.getConstraintRegister(
        Constraint, extractRegisterName(Exprs[i], Target));
    if (InOutReg != "")
      InOutVars.insert(InOutReg);
  }
  // Check for each item in the clobber list if it conflicts with the input
  // or output
  for (int i = 0; i < NumClobbers; ++i) {
    StringRef Clobber = Clobbers[i]->getString();
    // We only check registers, therefore we don't check cc and memory
    // clobbers
    if (Clobber == "cc" || Clobber == "memory")
      continue;
    Clobber = Target.getNormalizedGCCRegisterName(Clobber, true);
    // Go over the output's registers we collected
    if (InOutVars.count(Clobber))
      return Clobbers[i]->getLocStart();
  }
  return SourceLocation();
}

StmtResult Sema::ActOnGCCAsmStmt(SourceLocation AsmLoc, bool IsSimple,
                                 bool IsVolatile, unsigned NumOutputs,
                                 unsigned NumInputs, IdentifierInfo **Names,
                                 MultiExprArg constraints, MultiExprArg Exprs,
                                 Expr *asmString, MultiExprArg clobbers,
                                 SourceLocation RParenLoc) {
  unsigned NumClobbers = clobbers.size();
  StringLiteral **Constraints =
    reinterpret_cast<StringLiteral**>(constraints.data());
  StringLiteral *AsmString = cast<StringLiteral>(asmString);
  StringLiteral **Clobbers = reinterpret_cast<StringLiteral**>(clobbers.data());

  SmallVector<TargetInfo::ConstraintInfo, 4> OutputConstraintInfos;

  // The parser verifies that there is a string literal here.
  assert(AsmString->isAscii());

  // If we're compiling CUDA file and function attributes indicate that it's not
  // for this compilation side, skip all the checks.
  if (!DeclAttrsMatchCUDAMode(getLangOpts(), getCurFunctionDecl())) {
    GCCAsmStmt *NS = new (Context) GCCAsmStmt(
        Context, AsmLoc, IsSimple, IsVolatile, NumOutputs, NumInputs, Names,
        Constraints, Exprs.data(), AsmString, NumClobbers, Clobbers, RParenLoc);
    return NS;
  }

  for (unsigned i = 0; i != NumOutputs; i++) {
    StringLiteral *Literal = Constraints[i];
    assert(Literal->isAscii());

    StringRef OutputName;
    if (Names[i])
      OutputName = Names[i]->getName();

    TargetInfo::ConstraintInfo Info(Literal->getString(), OutputName);
    if (!Context.getTargetInfo().validateOutputConstraint(Info))
      return StmtError(Diag(Literal->getLocStart(),
                            diag::err_asm_invalid_output_constraint)
                       << Info.getConstraintStr());

    ExprResult ER = CheckPlaceholderExpr(Exprs[i]);
    if (ER.isInvalid())
      return StmtError();
    Exprs[i] = ER.get();

    // Check that the output exprs are valid lvalues.
    Expr *OutputExpr = Exprs[i];

    // Referring to parameters is not allowed in naked functions.
    if (CheckNakedParmReference(OutputExpr, *this))
      return StmtError();

    // Check that the output expression is compatible with memory constraint.
    if (Info.allowsMemory() &&
        checkExprMemoryConstraintCompat(*this, OutputExpr, Info, false))
      return StmtError();

    OutputConstraintInfos.push_back(Info);

    // If this is dependent, just continue.
    if (OutputExpr->isTypeDependent())
      continue;

    Expr::isModifiableLvalueResult IsLV =
        OutputExpr->isModifiableLvalue(Context, /*Loc=*/nullptr);
    switch (IsLV) {
    case Expr::MLV_Valid:
      // Cool, this is an lvalue.
      break;
    case Expr::MLV_ArrayType:
      // This is OK too.
      break;
    case Expr::MLV_LValueCast: {
      const Expr *LVal = OutputExpr->IgnoreParenNoopCasts(Context);
      if (!getLangOpts().HeinousExtensions) {
        Diag(LVal->getLocStart(), diag::err_invalid_asm_cast_lvalue)
            << OutputExpr->getSourceRange();
      } else {
        Diag(LVal->getLocStart(), diag::warn_invalid_asm_cast_lvalue)
            << OutputExpr->getSourceRange();
      }
      // Accept, even if we emitted an error diagnostic.
      break;
    }
    case Expr::MLV_IncompleteType:
    case Expr::MLV_IncompleteVoidType:
      if (RequireCompleteType(OutputExpr->getLocStart(), Exprs[i]->getType(),
                              diag::err_dereference_incomplete_type))
        return StmtError();
      LLVM_FALLTHROUGH;
    default:
      return StmtError(Diag(OutputExpr->getLocStart(),
                            diag::err_asm_invalid_lvalue_in_output)
                       << OutputExpr->getSourceRange());
    }

    unsigned Size = Context.getTypeSize(OutputExpr->getType());
    if (!Context.getTargetInfo().validateOutputSize(Literal->getString(),
                                                    Size))
      return StmtError(Diag(OutputExpr->getLocStart(),
                            diag::err_asm_invalid_output_size)
                       << Info.getConstraintStr());
  }

  SmallVector<TargetInfo::ConstraintInfo, 4> InputConstraintInfos;

  for (unsigned i = NumOutputs, e = NumOutputs + NumInputs; i != e; i++) {
    StringLiteral *Literal = Constraints[i];
    assert(Literal->isAscii());

    StringRef InputName;
    if (Names[i])
      InputName = Names[i]->getName();

    TargetInfo::ConstraintInfo Info(Literal->getString(), InputName);
    if (!Context.getTargetInfo().validateInputConstraint(OutputConstraintInfos,
                                                         Info)) {
      return StmtError(Diag(Literal->getLocStart(),
                            diag::err_asm_invalid_input_constraint)
                       << Info.getConstraintStr());
    }

    ExprResult ER = CheckPlaceholderExpr(Exprs[i]);
    if (ER.isInvalid())
      return StmtError();
    Exprs[i] = ER.get();

    Expr *InputExpr = Exprs[i];

    // Referring to parameters is not allowed in naked functions.
    if (CheckNakedParmReference(InputExpr, *this))
      return StmtError();

    // Check that the input expression is compatible with memory constraint.
    if (Info.allowsMemory() &&
        checkExprMemoryConstraintCompat(*this, InputExpr, Info, true))
      return StmtError();

    // Only allow void types for memory constraints.
    if (Info.allowsMemory() && !Info.allowsRegister()) {
      if (CheckAsmLValue(InputExpr, *this))
        return StmtError(Diag(InputExpr->getLocStart(),
                              diag::err_asm_invalid_lvalue_in_input)
                         << Info.getConstraintStr()
                         << InputExpr->getSourceRange());
    } else if (Info.requiresImmediateConstant() && !Info.allowsRegister()) {
      if (!InputExpr->isValueDependent()) {
        llvm::APSInt Result;
        if (!InputExpr->EvaluateAsInt(Result, Context))
           return StmtError(
               Diag(InputExpr->getLocStart(), diag::err_asm_immediate_expected)
                << Info.getConstraintStr() << InputExpr->getSourceRange());
         if (!Info.isValidAsmImmediate(Result))
           return StmtError(Diag(InputExpr->getLocStart(),
                                 diag::err_invalid_asm_value_for_constraint)
                            << Result.toString(10) << Info.getConstraintStr()
                            << InputExpr->getSourceRange());
      }

    } else {
      ExprResult Result = DefaultFunctionArrayLvalueConversion(Exprs[i]);
      if (Result.isInvalid())
        return StmtError();

      Exprs[i] = Result.get();
    }

    if (Info.allowsRegister()) {
      if (InputExpr->getType()->isVoidType()) {
        return StmtError(Diag(InputExpr->getLocStart(),
                              diag::err_asm_invalid_type_in_input)
          << InputExpr->getType() << Info.getConstraintStr()
          << InputExpr->getSourceRange());
      }
    }

    InputConstraintInfos.push_back(Info);

    const Type *Ty = Exprs[i]->getType().getTypePtr();
    if (Ty->isDependentType())
      continue;

    if (!Ty->isVoidType() || !Info.allowsMemory())
      if (RequireCompleteType(InputExpr->getLocStart(), Exprs[i]->getType(),
                              diag::err_dereference_incomplete_type))
        return StmtError();

    unsigned Size = Context.getTypeSize(Ty);
    if (!Context.getTargetInfo().validateInputSize(Literal->getString(),
                                                   Size))
      return StmtError(Diag(InputExpr->getLocStart(),
                            diag::err_asm_invalid_input_size)
                       << Info.getConstraintStr());
  }

  // Check that the clobbers are valid.
  for (unsigned i = 0; i != NumClobbers; i++) {
    StringLiteral *Literal = Clobbers[i];
    assert(Literal->isAscii());

    StringRef Clobber = Literal->getString();

    if (!Context.getTargetInfo().isValidClobber(Clobber))
      return StmtError(Diag(Literal->getLocStart(),
                  diag::err_asm_unknown_register_name) << Clobber);
  }

  GCCAsmStmt *NS =
    new (Context) GCCAsmStmt(Context, AsmLoc, IsSimple, IsVolatile, NumOutputs,
                             NumInputs, Names, Constraints, Exprs.data(),
                             AsmString, NumClobbers, Clobbers, RParenLoc);
  // Validate the asm string, ensuring it makes sense given the operands we
  // have.
  SmallVector<GCCAsmStmt::AsmStringPiece, 8> Pieces;
  unsigned DiagOffs;
  if (unsigned DiagID = NS->AnalyzeAsmString(Pieces, Context, DiagOffs)) {
    Diag(getLocationOfStringLiteralByte(AsmString, DiagOffs), DiagID)
           << AsmString->getSourceRange();
    return StmtError();
  }

  // Validate constraints and modifiers.
  for (unsigned i = 0, e = Pieces.size(); i != e; ++i) {
    GCCAsmStmt::AsmStringPiece &Piece = Pieces[i];
    if (!Piece.isOperand()) continue;

    // Look for the correct constraint index.
    unsigned ConstraintIdx = Piece.getOperandNo();
    unsigned NumOperands = NS->getNumOutputs() + NS->getNumInputs();

    // Look for the (ConstraintIdx - NumOperands + 1)th constraint with
    // modifier '+'.
    if (ConstraintIdx >= NumOperands) {
      unsigned I = 0, E = NS->getNumOutputs();

      for (unsigned Cnt = ConstraintIdx - NumOperands; I != E; ++I)
        if (OutputConstraintInfos[I].isReadWrite() && Cnt-- == 0) {
          ConstraintIdx = I;
          break;
        }

      assert(I != E && "Invalid operand number should have been caught in "
                       " AnalyzeAsmString");
    }

    // Now that we have the right indexes go ahead and check.
    StringLiteral *Literal = Constraints[ConstraintIdx];
    const Type *Ty = Exprs[ConstraintIdx]->getType().getTypePtr();
    if (Ty->isDependentType() || Ty->isIncompleteType())
      continue;

    unsigned Size = Context.getTypeSize(Ty);
    std::string SuggestedModifier;
    if (!Context.getTargetInfo().validateConstraintModifier(
            Literal->getString(), Piece.getModifier(), Size,
            SuggestedModifier)) {
      Diag(Exprs[ConstraintIdx]->getLocStart(),
           diag::warn_asm_mismatched_size_modifier);

      if (!SuggestedModifier.empty()) {
        auto B = Diag(Piece.getRange().getBegin(),
                      diag::note_asm_missing_constraint_modifier)
                 << SuggestedModifier;
        SuggestedModifier = "%" + SuggestedModifier + Piece.getString();
        B.AddFixItHint(FixItHint::CreateReplacement(Piece.getRange(),
                                                    SuggestedModifier));
      }
    }
  }

  // Validate tied input operands for type mismatches.
  unsigned NumAlternatives = ~0U;
  for (unsigned i = 0, e = OutputConstraintInfos.size(); i != e; ++i) {
    TargetInfo::ConstraintInfo &Info = OutputConstraintInfos[i];
    StringRef ConstraintStr = Info.getConstraintStr();
    unsigned AltCount = ConstraintStr.count(',') + 1;
    if (NumAlternatives == ~0U)
      NumAlternatives = AltCount;
    else if (NumAlternatives != AltCount)
      return StmtError(Diag(NS->getOutputExpr(i)->getLocStart(),
                            diag::err_asm_unexpected_constraint_alternatives)
                       << NumAlternatives << AltCount);
  }
  SmallVector<size_t, 4> InputMatchedToOutput(OutputConstraintInfos.size(),
                                              ~0U);
  for (unsigned i = 0, e = InputConstraintInfos.size(); i != e; ++i) {
    TargetInfo::ConstraintInfo &Info = InputConstraintInfos[i];
    StringRef ConstraintStr = Info.getConstraintStr();
    unsigned AltCount = ConstraintStr.count(',') + 1;
    if (NumAlternatives == ~0U)
      NumAlternatives = AltCount;
    else if (NumAlternatives != AltCount)
      return StmtError(Diag(NS->getInputExpr(i)->getLocStart(),
                            diag::err_asm_unexpected_constraint_alternatives)
                       << NumAlternatives << AltCount);

    // If this is a tied constraint, verify that the output and input have
    // either exactly the same type, or that they are int/ptr operands with the
    // same size (int/long, int*/long, are ok etc).
    if (!Info.hasTiedOperand()) continue;

    unsigned TiedTo = Info.getTiedOperand();
    unsigned InputOpNo = i+NumOutputs;
    Expr *OutputExpr = Exprs[TiedTo];
    Expr *InputExpr = Exprs[InputOpNo];

    // Make sure no more than one input constraint matches each output.
    assert(TiedTo < InputMatchedToOutput.size() && "TiedTo value out of range");
    if (InputMatchedToOutput[TiedTo] != ~0U) {
      Diag(NS->getInputExpr(i)->getLocStart(),
           diag::err_asm_input_duplicate_match)
          << TiedTo;
      Diag(NS->getInputExpr(InputMatchedToOutput[TiedTo])->getLocStart(),
           diag::note_asm_input_duplicate_first)
          << TiedTo;
      return StmtError();
    }
    InputMatchedToOutput[TiedTo] = i;

    if (OutputExpr->isTypeDependent() || InputExpr->isTypeDependent())
      continue;

    QualType InTy = InputExpr->getType();
    QualType OutTy = OutputExpr->getType();
    if (Context.hasSameType(InTy, OutTy))
      continue;  // All types can be tied to themselves.

    // Decide if the input and output are in the same domain (integer/ptr or
    // floating point.
    enum AsmDomain {
      AD_Int, AD_FP, AD_Other
    } InputDomain, OutputDomain;

    if (InTy->isIntegerType() || InTy->isPointerType())
      InputDomain = AD_Int;
    else if (InTy->isRealFloatingType())
      InputDomain = AD_FP;
    else
      InputDomain = AD_Other;

    if (OutTy->isIntegerType() || OutTy->isPointerType())
      OutputDomain = AD_Int;
    else if (OutTy->isRealFloatingType())
      OutputDomain = AD_FP;
    else
      OutputDomain = AD_Other;

    // They are ok if they are the same size and in the same domain.  This
    // allows tying things like:
    //   void* to int*
    //   void* to int            if they are the same size.
    //   double to long double   if they are the same size.
    //
    uint64_t OutSize = Context.getTypeSize(OutTy);
    uint64_t InSize = Context.getTypeSize(InTy);
    if (OutSize == InSize && InputDomain == OutputDomain &&
        InputDomain != AD_Other)
      continue;

    // If the smaller input/output operand is not mentioned in the asm string,
    // then we can promote the smaller one to a larger input and the asm string
    // won't notice.
    bool SmallerValueMentioned = false;

    // If this is a reference to the input and if the input was the smaller
    // one, then we have to reject this asm.
    if (isOperandMentioned(InputOpNo, Pieces)) {
      // This is a use in the asm string of the smaller operand.  Since we
      // codegen this by promoting to a wider value, the asm will get printed
      // "wrong".
      SmallerValueMentioned |= InSize < OutSize;
    }
    if (isOperandMentioned(TiedTo, Pieces)) {
      // If this is a reference to the output, and if the output is the larger
      // value, then it's ok because we'll promote the input to the larger type.
      SmallerValueMentioned |= OutSize < InSize;
    }

    // If the smaller value wasn't mentioned in the asm string, and if the
    // output was a register, just extend the shorter one to the size of the
    // larger one.
    if (!SmallerValueMentioned && InputDomain != AD_Other &&
        OutputConstraintInfos[TiedTo].allowsRegister())
      continue;

    // Either both of the operands were mentioned or the smaller one was
    // mentioned.  One more special case that we'll allow: if the tied input is
    // integer, unmentioned, and is a constant, then we'll allow truncating it
    // down to the size of the destination.
    if (InputDomain == AD_Int && OutputDomain == AD_Int &&
        !isOperandMentioned(InputOpNo, Pieces) &&
        InputExpr->isEvaluatable(Context)) {
      CastKind castKind =
        (OutTy->isBooleanType() ? CK_IntegralToBoolean : CK_IntegralCast);
      InputExpr = ImpCastExprToType(InputExpr, OutTy, castKind).get();
      Exprs[InputOpNo] = InputExpr;
      NS->setInputExpr(i, InputExpr);
      continue;
    }

    Diag(InputExpr->getLocStart(),
         diag::err_asm_tying_incompatible_types)
      << InTy << OutTy << OutputExpr->getSourceRange()
      << InputExpr->getSourceRange();
    return StmtError();
  }

  // Check for conflicts between clobber list and input or output lists
  SourceLocation ConstraintLoc =
      getClobberConflictLocation(Exprs, Constraints, Clobbers, NumClobbers,
                                 Context.getTargetInfo(), Context);
  if (ConstraintLoc.isValid())
    return Diag(ConstraintLoc, diag::error_inoutput_conflict_with_clobber);
  
  return NS;
}

void Sema::FillInlineAsmIdentifierInfo(Expr *Res,
                                       llvm::InlineAsmIdentifierInfo &Info) {
  QualType T = Res->getType();
  Expr::EvalResult Eval;
  if (T->isFunctionType() || T->isDependentType())
    return Info.setLabel(Res);
  if (Res->isRValue()) {
    if (isa<clang::EnumType>(T) && Res->EvaluateAsRValue(Eval, Context))
      return Info.setEnum(Eval.Val.getInt().getSExtValue());
    return Info.setLabel(Res);
  }
  unsigned Size = Context.getTypeSizeInChars(T).getQuantity();
  unsigned Type = Size;
  if (const auto *ATy = Context.getAsArrayType(T))
    Type = Context.getTypeSizeInChars(ATy->getElementType()).getQuantity();
  bool IsGlobalLV = false;
  if (Res->EvaluateAsLValue(Eval, Context))
    IsGlobalLV = Eval.isGlobalLValue();
  Info.setVar(Res, IsGlobalLV, Size, Type);
}

ExprResult Sema::LookupInlineAsmIdentifier(CXXScopeSpec &SS,
                                           SourceLocation TemplateKWLoc,
                                           UnqualifiedId &Id,
                                           bool IsUnevaluatedContext) {

  if (IsUnevaluatedContext)
    PushExpressionEvaluationContext(
        ExpressionEvaluationContext::UnevaluatedAbstract,
        ReuseLambdaContextDecl);

  ExprResult Result = ActOnIdExpression(getCurScope(), SS, TemplateKWLoc, Id,
                                        /*trailing lparen*/ false,
                                        /*is & operand*/ false,
                                        /*CorrectionCandidateCallback=*/nullptr,
                                        /*IsInlineAsmIdentifier=*/ true);

  if (IsUnevaluatedContext)
    PopExpressionEvaluationContext();

  if (!Result.isUsable()) return Result;

  Result = CheckPlaceholderExpr(Result.get());
  if (!Result.isUsable()) return Result;

  // Referring to parameters is not allowed in naked functions.
  if (CheckNakedParmReference(Result.get(), *this))
    return ExprError();

  QualType T = Result.get()->getType();

  if (T->isDependentType()) {
    return Result;
  }

  // Any sort of function type is fine.
  if (T->isFunctionType()) {
    return Result;
  }

  // Otherwise, it needs to be a complete type.
  if (RequireCompleteExprType(Result.get(), diag::err_asm_incomplete_type)) {
    return ExprError();
  }

  return Result;
}

bool Sema::LookupInlineAsmField(StringRef Base, StringRef Member,
                                unsigned &Offset, SourceLocation AsmLoc) {
  Offset = 0;
  SmallVector<StringRef, 2> Members;
  Member.split(Members, ".");

  NamedDecl *FoundDecl = nullptr;

  // MS InlineAsm uses 'this' as a base
  if (getLangOpts().CPlusPlus && Base.equals("this")) {
    if (const Type *PT = getCurrentThisType().getTypePtrOrNull())
      FoundDecl = PT->getPointeeType()->getAsTagDecl();
  } else {
    LookupResult BaseResult(*this, &Context.Idents.get(Base), SourceLocation(),
                            LookupOrdinaryName);
    if (LookupName(BaseResult, getCurScope()) && BaseResult.isSingleResult())
      FoundDecl = BaseResult.getFoundDecl();
  }

  if (!FoundDecl)
    return true;

  for (StringRef NextMember : Members) {
    const RecordType *RT = nullptr;
    if (VarDecl *VD = dyn_cast<VarDecl>(FoundDecl))
      RT = VD->getType()->getAs<RecordType>();
    else if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(FoundDecl)) {
      MarkAnyDeclReferenced(TD->getLocation(), TD, /*OdrUse=*/false);
      // MS InlineAsm often uses struct pointer aliases as a base
      QualType QT = TD->getUnderlyingType();
      if (const auto *PT = QT->getAs<PointerType>())
        QT = PT->getPointeeType();
      RT = QT->getAs<RecordType>();
    } else if (TypeDecl *TD = dyn_cast<TypeDecl>(FoundDecl))
      RT = TD->getTypeForDecl()->getAs<RecordType>();
    else if (FieldDecl *TD = dyn_cast<FieldDecl>(FoundDecl))
      RT = TD->getType()->getAs<RecordType>();
    if (!RT)
      return true;

    if (RequireCompleteType(AsmLoc, QualType(RT, 0),
                            diag::err_asm_incomplete_type))
      return true;

    LookupResult FieldResult(*this, &Context.Idents.get(NextMember),
                             SourceLocation(), LookupMemberName);

    if (!LookupQualifiedName(FieldResult, RT->getDecl()))
      return true;

    if (!FieldResult.isSingleResult())
      return true;
    FoundDecl = FieldResult.getFoundDecl();

    // FIXME: Handle IndirectFieldDecl?
    FieldDecl *FD = dyn_cast<FieldDecl>(FoundDecl);
    if (!FD)
      return true;

    const ASTRecordLayout &RL = Context.getASTRecordLayout(RT->getDecl());
    unsigned i = FD->getFieldIndex();
    CharUnits Result = Context.toCharUnitsFromBits(RL.getFieldOffset(i));
    Offset += (unsigned)Result.getQuantity();
  }

  return false;
}

ExprResult
Sema::LookupInlineAsmVarDeclField(Expr *E, StringRef Member,
                                  SourceLocation AsmLoc) {

  QualType T = E->getType();
  if (T->isDependentType()) {
    DeclarationNameInfo NameInfo;
    NameInfo.setLoc(AsmLoc);
    NameInfo.setName(&Context.Idents.get(Member));
    return CXXDependentScopeMemberExpr::Create(
        Context, E, T, /*IsArrow=*/false, AsmLoc, NestedNameSpecifierLoc(),
        SourceLocation(),
        /*FirstQualifierInScope=*/nullptr, NameInfo, /*TemplateArgs=*/nullptr);
  }

  const RecordType *RT = T->getAs<RecordType>();
  // FIXME: Diagnose this as field access into a scalar type.
  if (!RT)
    return ExprResult();

  LookupResult FieldResult(*this, &Context.Idents.get(Member), AsmLoc,
                           LookupMemberName);

  if (!LookupQualifiedName(FieldResult, RT->getDecl()))
    return ExprResult();

  // Only normal and indirect field results will work.
  ValueDecl *FD = dyn_cast<FieldDecl>(FieldResult.getFoundDecl());
  if (!FD)
    FD = dyn_cast<IndirectFieldDecl>(FieldResult.getFoundDecl());
  if (!FD)
    return ExprResult();

  // Make an Expr to thread through OpDecl.
  ExprResult Result = BuildMemberReferenceExpr(
      E, E->getType(), AsmLoc, /*IsArrow=*/false, CXXScopeSpec(),
      SourceLocation(), nullptr, FieldResult, nullptr, nullptr);

  return Result;
}

StmtResult Sema::ActOnMSAsmStmt(SourceLocation AsmLoc, SourceLocation LBraceLoc,
                                ArrayRef<Token> AsmToks,
                                StringRef AsmString,
                                unsigned NumOutputs, unsigned NumInputs,
                                ArrayRef<StringRef> Constraints,
                                ArrayRef<StringRef> Clobbers,
                                ArrayRef<Expr*> Exprs,
                                SourceLocation EndLoc) {
  bool IsSimple = (NumOutputs != 0 || NumInputs != 0);
  getCurFunction()->setHasBranchProtectedScope();
  MSAsmStmt *NS =
    new (Context) MSAsmStmt(Context, AsmLoc, LBraceLoc, IsSimple,
                            /*IsVolatile*/ true, AsmToks, NumOutputs, NumInputs,
                            Constraints, Exprs, AsmString,
                            Clobbers, EndLoc);
  return NS;
}

LabelDecl *Sema::GetOrCreateMSAsmLabel(StringRef ExternalLabelName,
                                       SourceLocation Location,
                                       bool AlwaysCreate) {
  LabelDecl* Label = LookupOrCreateLabel(PP.getIdentifierInfo(ExternalLabelName),
                                         Location);

  if (Label->isMSAsmLabel()) {
    // If we have previously created this label implicitly, mark it as used.
    Label->markUsed(Context);
  } else {
    // Otherwise, insert it, but only resolve it if we have seen the label itself.
    std::string InternalName;
    llvm::raw_string_ostream OS(InternalName);
    // Create an internal name for the label.  The name should not be a valid
    // mangled name, and should be unique.  We use a dot to make the name an
    // invalid mangled name. We use LLVM's inline asm ${:uid} escape so that a
    // unique label is generated each time this blob is emitted, even after
    // inlining or LTO.
    OS << "__MSASMLABEL_.${:uid}__";
    for (char C : ExternalLabelName) {
      OS << C;
      // We escape '$' in asm strings by replacing it with "$$"
      if (C == '$')
        OS << '$';
    }
    Label->setMSAsmLabel(OS.str());
  }
  if (AlwaysCreate) {
    // The label might have been created implicitly from a previously encountered
    // goto statement.  So, for both newly created and looked up labels, we mark
    // them as resolved.
    Label->setMSAsmLabelResolved();
  }
  // Adjust their location for being able to generate accurate diagnostics.
  Label->setLocation(Location);

  return Label;
}
