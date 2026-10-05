// Copyright (c) 2016 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SOURCE_OPT_DEF_USE_MANAGER_H_
#define SOURCE_OPT_DEF_USE_MANAGER_H_

#include <set>
#include <unordered_map>
#include <vector>

#include "source/opt/instruction.h"
#include "source/opt/module.h"
#include "spirv-tools/libspirv.hpp"

namespace spvtools {
namespace opt {
namespace analysis {

// Definition should never be null. User can be null, however, such an entry
// should be used only for searching (e.g. all users of a particular definition)
// and never stored in a container.
//
// The unique ids are cached so ordering never has to load the instructions; a
// null instruction has id 0, below every real one.
struct UserEntry {
  UserEntry(Instruction* d, Instruction* u)
      : def(d),
        user(u),
        def_uid(d ? d->unique_id() : 0),
        user_uid(u ? u->unique_id() : 0) {}

  Instruction* def;
  Instruction* user;
  uint32_t def_uid;
  uint32_t user_uid;
};

inline bool operator==(const UserEntry& lhs, const UserEntry& rhs) {
  return lhs.def == rhs.def && lhs.user == rhs.user;
}

// Orders UserEntry for use in associative containers (i.e. less than ordering).
//
// The definition of an UserEntry is treated as the major key and the users as
// the minor key so that all the users of a particular definition are
// consecutive in a container.
//
// A null user always compares less than a real user. This is done to provide
// easy values to search for the beginning of the users of a particular
// definition (i.e. using {def, nullptr}).
struct UserEntryLess {
  bool operator()(const UserEntry& lhs, const UserEntry& rhs) const {
    if (lhs.def_uid != rhs.def_uid) return lhs.def_uid < rhs.def_uid;
    return lhs.user_uid < rhs.user_uid;
  }
};

// A class for analyzing and managing defs and uses in an Module.
class DefUseManager {
 public:

  // Constructs a def-use manager from the given |module|. All internal messages
  // will be communicated to the outside via the given message |consumer|. This
  // instance only keeps a reference to the |consumer|, so the |consumer| should
  // outlive this instance.
  DefUseManager(Module* module) { AnalyzeDefUse(module); }

  DefUseManager(const DefUseManager&) = delete;
  DefUseManager(DefUseManager&&) = delete;
  DefUseManager& operator=(const DefUseManager&) = delete;
  DefUseManager& operator=(DefUseManager&&) = delete;

  // Analyzes the defs in the given |inst|.
  void AnalyzeInstDef(Instruction* inst);

  // Analyzes the uses in the given |inst|.
  //
  // All operands of |inst| must be analyzed as defs.
  void AnalyzeInstUse(Instruction* inst);

  // Analyzes the defs and uses in the given |inst|.
  void AnalyzeInstDefUse(Instruction* inst);

  // Returns the def instruction for the given |id|. If there is no instruction
  // defining |id|, returns nullptr.
  Instruction* GetDef(uint32_t id);
  const Instruction* GetDef(uint32_t id) const;

  // Runs the given function |f| on each unique user instruction of |def| (or
  // |id|).
  //
  // If one instruction uses |def| in multiple operands, that instruction will
  // only be visited once.
  //
  // |def| (or |id|) must be registered as a definition.
  void ForEachUser(const Instruction* def,
                   const std::function<void(Instruction*)>& f) const;
  void ForEachUser(uint32_t id,
                   const std::function<void(Instruction*)>& f) const;

  // Runs the given function |f| on each unique user instruction of |def| (or
  // |id|). If |f| returns false, iteration is terminated and this function
  // returns false.
  //
  // If one instruction uses |def| in multiple operands, that instruction will
  // be only be visited once.
  //
  // |def| (or |id|) must be registered as a definition.
  bool WhileEachUser(const Instruction* def,
                     const std::function<bool(Instruction*)>& f) const;
  bool WhileEachUser(uint32_t id,
                     const std::function<bool(Instruction*)>& f) const;

  // Runs the given function |f| on each unique use of |def| (or
  // |id|).
  //
  // If one instruction uses |def| in multiple operands, each operand will be
  // visited separately.
  //
  // |def| (or |id|) must be registered as a definition.
  void ForEachUse(
      const Instruction* def,
      const std::function<void(Instruction*, uint32_t operand_index)>& f) const;
  void ForEachUse(
      uint32_t id,
      const std::function<void(Instruction*, uint32_t operand_index)>& f) const;

  // Runs the given function |f| on each unique use of |def| (or
  // |id|). If |f| returns false, iteration is terminated and this function
  // returns false.
  //
  // If one instruction uses |def| in multiple operands, each operand will be
  // visited separately.
  //
  // |def| (or |id|) must be registered as a definition.
  bool WhileEachUse(
      const Instruction* def,
      const std::function<bool(Instruction*, uint32_t operand_index)>& f) const;
  bool WhileEachUse(
      uint32_t id,
      const std::function<bool(Instruction*, uint32_t operand_index)>& f) const;

  // Returns the number of users of |def| (or |id|).
  uint32_t NumUsers(const Instruction* def) const;
  uint32_t NumUsers(uint32_t id) const;

  // Returns the number of uses of |def| (or |id|).
  uint32_t NumUses(const Instruction* def) const;
  uint32_t NumUses(uint32_t id) const;

  // Returns the annotation instrunctions which are a direct use of the given
  // |id|. This means when the decorations are applied through decoration
  // group(s), this function will just return the OpGroupDecorate
  // instruction(s) which refer to the given id as an operand. The OpDecorate
  // instructions which decorate the decoration group will not be returned.
  std::vector<Instruction*> GetAnnotations(uint32_t id) const;

  // Clear the internal def-use record of the given instruction |inst|. This
  // method will update the use information of the operand ids of |inst|. The
  // record: |inst| uses an |id|, will be removed from the use records of |id|.
  // If |inst| defines an result id, the use record of this result id will also
  // be removed. Does nothing if |inst| was not analyzed before.
  void ClearInst(Instruction* inst);

  // Erases the records that a given instruction uses its operand ids.
  void EraseUseRecordsOfOperandIds(const Instruction* inst);

  friend bool CompareAndPrintDifferences(const DefUseManager&,
                                         const DefUseManager&);

  // If |inst| has not already been analysed, then analyses its definition and
  // uses.
  void UpdateDefUse(Instruction* inst);

 private:
  // Each definition's users, ordered by user. One small set per definition
  // rather than one module-wide set: lookups stay shallow, and the map's nodes
  // never move, so a set iterated while users are added stays valid.
  using UserSet = std::set<UserEntry, UserEntryLess>;
  using IdToUsersMap = std::unordered_map<const Instruction*, UserSet>;
  using InstToUsedIdsMap =
      std::unordered_map<const Instruction*, std::vector<uint32_t>>;

  // The users of |def|, or null if none were ever recorded.
  const UserSet* Users(const Instruction* def) const;

  // Analyzes the defs and uses in the given |module| and populates data
  // structures in this class. Does nothing if |module| is nullptr.
  void AnalyzeDefUse(Module* module);

  // Mapping from ids to their definitions, indexed by id (null if none).
  std::vector<Instruction*> id_to_def_;
  IdToUsersMap id_to_users_;  // Mapping from ids to their users
  // Mapping from instructions to the ids used in the instruction.
  InstToUsedIdsMap inst_to_used_ids_;
};

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_DEF_USE_MANAGER_H_
