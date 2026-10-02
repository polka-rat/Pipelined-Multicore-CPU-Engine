#include "cache/replacement.h"

#include "cache/cache_types.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace pipesim {
namespace cache {
namespace {

[[maybe_unused]] void CheckCoordinates(std::uint32_t set, std::uint32_t way,
                      std::uint32_t sets, std::uint32_t ways) {
  if (set >= sets || way >= ways) {
    throw std::runtime_error("replacement-policy coordinate out of range");
  }
}

// TODO(A2-REPL-01): Tree-PLRU. Implement Reset/BeginCycle/OnHit/OnInsert/
// PeekVictim/SelectVictim/DebugState per SPEC.md/ABI.md.
//
// Worked example (current/next commit discipline only -- the same shape
// as tutorial.cc's ValueRegister::Commit): Commit() below is fully wired.
// Every other method here needs the same current_/planned_ separation, but
// with the actual tree-bit read/update logic, which is not given.
class TreePlru final : public ReplacementPolicy {
 public:
  TreePlru(std::uint32_t sets, std::uint32_t ways)
      : sets_(sets), ways_(ways),
        current_(sets, std::vector<bool>(ways - 1)),
        planned_(sets, std::vector<bool>(ways - 1)) {
    if (!IsPowerOfTwo(ways_)) {
      throw std::runtime_error("Tree-PLRU requires power-of-two ways");
    }
  }

  void Reset() override {
    current_.assign(sets_, std::vector<bool>(ways_ - 1, false));
    planned_.assign(sets_, std::vector<bool>(ways_-1, false));
    begun_ = false;
    event_planned_ = false;
    // throw std::runtime_error("TreePlru::Reset: TODO(A2--REPL-01)");
  }

  void BeginCycle() override {
    if(begun_) throw std::runtime_error("Tree-PLRU BeginCycle called twice without commit");
    if(event_planned_) throw std::runtime_error("Tree-PLRU BeginCycle called after event planned");
    begun_ = true;
    event_planned_ = false;
    planned_= current_;
    // throw std::runtime_error("TreePlru::BeginCycle: TODO(A2--REPL-01)");
  }

  void OnHit(std::uint32_t set, std::uint32_t way) override {
    // (void)set;
    // (void)way;

    CheckCoordinates(set, way, sets_ , ways_);

    if(!begun_) throw std::runtime_error("Tree-PLRU OnHit without begin");
    if(event_planned_)throw std::runtime_error("Tree-PLRU OnHit after event planned");

    std::uint32_t index=0,left_lim=0,right_lim=ways_-1,mid;

    while(right_lim>left_lim){
        mid=(left_lim+right_lim)/2;
        if(way>mid){
          planned_[set][index]=false;
          index=2*index+2;
          left_lim=mid+1;
        }
        else{
          planned_[set][index]=true;
          index=2*index+1;
          right_lim=mid;
        }
    }
    event_planned_=true;
    // throw std::runtime_error("TreePlru::OnHit: TODO(A2--REPL-01)");
  }

  void OnInsert(std::uint32_t set, std::uint32_t way) override {
    // (void)set;
    // (void)way;

    CheckCoordinates(set, way, sets_ , ways_);

    if(!begun_) throw std::runtime_error("Tree-PLRU OnInsert without begin");
    if(event_planned_)throw std::runtime_error("Tree-PLRU OnInsert after event planned");

    std::uint32_t index=0,left_lim=0,right_lim=ways_-1,mid;

    while(right_lim>left_lim){
        mid=(left_lim+right_lim)/2;
        if(way>mid){
          planned_[set][index]=false;
          index=2*index+2;
          left_lim=mid+1;
        }
        else{
          planned_[set][index]=true;
          index=2*index+1;
          right_lim=mid;
        }
    }
    event_planned_=true;
    // throw std::runtime_error("TreePlru::OnInsert: TODO(A2--REPL-01)");
  }

  int SelectVictim(std::uint32_t set,
                   const std::vector<bool>& eligible) override {
    // (void)set;
    // (void)eligible;

    if(!begun_) throw std::runtime_error("Tree-PLRU SelectVictim without begin");
    if(event_planned_)throw std::runtime_error("Tree-PLRU SelectVictim after event planned");
    CheckCoordinates(set, 0, sets_ , ways_);
    if(eligible.size()!=ways_) throw std::runtime_error("Tree-PLRU PeekVictim eligible size mismatch");
    if(std::find(eligible.begin(),eligible.end(),true)==eligible.end()) throw std::runtime_error("Tree has no eligible ways");

    std::uint32_t index=0,left_lim=0,right_lim=ways_-1,mid,dir,left_possible=0,right_possible=ways_-1;

    while(left_lim<right_lim){
      mid=(left_lim+right_lim)/2;
      
      dir=planned_[set][index]?1:0;
      left_possible=(std::find(eligible.begin()+left_lim,eligible.begin()+mid+1,true)!=eligible.begin()+mid+1);
      right_possible=(std::find(eligible.begin()+mid+1,eligible.begin()+right_lim+1,true)!=eligible.begin()+right_lim+1);

      if(dir){
        if(right_possible){
          index=2*index+2;
          left_lim=mid+1;
        }
        else{
          index=2*index+1;
          right_lim=mid;
        }
      }
      else{
        if(left_possible){
          index=2*index+1;
          right_lim=mid;
        }
        else{
          index=2*index+2;
          left_lim=mid+1;
        }
      }
    }

    OnInsert(set,left_lim);
    // event_planned_=true;
    return left_lim;
    // throw std::runtime_error("TreePlru::SelectVictim: TODO(A2--REPL-01)");
  }

  int PeekVictim(std::uint32_t set,
                 const std::vector<bool>& eligible) const override {
    // (void)set;
    // (void)eligible;

    CheckCoordinates(set, 0, sets_ , ways_);
    if(eligible.size()!=ways_) throw std::runtime_error("Tree-PLRU PeekVictim eligible size mismatch");
    if(std::find(eligible.begin(),eligible.end(),true)==eligible.end()) throw std::runtime_error("Tree has no eligible ways");

    std::uint32_t index=0,left_lim=0,right_lim=ways_-1,mid,dir,left_possible=0,right_possible=ways_-1;

    while(left_lim<right_lim){
      mid=(left_lim+right_lim)/2;
      
      dir=current_[set][index]?1:0;
      left_possible=(std::find(eligible.begin()+left_lim,eligible.begin()+mid+1,true)!=eligible.begin()+mid+1);
      right_possible=(std::find(eligible.begin()+mid+1,eligible.begin()+right_lim+1,true)!=eligible.begin()+right_lim+1);

      if(dir){
        if(right_possible){
          index=2*index+2;
          left_lim=mid+1;
        }
        else{
          index=2*index+1;
          right_lim=mid;
        }
      }
      else{
        if(left_possible){
          index=2*index+1;
          right_lim=mid;
        }
        else{
          index=2*index+2;
          left_lim=mid+1;
        }
      }
    }

    return left_lim;
    // throw std::runtime_error("TreePlru::PeekVictim: TODO(A2--REPL-01)");
  }

    std::vector<std::uint32_t> DebugState(std::uint32_t set) const override {
    CheckCoordinates(set, 0, sets_, ways_);
    return std::vector<std::uint32_t>(
        current_[set].begin(), current_[set].end());
  }

  // Worked example: the general current/next commit discipline (compare
  // tutorial.cc's ValueRegister::Commit). Every other method above must
  // maintain the same current_/planned_ separation.
  void Commit() override {
    if (!begun_) throw std::runtime_error("Tree-PLRU Commit without begin");
    current_ = planned_;
    begun_ = false;
    event_planned_ = false;
  }

 private:
  std::uint32_t sets_;
  std::uint32_t ways_;
  std::vector<std::vector<bool>> current_;
  std::vector<std::vector<bool>> planned_;
  bool begun_ = false;
  bool event_planned_ = false;
};

// TODO(A2-REPL-02): SRRIP. Implement Reset/BeginCycle/OnHit/OnInsert/
// PeekVictim/SelectVictim/DebugState per SPEC.md/ABI.md (2-bit RRPV,
// init 3, hit 0, insertion 2, saturating aging, lowest-way tie-break).
class Srrip final : public ReplacementPolicy {
 public:
  Srrip(std::uint32_t sets, std::uint32_t ways)
      : sets_(sets), ways_(ways),
        current_(sets, std::vector<std::uint8_t>(ways, 3)),
        planned_(sets, std::vector<std::uint8_t>(ways, 3)) {}

  void Reset() override {
    current_.assign(sets_,std::vector<std::uint8_t>(ways_,3));
    planned_.assign(sets_,std::vector<std::uint8_t>(ways_,3));

    begun_ = false;
    event_planned_ = false;
    // throw std::runtime_error("Srrip::Reset: TODO(A2--REPL-02)");
  }

  void BeginCycle() override {
    // (void)set;
    if(begun_) throw std::runtime_error("Tree-PLRU BeginCycle called twice without commit");
    if(event_planned_) throw std::runtime_error("Tree-PLRU BeginCycle called after event planned");
    begun_ = true;
    event_planned_ = false;
    planned_= current_;
    
    // throw std::runtime_error("Srrip::BeginCycle: TODO(A2--REPL-02)");
  }

  void OnHit(std::uint32_t set, std::uint32_t way) override {
    // (void)set;
    // (void)way;
    if(!begun_) throw std::runtime_error("SRRIP OnHit without begin");
    if(event_planned_) throw std::runtime_error("SRRIP OnHit called after event planned");
    CheckCoordinates(set,way,sets_,ways_);
    planned_[set][way] = 0;

    event_planned_ = true;

    // throw std::runtime_error("Srrip::OnHit: TODO(A2--REPL-02)");
  }

  void OnInsert(std::uint32_t set, std::uint32_t way) override {
    // (void)set;
    // (void)way;
    
    if(!begun_) throw std::runtime_error("SRRIP OnInsert without begin");
    if(event_planned_) throw std::runtime_error("SRRIP OnInsert called after event planned");
    CheckCoordinates(set,way,sets_,ways_);
    planned_[set][way] = 2;

    event_planned_ = true;

    // throw std::runtime_error("Srrip::OnHit: TODO(A2--REPL-02)");
  }

  int SelectVictim(std::uint32_t set,
                   const std::vector<bool>& eligible) override {
    // (void)set;
    // (void)way;
    
    if(eligible.size()!=ways_) throw std::runtime_error("SRRIP SelectVictim eligible size mismatch");
    if(std::find(eligible.begin(),eligible.end(),true)==eligible.end()) throw std::runtime_error("SRRIP SelectVictim: no eligible ways");
    if(!begun_) throw std::runtime_error("SRRIP SelectVictim without begin");
    if(event_planned_) throw std::runtime_error("SRRIP SelectVictim called after event planned");
    CheckCoordinates(set,0,sets_,ways_);
    int max_val=-1,max_index=-1;

    for(uint32_t i=0;i<ways_;i++){
      if(eligible[i] && planned_[set][i]>max_val){
        max_val=planned_[set][i];
        max_index=i;
      }
    }

    if(max_val==-1) throw std::runtime_error("SRRIP SelectVictim: no eligible ways");

    int diff=3-max_val;
    if(diff>0){
      for(std::uint32_t i=0;i<ways_;i++) planned_[set][i]=std::min(3,planned_[set][i]+diff);
    }
    event_planned_ = true;
    return max_index;

    // throw std::runtime_error("Srrip::OnHit: TODO(A2--REPL-02)");
  }

  int PeekVictim(std::uint32_t set,
                 const std::vector<bool>& eligible) const override {
    // (void)set;
    // (void)way;
    if(eligible.size()!=ways_) throw std::runtime_error("SRRIP PeekVictim eligible size mismatch");
    if(std::find(eligible.begin(),eligible.end(),true)==eligible.end()) throw std::runtime_error("SRRIP PeekVictim: no eligible ways");
    CheckCoordinates(set,0,sets_,ways_);
    int max_val=-1,max_index=-1;

    for(std::uint32_t i=0;i<ways_;i++){
      if(eligible[i] && current_[set][i]>max_val){
        max_val=current_[set][i];
        max_index=i;
      }
    }

    if(max_val==-1) throw std::runtime_error("SRRIP SelectVictim: no eligible ways");
    return max_index;

    // throw std::runtime_error("Srrip::OnHit: TODO(A2--REPL-02)");
  }

  std::vector<std::uint32_t> DebugState(std::uint32_t set) const override {
    // (void)set;
    CheckCoordinates(set, 0, sets_ , ways_);
    return std::vector<std::uint32_t>(current_[set].begin(), current_[set].end());
    
    // throw std::runtime_error("Srrip::DebugState: TODO(A2--REPL-02)");
  }

  void Commit() override {
    if (!begun_) throw std::runtime_error("SRRIP Commit without begin");
    current_ = planned_;
    begun_ = false;
    event_planned_ = false;
  }

 private:
  std::uint32_t sets_;
  std::uint32_t ways_;
  std::vector<std::vector<std::uint8_t>> current_;
  std::vector<std::vector<std::uint8_t>> planned_;
  bool begun_ = false;
  bool event_planned_ = false;
};

}  // namespace

// TODO(A2-REPL-03): return the requested concrete policy for the supplied
// sets/ways, or fail on an impossible enumerator. Worked example below.
std::unique_ptr<ReplacementPolicy> MakeReplacementPolicy(
    ReplacementKind kind, std::uint32_t sets, std::uint32_t ways) {
  switch (kind) {
    case ReplacementKind::kTreePlru:
      return std::unique_ptr<ReplacementPolicy>(new TreePlru(sets, ways));
    case ReplacementKind::kSrrip:
      return std::unique_ptr<ReplacementPolicy>(new Srrip(sets, ways));
  }
  throw std::runtime_error("unknown replacement policy");
}

}  // namespace cache
}  // namespace pipesim
