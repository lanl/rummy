//========================================================================================
// (C) (or copyright) 2025-2026. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

// This file was created in part with generative AI

#ifndef RUMMY_FULL_DECK_HPP_
#define RUMMY_FULL_DECK_HPP_

#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "deck_base.hpp"
#include "deck_graph.hpp"
#include "yaml_schema.hpp"
#include <optional>
#include <pips/device/device_chunk.hpp>
#include <pips/device/device_pack.hpp>
#include <pips/device/device_value.hpp>
#include <pips/device/device_vm.hpp>
#include <pips/vm.hpp>

namespace Rummy {

// FullDeck: block-aware front end that lowers a deck source to a single
// pips program (schema classes + per-block lowering) and reads cards back
// by walking the VM instance graph after one vm.interpret() call.
//
// Use this when the input takes advantage of pips features (control flow,
// user-declared classes, `<Class(name)>` instantiation headers, native
// vectors, etc.). For older flat key=value inputs, prefer SimpleDeck.
//
// Highlights vs SimpleDeck:
//  1. Suit headers may carry an optional instance name: `<suit(varname)>`.
//     The VM variable for that suit is `varname` instead of the
//     mangled suit-segment name. Useful for pips-mode blocks where the
//     user writes `setattr(b, ...)` rather than `setattr(block, ...)`.
//  2. After compilation, cards are read back by walking the VM instance
//     graph rather than a statically-built metadata map. Fields created
//     dynamically (e.g. inside a for-loop) are therefore discovered
//     automatically.
//  3. Device function packing: pips functions defined in the deck may be
//     compiled to a self-contained pips::device::DeviceModule suitable for
//     invocation inside a Kokkos (or any other) device kernel.
class FullDeck : public DeckBase {
 public:
  // Field-discovery strictness:
  //  - Loose (default): fields are attached at runtime via `setattr`, so
  //    classes may be empty stubs and instances can grow arbitrary fields.
  //  - Strict: caller supplies a YAML schema (via ctor argument `schema`)
  //    that declares every class with its fields and defaults.  Lowering
  //    uses direct `obj.field = expr` assignment (no setattr), so writing
  //    to an undeclared field is a pips error.  Instantiating a class not
  //    in the schema is a fatal Rummy error.
  enum class Mode { Loose, Strict };

  FullDeck() = default;
  // Construct with an explicit mode and no schema.  Strict mode requires a
  // schema (lowering will fail at Build time if none is supplied).
  explicit FullDeck(Mode mode) : mode_(mode) {}
  // Construct with an explicit mode and a YAML schema describing the deck's
  // classes.  In Strict mode the schema is the single source of truth for
  // declared classes and their fields.
  FullDeck(Mode mode, Schema schema) : mode_(mode), schema_(std::move(schema)) {}
  // Convenience: load the schema from a YAML file path.
  FullDeck(Mode mode, const std::string &schema_path)
      : mode_(mode), schema_(Schema::FromFile(schema_path)) {}
  FullDeck(const FullDeck &other)
      : DeckBase(other), mode_(other.mode_), schema_(other.schema_),
        suit_to_vm_name_(other.suit_to_vm_name_),
        suit_class_name_(other.suit_class_name_),
        suit_canonical_path_(other.suit_canonical_path_) {
    RebuildInstanceRegistry();
  }
  FullDeck &operator=(const FullDeck &other) {
    if (this != &other) {
      DeckBase::operator=(other);
      mode_ = other.mode_;
      schema_ = other.schema_;
      suit_to_vm_name_ = other.suit_to_vm_name_;
      suit_class_name_ = other.suit_class_name_;
      suit_canonical_path_ = other.suit_canonical_path_;
      RebuildInstanceRegistry();
    }
    return *this;
  }
  FullDeck(FullDeck &&) noexcept = default;
  FullDeck &operator=(FullDeck &&) noexcept = default;

  void Build(std::string fname, std::string prepends = "") override;
  void Build(std::istream &ss) override;
  void Build(std::istream &ss, std::string prepends) override;
  void Build(std::istream &ss, std::istream &prepends) override;
  void BuildSources(const std::vector<InputSource> &sources) override;

  // Cards in card_map insertion order (with vector index secondary sort).
  std::vector<Card> FindSuitInOrder(const std::string &suit,
                                    const bool fuzzy = false) const override;

  // -------------------------------------------------------------------
  // Device function packing & calling
  //
  // A deck may define pips functions in its globals block (e.g.
  //   fn f(x) { return 2 * x }
  // ). After Build(), PackDeviceFunction("f") walks the host bytecode
  // and produces a self-contained pips::device::DeviceModuleStorage
  // suitable for upload to an accelerator. The packed module is owned
  // by the FullDeck instance.
  //
  // The returned DeviceFunctionHandle is trivially copyable and may be
  // captured by value into a device kernel (e.g. a KOKKOS_LAMBDA);
  // Rummy itself has no Kokkos dependency. Inside the kernel:
  //   pips::device::DeviceVM dvm;
  //   pips::device::DeviceValue result;
  //   dvm.run(handle.module, handle.entry_id, args, argc, &result);
  // -------------------------------------------------------------------
  struct DeviceFunctionHandle {
    pips::device::DeviceModule module{};
    std::uint32_t entry_id = 0;
  };

  // Pack `name` and every function it transitively calls into a stored
  // device module. Returns true on success; on failure writes a human-
  // readable reason into `error` and leaves no module registered.
  bool PackDeviceFunction(const std::string &name, std::string &error);

  // Convenience that throws std::runtime_error on failure.
  void PackDeviceFunction(const std::string &name);

  // True iff `name` was successfully packed via PackDeviceFunction.
  bool HasDeviceFunction(const std::string &name) const {
    return device_modules_.find(name) != device_modules_.end();
  }

  // Throws std::runtime_error if `name` was not packed.
  DeviceFunctionHandle GetDeviceFunction(const std::string &name) const;

  // Names of every device-packed function, in insertion order.
  std::vector<std::string> AvailableDeviceFunctions() const {
    return device_function_order_;
  }

  // Host-side execution helper: runs the packed function with numeric
  // arguments via pips::device::DeviceVM::run. Useful for tests and for
  // sanity-checking the packed bytecode before launching kernels.
  double CallDeviceFunction(const std::string &name,
                            const std::vector<double> &args) const;

  // -------------------------------------------------------------------
  // Class introspection
  //
  // Each suit in a FullDeck is backed by a pips::Instance of some class
  // (either user-declared `class X { ... }` or a synthetic schema class
  // generated from the suit's literal fields). These helpers expose that
  // mapping so callers can filter suits by their class.
  // -------------------------------------------------------------------

  // Returns the pips class name of the instance backing `suit`, or empty
  // string if `suit` has no live instance (e.g. "/" or suits that hold
  // only globals).
  std::string GetClassName(const std::string &suit) const;

  std::string GetCanonicalPath(const std::string &suit) const;

  void SeedSuitMetadata(const std::string &suit, const std::string &class_name,
                        const std::string &canonical_path);

  // Returns every suit path whose backing instance has class name
  // `className`, sorted lexicographically. Includes nested suits.
  std::vector<std::string> FindSuitsOfClass(const std::string &className) const;

  // -------------------------------------------------------------------
  // Linked-tree representation
  //
  // Build a DeckGraph snapshot from the current VM state.  Each top-level
  // pips instance becomes a child of the synthetic Root node; fields
  // recursively become Variable or Class children.  Node names are the
  // true pips instance / field names, never the class name.
  //
  // The returned graph references live VM data (string heap, vector
  // objects, ClassDef*) and must not outlive this FullDeck.
  // -------------------------------------------------------------------
  DeckGraph BuildGraph() const;

  // Pretty-print the graph (delegates to DeckGraph::Print).
  void PrintGraph(std::ostream &os) const;

  // Save the graph as a self-contained pips program suitable for re-loading.
  // The synthetic Root is omitted; class declarations are written first.
  void SaveGraph(std::ostream &os) const;

  // Re-build this FullDeck from a previously-saved graph stream.  This is a
  // thin wrapper around Build(istream&): the saved file is, by design,
  // legal pips source.
  void LoadGraph(std::istream &is);

 protected:
  void CopyVmState(const pips::VM &other_vm) override {
    CopyVmStateImpl(other_vm);
    RebuildInstanceRegistry();
  }

 private:
  void RebuildInstanceRegistry();
  void BuildInternal(const std::vector<InputSource> &sources);

  // Lowering / validation mode.  See enum Mode for semantics.
  Mode mode_ = Mode::Loose;
  // Optional YAML schema describing classes/fields/defaults.  In Strict
  // mode this is required; in Loose mode it merely augments class-name
  // resolution and field defaults.
  std::optional<Schema> schema_;

  // Per-suit instance registry, populated post-interpret by the readback.
  std::unordered_map<pips::Instance *, std::string> instance_to_suit_;
  // suit_path -> vm variable name (only differs when (instance_name) is used).
  std::map<std::string, std::string> suit_to_vm_name_;
  // suit_path -> pips class name backing this suit. Only populated when the
  // class differs from the suit path (i.e. for `<Class(instance)>` blocks).
  // Used by GetClassName so the answer is available even when no live
  // instance exists yet (e.g. after construction but before Build).
  std::map<std::string, std::string> suit_class_name_;
  std::map<std::string, std::string> suit_canonical_path_;

  // Device function packing: storage owned per packed function so the
  // DeviceModule view returned to callers remains valid for the lifetime
  // of the FullDeck instance.
  std::unordered_map<std::string, pips::device::DeviceModuleStorage> device_modules_;
  std::unordered_map<std::string, std::uint32_t> device_entry_ids_;
  std::vector<std::string> device_function_order_;
};

} // namespace Rummy

#endif // RUMMY_FULL_DECK_HPP_
