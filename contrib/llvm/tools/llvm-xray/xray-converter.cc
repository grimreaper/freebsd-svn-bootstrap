//===- xray-converter.cc - XRay Trace Conversion --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the trace conversion functions.
//
//===----------------------------------------------------------------------===//
#include "xray-converter.h"

#include "trie-node.h"
#include "xray-registry.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"
#include "llvm/XRay/YAMLXRayRecord.h"

using namespace llvm;
using namespace xray;

// llvm-xray convert
// ----------------------------------------------------------------------------
static cl::SubCommand Convert("convert", "Trace Format Conversion");
static cl::opt<std::string> ConvertInput(cl::Positional,
                                         cl::desc("<xray log file>"),
                                         cl::Required, cl::sub(Convert));
enum class ConvertFormats { BINARY, YAML, CHROME_TRACE_EVENT };
static cl::opt<ConvertFormats> ConvertOutputFormat(
    "output-format", cl::desc("output format"),
    cl::values(clEnumValN(ConvertFormats::BINARY, "raw", "output in binary"),
               clEnumValN(ConvertFormats::YAML, "yaml", "output in yaml"),
               clEnumValN(ConvertFormats::CHROME_TRACE_EVENT, "trace_event",
                          "Output in chrome's trace event format. "
                          "May be visualized with the Catapult trace viewer.")),
    cl::sub(Convert));
static cl::alias ConvertOutputFormat2("f", cl::aliasopt(ConvertOutputFormat),
                                      cl::desc("Alias for -output-format"),
                                      cl::sub(Convert));
static cl::opt<std::string>
    ConvertOutput("output", cl::value_desc("output file"), cl::init("-"),
                  cl::desc("output file; use '-' for stdout"),
                  cl::sub(Convert));
static cl::alias ConvertOutput2("o", cl::aliasopt(ConvertOutput),
                                cl::desc("Alias for -output"),
                                cl::sub(Convert));

static cl::opt<bool>
    ConvertSymbolize("symbolize",
                     cl::desc("symbolize function ids from the input log"),
                     cl::init(false), cl::sub(Convert));
static cl::alias ConvertSymbolize2("y", cl::aliasopt(ConvertSymbolize),
                                   cl::desc("Alias for -symbolize"),
                                   cl::sub(Convert));

static cl::opt<std::string>
    ConvertInstrMap("instr_map",
                    cl::desc("binary with the instrumentation map, or "
                             "a separate instrumentation map"),
                    cl::value_desc("binary with xray_instr_map"),
                    cl::sub(Convert), cl::init(""));
static cl::alias ConvertInstrMap2("m", cl::aliasopt(ConvertInstrMap),
                                  cl::desc("Alias for -instr_map"),
                                  cl::sub(Convert));
static cl::opt<bool> ConvertSortInput(
    "sort",
    cl::desc("determines whether to sort input log records by timestamp"),
    cl::sub(Convert), cl::init(true));
static cl::alias ConvertSortInput2("s", cl::aliasopt(ConvertSortInput),
                                   cl::desc("Alias for -sort"),
                                   cl::sub(Convert));

using llvm::yaml::Output;

void TraceConverter::exportAsYAML(const Trace &Records, raw_ostream &OS) {
  YAMLXRayTrace Trace;
  const auto &FH = Records.getFileHeader();
  Trace.Header = {FH.Version, FH.Type, FH.ConstantTSC, FH.NonstopTSC,
                  FH.CycleFrequency};
  Trace.Records.reserve(Records.size());
  for (const auto &R : Records) {
    Trace.Records.push_back({R.RecordType, R.CPU, R.Type, R.FuncId,
                             Symbolize ? FuncIdHelper.SymbolOrNumber(R.FuncId)
                                       : llvm::to_string(R.FuncId),
                             R.TSC, R.TId, R.CallArgs});
  }
  Output Out(OS, nullptr, 0);
  Out << Trace;
}

void TraceConverter::exportAsRAWv1(const Trace &Records, raw_ostream &OS) {
  // First write out the file header, in the correct endian-appropriate format
  // (XRay assumes currently little endian).
  support::endian::Writer<support::endianness::little> Writer(OS);
  const auto &FH = Records.getFileHeader();
  Writer.write(FH.Version);
  Writer.write(FH.Type);
  uint32_t Bitfield{0};
  if (FH.ConstantTSC)
    Bitfield |= 1uL;
  if (FH.NonstopTSC)
    Bitfield |= 1uL << 1;
  Writer.write(Bitfield);
  Writer.write(FH.CycleFrequency);

  // There's 16 bytes of padding at the end of the file header.
  static constexpr uint32_t Padding4B = 0;
  Writer.write(Padding4B);
  Writer.write(Padding4B);
  Writer.write(Padding4B);
  Writer.write(Padding4B);

  // Then write out the rest of the records, still in an endian-appropriate
  // format.
  for (const auto &R : Records) {
    Writer.write(R.RecordType);
    // The on disk naive raw format uses 8 bit CPUs, but the record has 16.
    // There's no choice but truncation.
    Writer.write(static_cast<uint8_t>(R.CPU));
    switch (R.Type) {
    case RecordTypes::ENTER:
    case RecordTypes::ENTER_ARG:
      Writer.write(uint8_t{0});
      break;
    case RecordTypes::EXIT:
      Writer.write(uint8_t{1});
      break;
    case RecordTypes::TAIL_EXIT:
      Writer.write(uint8_t{2});
      break;
    }
    Writer.write(R.FuncId);
    Writer.write(R.TSC);
    Writer.write(R.TId);
    Writer.write(Padding4B);
    Writer.write(Padding4B);
    Writer.write(Padding4B);
  }
}

namespace {

// A structure that allows building a dictionary of stack ids for the Chrome
// trace event format.
struct StackIdData {
  // Each Stack of function calls has a unique ID.
  unsigned id;

  // Bookkeeping so that IDs can be maintained uniquely across threads.
  // Traversal keeps sibling pointers to other threads stacks. This is helpful
  // to determine when a thread encounters a new stack and should assign a new
  // unique ID.
  SmallVector<TrieNode<StackIdData> *, 4> siblings;
};

using StackTrieNode = TrieNode<StackIdData>;

// A helper function to find the sibling nodes for an encountered function in a
// thread of execution. Relies on the invariant that each time a new node is
// traversed in a thread, sibling bidirectional pointers are maintained.
SmallVector<StackTrieNode *, 4>
findSiblings(StackTrieNode *parent, int32_t FnId, uint32_t TId,
             const DenseMap<uint32_t, SmallVector<StackTrieNode *, 4>>
                 &StackRootsByThreadId) {

  SmallVector<StackTrieNode *, 4> Siblings{};

  if (parent == nullptr) {
    for (auto map_iter : StackRootsByThreadId) {
      // Only look for siblings in other threads.
      if (map_iter.first != TId)
        for (auto node_iter : map_iter.second) {
          if (node_iter->FuncId == FnId)
            Siblings.push_back(node_iter);
        }
    }
    return Siblings;
  }

  for (auto *ParentSibling : parent->ExtraData.siblings)
    for (auto node_iter : ParentSibling->Callees)
      if (node_iter->FuncId == FnId)
        Siblings.push_back(node_iter);

  return Siblings;
}

// Given a function being invoked in a thread with id TId, finds and returns the
// StackTrie representing the function call stack. If no node exists, creates
// the node. Assigns unique IDs to stacks newly encountered among all threads
// and keeps sibling links up to when creating new nodes.
StackTrieNode *findOrCreateStackNode(
    StackTrieNode *Parent, int32_t FuncId, uint32_t TId,
    DenseMap<uint32_t, SmallVector<StackTrieNode *, 4>> &StackRootsByThreadId,
    DenseMap<unsigned, StackTrieNode *> &StacksByStackId, unsigned *id_counter,
    std::forward_list<StackTrieNode> &NodeStore) {
  SmallVector<StackTrieNode *, 4> &ParentCallees =
      Parent == nullptr ? StackRootsByThreadId[TId] : Parent->Callees;
  auto match = find_if(ParentCallees, [FuncId](StackTrieNode *ParentCallee) {
    return FuncId == ParentCallee->FuncId;
  });
  if (match != ParentCallees.end())
    return *match;

  SmallVector<StackTrieNode *, 4> siblings =
      findSiblings(Parent, FuncId, TId, StackRootsByThreadId);
  if (siblings.empty()) {
    NodeStore.push_front({FuncId, Parent, {}, {(*id_counter)++, {}}});
    StackTrieNode *CurrentStack = &NodeStore.front();
    StacksByStackId[*id_counter - 1] = CurrentStack;
    ParentCallees.push_back(CurrentStack);
    return CurrentStack;
  }
  unsigned stack_id = siblings[0]->ExtraData.id;
  NodeStore.push_front({FuncId, Parent, {}, {stack_id, std::move(siblings)}});
  StackTrieNode *CurrentStack = &NodeStore.front();
  for (auto *sibling : CurrentStack->ExtraData.siblings)
    sibling->ExtraData.siblings.push_back(CurrentStack);
  ParentCallees.push_back(CurrentStack);
  return CurrentStack;
}

void writeTraceViewerRecord(raw_ostream &OS, int32_t FuncId, uint32_t TId,
                            bool Symbolize,
                            const FuncIdConversionHelper &FuncIdHelper,
                            double EventTimestampUs,
                            const StackTrieNode &StackCursor,
                            StringRef FunctionPhenotype) {
  OS << "    ";
  OS << llvm::formatv(
      R"({ "name" : "{0}", "ph" : "{1}", "tid" : "{2}", "pid" : "1", )"
      R"("ts" : "{3:f3}", "sf" : "{4}" })",
      (Symbolize ? FuncIdHelper.SymbolOrNumber(FuncId)
                 : llvm::to_string(FuncId)),
      FunctionPhenotype, TId, EventTimestampUs, StackCursor.ExtraData.id);
}

} // namespace

void TraceConverter::exportAsChromeTraceEventFormat(const Trace &Records,
                                                    raw_ostream &OS) {
  const auto &FH = Records.getFileHeader();
  auto CycleFreq = FH.CycleFrequency;

  unsigned id_counter = 0;

  OS << "{\n  \"traceEvents\": [";
  DenseMap<uint32_t, StackTrieNode *> StackCursorByThreadId{};
  DenseMap<uint32_t, SmallVector<StackTrieNode *, 4>> StackRootsByThreadId{};
  DenseMap<unsigned, StackTrieNode *> StacksByStackId{};
  std::forward_list<StackTrieNode> NodeStore{};
  int loop_count = 0;
  for (const auto &R : Records) {
    if (loop_count++ == 0)
      OS << "\n";
    else
      OS << ",\n";

    // Chrome trace event format always wants data in micros.
    // CyclesPerMicro = CycleHertz / 10^6
    // TSC / CyclesPerMicro == TSC * 10^6 / CycleHertz == MicroTimestamp
    // Could lose some precision here by converting the TSC to a double to
    // multiply by the period in micros. 52 bit mantissa is a good start though.
    // TODO: Make feature request to Chrome Trace viewer to accept ticks and a
    // frequency or do some more involved calculation to avoid dangers of
    // conversion.
    double EventTimestampUs = double(1000000) / CycleFreq * double(R.TSC);
    StackTrieNode *&StackCursor = StackCursorByThreadId[R.TId];
    switch (R.Type) {
    case RecordTypes::ENTER:
    case RecordTypes::ENTER_ARG:
      StackCursor = findOrCreateStackNode(StackCursor, R.FuncId, R.TId,
                                          StackRootsByThreadId, StacksByStackId,
                                          &id_counter, NodeStore);
      // Each record is represented as a json dictionary with function name,
      // type of B for begin or E for end, thread id, process id (faked),
      // timestamp in microseconds, and a stack frame id. The ids are logged
      // in an id dictionary after the events.
      writeTraceViewerRecord(OS, R.FuncId, R.TId, Symbolize, FuncIdHelper,
                             EventTimestampUs, *StackCursor, "B");
      break;
    case RecordTypes::EXIT:
    case RecordTypes::TAIL_EXIT:
      // No entries to record end for.
      if (StackCursor == nullptr)
        break;
      // Should we emit an END record anyway or account this condition?
      // (And/Or in loop termination below)
      StackTrieNode *PreviousCursor = nullptr;
      do {
        writeTraceViewerRecord(OS, StackCursor->FuncId, R.TId, Symbolize,
                               FuncIdHelper, EventTimestampUs, *StackCursor,
                               "E");
        PreviousCursor = StackCursor;
        StackCursor = StackCursor->Parent;
      } while (PreviousCursor->FuncId != R.FuncId && StackCursor != nullptr);
      break;
    }
  }
  OS << "\n  ],\n"; // Close the Trace Events array.
  OS << "  "
     << "\"displayTimeUnit\": \"ns\",\n";

  // The stackFrames dictionary substantially reduces size of the output file by
  // avoiding repeating the entire call stack of function names for each entry.
  OS << R"(  "stackFrames": {)";
  int stack_frame_count = 0;
  for (auto map_iter : StacksByStackId) {
    if (stack_frame_count++ == 0)
      OS << "\n";
    else
      OS << ",\n";
    OS << "    ";
    OS << llvm::formatv(
        R"("{0}" : { "name" : "{1}")", map_iter.first,
        (Symbolize ? FuncIdHelper.SymbolOrNumber(map_iter.second->FuncId)
                   : llvm::to_string(map_iter.second->FuncId)));
    if (map_iter.second->Parent != nullptr)
      OS << llvm::formatv(R"(, "parent": "{0}")",
                          map_iter.second->Parent->ExtraData.id);
    OS << " }";
  }
  OS << "\n  }\n"; // Close the stack frames map.
  OS << "}\n";     // Close the JSON entry.
}

namespace llvm {
namespace xray {

static CommandRegistration Unused(&Convert, []() -> Error {
  // FIXME: Support conversion to BINARY when upgrading XRay trace versions.
  InstrumentationMap Map;
  if (!ConvertInstrMap.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(ConvertInstrMap);
    if (!InstrumentationMapOrError)
      return joinErrors(make_error<StringError>(
                            Twine("Cannot open instrumentation map '") +
                                ConvertInstrMap + "'",
                            std::make_error_code(std::errc::invalid_argument)),
                        InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  const auto &FunctionAddresses = Map.getFunctionAddresses();
  symbolize::LLVMSymbolizer::Options Opts(
      symbolize::FunctionNameKind::LinkageName, true, true, false, "");
  symbolize::LLVMSymbolizer Symbolizer(Opts);
  llvm::xray::FuncIdConversionHelper FuncIdHelper(ConvertInstrMap, Symbolizer,
                                                  FunctionAddresses);
  llvm::xray::TraceConverter TC(FuncIdHelper, ConvertSymbolize);
  std::error_code EC;
  raw_fd_ostream OS(ConvertOutput, EC,
                    ConvertOutputFormat == ConvertFormats::BINARY
                        ? sys::fs::OpenFlags::F_None
                        : sys::fs::OpenFlags::F_Text);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + ConvertOutput + "' for writing.", EC);

  auto TraceOrErr = loadTraceFile(ConvertInput, ConvertSortInput);
  if (!TraceOrErr)
    return joinErrors(
        make_error<StringError>(
            Twine("Failed loading input file '") + ConvertInput + "'.",
            std::make_error_code(std::errc::executable_format_error)),
        TraceOrErr.takeError());

  auto &T = *TraceOrErr;
  switch (ConvertOutputFormat) {
  case ConvertFormats::YAML:
    TC.exportAsYAML(T, OS);
    break;
  case ConvertFormats::BINARY:
    TC.exportAsRAWv1(T, OS);
    break;
  case ConvertFormats::CHROME_TRACE_EVENT:
    TC.exportAsChromeTraceEventFormat(T, OS);
    break;
  }
  return Error::success();
});

} // namespace xray
} // namespace llvm
