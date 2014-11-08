/// @file indexed_fault_tree.cc
/// Implementation of IndexedFaultTree class.
#include "indexed_fault_tree.h"

#include <ctime>

#include "event.h"
#include "indexed_gate.h"
#include "logger.h"

namespace scram {

IndexedFaultTree::IndexedFaultTree(int top_event_id, int limit_order)
  : top_event_index_(top_event_id),
    new_gate_index_(0),
    limit_order_(limit_order),
    top_event_sign_(1) {}

IndexedFaultTree::~IndexedFaultTree() {
  boost::unordered_map<int, IndexedGate*>::iterator it;
  for (it = indexed_gates_.begin(); it != indexed_gates_.end(); ++it) {
    delete it->second;
  }
}

void IndexedFaultTree::InitiateIndexedFaultTree(
    const boost::unordered_map<int, GatePtr>& int_to_inter,
    const boost::unordered_map<std::string, int>& all_to_int) {
  boost::unordered_map<int, GatePtr>::const_iterator it;
  for (it = int_to_inter.begin(); it != int_to_inter.end(); ++it) {
    IndexedGate* gate = new IndexedGate(it->first);
    gate->string_type(it->second->type());  // Get the original gate type.
    if (gate->string_type() == "atleast")
      gate->vote_number(it->second->vote_number());

    typedef boost::shared_ptr<Event> EventPtr;

    const std::map<std::string, EventPtr>* children =
        &it->second->children();
    std::map<std::string, EventPtr>::const_iterator it_children;
    for (it_children = children->begin();
         it_children != children->end(); ++it_children) {
      gate->InitiateWithChild(all_to_int.find(it_children->first)->second);
    }
    indexed_gates_.insert(std::make_pair(it->first, gate));
    if (gate->index() > new_gate_index_) new_gate_index_ = gate->index() + 1;
  }

  assert(top_event_index_ == indexed_gates_.begin()->first);
  IndexedGate* top = indexed_gates_.find(top_event_index_)->second;
  std::set<int> processed_gates;
  IndexedFaultTree::GatherParentInformation(top, &processed_gates);

  IndexedFaultTree::UnrollGates();
}

void IndexedFaultTree::PropagateConstants(
    const std::set<int>& true_house_events,
    const std::set<int>& false_house_events) {
  IndexedGate* top = indexed_gates_.find(top_event_index_)->second;
  std::set<int> processed_gates;
  LOG() << "Propagating constants in a fault tree.";
  IndexedFaultTree::PropagateConstants(true_house_events, false_house_events,
                                       top, &processed_gates);
  LOG() << "Constant propagation is done.";
}

void IndexedFaultTree::ProcessIndexedFaultTree(int num_basic_events) {
  // Detect original modules for processing.
  IndexedFaultTree::DetectModules(num_basic_events);
}

void IndexedFaultTree::FindMcs() {
  // It is assumed that the tree is layered with OR and AND gates on each
  // level. That is, one level contains only AND or OR gates.
  // AND gates are operated; whereas, OR gates are left for later minimal
  // cut set finding. This operations make a big tree consisting of
  // only OR gates. The function assumes the tree contains only positive gates.
  std::clock_t start_time;
  start_time = std::clock();

  LOG() << "Start minimal cut set generation.";

  std::vector<SimpleGatePtr> min_gates;  // This AND gates are minimal.
  // Generate main minimal cut set gates from top modue.
  IndexedFaultTree::FindMcsFromModule(top_event_sign_ * top_event_index_,
                                      &min_gates);

  // Container of already processed modules. Note that the sign of
  // indices matter because positive module is not the same as negative.
  // Top module is not expected to re-occur in minimal cut sets.
  std::map<int, std::vector<SimpleGatePtr> > processed_modules;

  while(!min_gates.empty()) {
    SimpleGatePtr cut_set = min_gates.back();
    min_gates.pop_back();
    assert(cut_set->gates().empty());

    if (cut_set->modules().empty()) {
      imcs_.push_back(cut_set->basic_events());

    } else {
      /// @todo Optimize to detect already expanded modules to get in first.
      /// @todo Optimize to handle faster modules with basic events only.
      std::vector<SimpleGatePtr> new_sets;
      SimpleGatePtr proto_set(new SimpleGate(2));
      proto_set->basic_events(cut_set->basic_events());
      new_sets.push_back(proto_set);
      std::set<int>::const_iterator it;
      for (it = cut_set->modules().begin(); it != cut_set->modules().end();
           ++it) {
        if (!processed_modules.count(*it)) {
          std::vector<SimpleGatePtr> module_mcs;  // For new MCS of a module.
          if (processed_modules.count(-*it)) {
            // The negation of this module has already been expanded.
            // This means that the original module cannot be treated again
            // because it was transformed upon traversal that may make another
            // traversal invalid.

            // Complement of OR gate.
            SimpleGatePtr complement(new SimpleGate(2));
            std::vector<SimpleGatePtr>* mcs =
                &processed_modules.find(-*it)->second;
            std::vector<SimpleGatePtr>::const_iterator it_mcs;
            for (it_mcs = mcs->begin(); it_mcs != mcs->end(); ++it_mcs) {
              SimpleGatePtr child(new SimpleGate(1));  // Complement of AND.
              complement->AddChildGate(child);
              // Reverse iterator is for optimization of inverting sorted
              // range by chaning the sign.
              std::set<int>::reverse_iterator it_r;
              for (it_r = (*it_mcs)->basic_events().rbegin();
                   it_r != (*it_mcs)->basic_events().rend(); ++it_r) {
                child->InitiateWithBasic(-*it_r);
              }
              for (it_r = (*it_mcs)->modules().rbegin();
                   it_r != (*it_mcs)->modules().rend(); ++it_r) {
                child->InitiateWithModule(-*it_r);
              }
            }
            IndexedFaultTree::FindMcsFromSimpleGate(complement, &module_mcs);

          } else {
            IndexedFaultTree::FindMcsFromModule(*it, &module_mcs);
          }
          processed_modules.insert(std::make_pair(*it, module_mcs));
        }
        std::vector<SimpleGatePtr>* module_mcs =
            &processed_modules.find(*it)->second;
        if (module_mcs->empty()) continue;  // This is a null set.
        std::vector<SimpleGatePtr> joined_sets;
        std::vector<SimpleGatePtr>::const_iterator it_n;
        for (it_n = new_sets.begin(); it_n != new_sets.end(); ++it_n) {
          std::vector<SimpleGatePtr>::iterator it_m;
          for (it_m = module_mcs->begin(); it_m != module_mcs->end();
               ++it_m) {
            if (((*it_n)->basic_events().size() +
                 (*it_m)->basic_events().size()) <= limit_order_) {
              SimpleGatePtr new_set(new SimpleGate(**it_n));
              new_set->JoinAsMcs(*it_m);
              joined_sets.push_back(new_set);
            }
          }
        }
        new_sets = joined_sets;
      }
      /// @todo Optimize to detect basic-events-only cut sets earlier.
      min_gates.insert(min_gates.end(), new_sets.begin(), new_sets.end());
    }
  }

  double mcs_time = (std::clock() - start_time) /
                    static_cast<double>(CLOCKS_PER_SEC);
  LOG() << "The number of MCS found: " << imcs_.size();
  LOG() << "Minimal cut set finding time: " << mcs_time;
}

void IndexedFaultTree::FindMcsFromModule(
    int index,
    std::vector<SimpleGatePtr>* min_gates) {
  std::clock_t start_time;
  start_time = std::clock();

  LOG() << "IndexedFaultTree: Start post-processing gate to simple gate.";

  assert(index != 0);
  IndexedGate* gate = indexed_gates_.find(std::abs(index))->second;

  // This gate is a top event of an independent sub-tree.
  // Preprocess complex gates.
  IndexedFaultTree::UnrollComplexTopGate(gate);

  // Upon unrolling the tree, the top event may be detected to be complement.
  // This fact is processed before giving the top event to complement
  // propagation function.
  if (index < 0) {
    gate->type(gate->type() == 1 ? 2 : 1);
    gate->InvertChildren();
  }
  std::map<int, int> complements;
  std::set<int> processed_gates;
  IndexedFaultTree::PropagateComplements(gate, &complements,
                                         &processed_gates);
  // After this point there should not be any negative gates.

  processed_gates.clear();
  IndexedFaultTree::JoinGates(gate, &processed_gates);
  // After this point there might be null AND gates, and the tree structure
  // should be repeating OR and AND.

  processed_gates.clear();
  IndexedFaultTree::ProcessNullGates(gate, &processed_gates);
  /// @todo Consider joining if there has been removal and one child gates.

  std::map<int, SimpleGatePtr> processed_simple_gates;
  SimpleGatePtr simple_gate =
      IndexedFaultTree::CreateSimpleTree(std::abs(index),
                                         &processed_simple_gates);

  // Duration of simple tree generation.
  double simple_tree_time = (std::clock() - start_time) /
                            static_cast<double>(CLOCKS_PER_SEC);
  LOG() << "Simple tree creation time: " << simple_tree_time;

  IndexedFaultTree::FindMcsFromSimpleGate(simple_gate, min_gates);
}

void IndexedFaultTree::FindMcsFromSimpleGate(
    SimpleGatePtr& gate,
    std::vector<SimpleGatePtr>* min_gates) {
  std::clock_t start_time;
  start_time = std::clock();

  // Expanding AND gate with basic event children and OR gate children.
  if (gate->basic_events().empty() && gate->gates().empty() &&
      gate->modules().empty()) return;
  LOG() << "IndexedFaultTree: Finding MCS for non-empty gate.";
  if (gate->type() == 2) {
    if (gate->basic_events().size() > limit_order_) {
      return;  // No cut set generation for this level.
    } else if (gate->gates().empty()) {
      // The special case of the gate is only cut set.
      /// @todo This may be removed by early detection optimization or
      ///       reusing the passed gate as a minimal cut set.
      SimpleGatePtr only_set(new SimpleGate(*gate));
      min_gates->push_back(only_set);
      return;
    }
    IndexedFaultTree::ExpandAndLayer(gate);
  }
  std::vector<SimpleGatePtr> cut_sets;
  IndexedFaultTree::ExpandOrLayer(gate, &cut_sets);

  LOG() << "Non-Unique cut sets generated: " << cut_sets.size();
  double cut_sets_time = (std::clock() - start_time) /
                         static_cast<double>(CLOCKS_PER_SEC);
  LOG() << "Cut set generation time: " << cut_sets_time;
  LOG() << "Top gate's gate children: " << gate->gates().size();

  // At this point cut sets must be generated.
  SetPtrComp comp;
  std::set<SimpleGatePtr, SetPtrComp> unique_cut_sets(comp);

  std::set<SimpleGatePtr, SetPtrComp>  one_element_sets;
  // Special case when top gate OR has basic events and modules.
  std::set<int>::iterator it_b;
  for (it_b = gate->basic_events().begin();
       it_b != gate->basic_events().end(); ++it_b) {
    SimpleGatePtr new_set(new SimpleGate(2));
    new_set->InitiateWithBasic(*it_b);
    one_element_sets.insert(one_element_sets.end(), new_set);
  }
  for (it_b = gate->modules().begin();
       it_b != gate->modules().end(); ++it_b) {
    SimpleGatePtr new_set(new SimpleGate(2));
    new_set->InitiateWithModule(*it_b);
    one_element_sets.insert(one_element_sets.end(), new_set);
  }
  std::vector<SimpleGatePtr>::const_iterator it;
  for (it = cut_sets.begin(); it != cut_sets.end(); ++it) {
    assert((*it)->type() == 2);
    assert((*it)->gates().empty());
    if ((*it)->basic_events().size() == 1 && (*it)->modules().empty()) {
      one_element_sets.insert(*it);
    } else if ((*it)->modules().size() == 1 && (*it)->basic_events().empty()) {
      one_element_sets.insert(*it);
    } else {
      unique_cut_sets.insert(*it);
    }
  }
  std::vector<SimpleGatePtr> sets_unique;
  std::set<SimpleGatePtr, SetPtrComp>::iterator it_un;
  for (it_un = unique_cut_sets.begin(); it_un != unique_cut_sets.end();
       ++it_un) {
    assert(!(*it_un)->modules().empty() || !(*it_un)->basic_events().empty());
    if ((*it_un)->basic_events().size() == 1 &&
        (*it_un)->modules().empty()) {
      one_element_sets.insert(*it);
    } else if ((*it_un)->modules().size() == 1 &&
               (*it_un)->basic_events().empty()) {
      one_element_sets.insert(*it);
    } else {
      sets_unique.push_back(*it_un);
    }
  }

  min_gates->reserve(sets_unique.size() + one_element_sets.size());
  std::set<SimpleGatePtr, SetPtrComp>::const_iterator it_s;
  for (it_s = one_element_sets.begin(); it_s != one_element_sets.end();
       ++it_s) {
    min_gates->push_back(*it_s);
  }

  LOG() << "Unique cut sets size: " << sets_unique.size();
  LOG() << "One element sets size: " << min_gates->size();

  LOG() << "IndexedFaultTree: Minimizing the cut sets.";
  IndexedFaultTree::MinimizeCutSets(sets_unique, *min_gates, 2, min_gates);
}

void IndexedFaultTree::UnrollGates() {
  LOG() << "Unrolling basic gates.";
  // Handle spacial case for a top event.
  IndexedGate* top_gate = indexed_gates_.find(top_event_index_)->second;
  std::string type = top_gate->string_type();
  assert(type != "undefined");
  top_event_sign_ = 1;  // For positive gates.
  if (type == "nor") {
    top_gate->string_type("or");
    top_event_sign_ = -1;  // For negative gates.
  } else if (type == "nand") {
    top_gate->string_type("and");
    top_event_sign_ = -1;
  } else if (type == "not") {
    top_gate->string_type("and");
    top_event_sign_ = -1;
  } else if (type == "null") {
    top_gate->string_type("and");
    top_event_sign_ = 1;
  } else if (type == "or") {
    top_gate->type(1);
  } else if (type == "and") {
    top_gate->type(2);
  }
  // Assumes that all gates are in indexed_gates_ container.
  boost::unordered_map<int, IndexedGate*>::iterator it
      = indexed_gates_.begin();
  assert(top_event_index_ == it->first);
  for (++it; it != indexed_gates_.end(); ++it) {
    IndexedFaultTree::UnrollGate(it->second);
  }
  LOG() << "Finished unrolling basic gates.";
}

void IndexedFaultTree::UnrollGate(IndexedGate* gate) {
  std::string type = gate->string_type();
  assert(type != "undefined");
  // Deal with negative gate.
  if (type == "nor" || type == "not" || type == "nand") {
    int child_index = gate->index();
    std::set<int>::const_iterator it;
    for (it = gate->parents().begin(); it != gate->parents().end(); ++it) {
      IndexedGate* parent = indexed_gates_.find(*it)->second;
      if (parent->children().count(child_index)) {  // Positive child.
        bool ret = parent->SwapChild(child_index, -child_index);
        assert(ret);
      } else {  // Negative child.
        bool ret = parent->SwapChild(-child_index, child_index);
        assert(ret);
      }
    }
  }
  if (type == "or") {
    gate->type(1);
  } else if (type == "and" || type == "null") {
    gate->type(2);
    gate->string_type("and");
  } else if (type == "not" || type == "nand") {
    gate->string_type("and");
    gate->type(2);
  } else if (type == "nor") {
    gate->string_type("or");
    gate->type(1);
  }
  // Do not touch XOR or ATLEAST gates. Leave them undefined.
}

void IndexedFaultTree::UnrollComplexTopGate(IndexedGate* top_gate) {
  LOG() << "Unrolling complex gates.";
  std::string type = top_gate->string_type();
  assert(type != "undefined");
  if (type == "xor") {
    IndexedFaultTree::UnrollXorGate(top_gate);
  } else if (type == "atleast") {
    IndexedFaultTree::UnrollAtleastGate(top_gate);
  }
  std::set<int> unrolled_gates;
  IndexedFaultTree::UnrollComplexGates(top_gate, &unrolled_gates);
  LOG() << "Finished unrolling complex gates.";
}

void IndexedFaultTree::UnrollComplexGates(IndexedGate* parent_gate,
                                          std::set<int>* unrolled_gates) {
  std::set<int>::const_iterator it;
  for (it = parent_gate->children().begin();
       it != parent_gate->children().end();) {
    if (std::abs(*it) > top_event_index_ &&
        !unrolled_gates->count(std::abs(*it)) &&
        !modules_.count(std::abs(*it))) {
      IndexedGate* gate = indexed_gates_.find(std::abs(*it))->second;
      unrolled_gates->insert(gate->index());
      std::string type = gate->string_type();
      assert(type != "undefined");
      if (type == "xor") {
        IndexedFaultTree::UnrollXorGate(gate);
      } else if (type == "atleast") {
        IndexedFaultTree::UnrollAtleastGate(gate);
      }
      IndexedFaultTree::UnrollComplexGates(gate, unrolled_gates);
    }
    ++it;
  }
}

void IndexedFaultTree::UnrollXorGate(IndexedGate* gate) {
  assert(gate->children().size() == 2);
  std::set<int>::const_iterator it = gate->children().begin();
  IndexedGate* gate_one = new IndexedGate(++new_gate_index_);
  IndexedGate* gate_two = new IndexedGate(++new_gate_index_);

  gate->type(1);
  gate->string_type("or");
  gate_one->type(2);
  gate_two->type(2);
  gate_one->string_type("and");
  gate_two->string_type("and");
  indexed_gates_.insert(std::make_pair(gate_one->index(), gate_one));
  indexed_gates_.insert(std::make_pair(gate_two->index(), gate_two));

  gate_one->AddChild(*it);
  gate_two->AddChild(-*it);
  ++it;
  gate_one->AddChild(-*it);
  gate_two->AddChild(*it);
  gate->EraseAllChildren();
  gate->AddChild(gate_one->index());
  gate->AddChild(gate_two->index());
}

void IndexedFaultTree::UnrollAtleastGate(IndexedGate* gate) {
  int vote_number = gate->vote_number();

  assert(vote_number > 1);
  assert(gate->children().size() > vote_number);
  std::set< std::set<int> > all_sets;
  const std::set<int>* children = &gate->children();
  int size = children->size();

  std::set<int>::iterator it;
  for (it = children->begin(); it != children->end(); ++it) {
    std::set<int> set;
    set.insert(*it);
    all_sets.insert(set);
  }
  for (int i = 1; i < vote_number; ++i) {
    std::set< std::set<int> > tmp_sets;
    std::set< std::set<int> >::iterator it_sets;
    for (it_sets = all_sets.begin(); it_sets != all_sets.end(); ++it_sets) {
      for (it = children->begin(); it != children->end(); ++it) {
        std::set<int> set(*it_sets);
        set.insert(*it);
        if (set.size() > i) {
          tmp_sets.insert(set);
        }
      }
    }
    all_sets = tmp_sets;
  }

  gate->type(1);
  gate->string_type("or");
  gate->EraseAllChildren();
  std::set< std::set<int> >::iterator it_sets;
  for (it_sets = all_sets.begin(); it_sets != all_sets.end(); ++it_sets) {
    IndexedGate* gate_one = new IndexedGate(++new_gate_index_);
    gate_one->type(2);
    gate_one->string_type("and");
    std::set<int>::iterator it;
    for (it = it_sets->begin(); it != it_sets->end(); ++it) {
      bool ret = gate_one->AddChild(*it);
      assert(ret);
    }
    gate->AddChild(gate_one->index());
    indexed_gates_.insert(std::make_pair(gate_one->index(), gate_one));
  }
}

void IndexedFaultTree::PropagateConstants(
    const std::set<int>& true_house_events,
    const std::set<int>& false_house_events,
    IndexedGate* gate,
    std::set<int>* processed_gates) {
  if (processed_gates->count(gate->index())) return;
  processed_gates->insert(gate->index());
  // True house event in AND gate is removed.
  // False house event in AND gate makes the gate NULL.
  // True house event in OR gate makes the gate Unity, and it shouldn't appear
  // in minimal cut sets.
  // False house event in OR gate is removed.
  // Unity must be only due to House event.
  // Null can be due to house events or complement elments.
  // True and false house events are treated as well for XOR and ATLEAST gates.
  std::set<int>::const_iterator it;
  /// @todo This may have bad behavior due to erased children. Needs more
  ///       testing and optimization. The function needs simplification.
  for (it = gate->children().begin(); it != gate->children().end();) {
    bool state = false;  // Null or Unity case.
    if (std::abs(*it) > top_event_index_) {  // Processing a gate.
      IndexedGate* child_gate = indexed_gates_.find(std::abs(*it))->second;
      PropagateConstants(true_house_events, false_house_events, child_gate,
                         processed_gates);
      std::string string_state = child_gate->state();
      assert(string_state == "normal" || string_state == "null" ||
             string_state == "unity");
      if (string_state == "normal") {
        ++it;
        continue;
      } else if (string_state == "null") {
        state = *it > 0 ? false : true;
      } else if (string_state == "unity") {
        state = *it > 0 ? true : false;
      }
    } else {  // Processing a primary event.
      if (false_house_events.count(std::abs(*it))) {
        state = *it > 0 ? false : true;
      } else if (true_house_events.count(std::abs(*it))) {
        state = *it > 0 ? true : false;
      } else {
        ++it;
        continue;
      }
    }

    std::string parent_type = gate->string_type();
    assert(parent_type == "or" || parent_type == "and" ||
           parent_type == "xor" || parent_type == "atleast");

    if (!state) {  // Null state.
      if (parent_type == "or") {
        gate->EraseChild(*it);  // OR gate with null child.
        if (gate->children().empty()) {
          gate->Nullify();
          return;
        }
        it = gate->children().begin();
        continue;
      } else if (parent_type == "and") {
        // AND gate with null child.
        gate->Nullify();
        return;
      } else if (parent_type == "xor") {
        assert(gate->children().size() == 2);
        gate->EraseChild(*it);  // XOR gate with null child.
        assert(!gate->children().empty());
        gate->string_type("or");
        gate->type(1);
        it = gate->children().begin();
        continue;
      } else if (parent_type == "atleast") {
        gate->EraseChild(*it);
        if (gate->vote_number() == gate->children().size()) {
          gate->string_type("and");
          gate->type(2);
        }
        it = gate->children().begin();
        continue;
      }
    } else {  // Unity state.
      if (parent_type == "or") {
        gate->MakeUnity();
        return;
      } else if (parent_type == "and"){
        gate->EraseChild(*it);
        if (gate->children().empty()) {
          gate->MakeUnity();
          return;
        }
        it = gate->children().begin();
        continue;
      } else if (parent_type == "xor") {
        assert(gate->children().size() == 2);
        gate->EraseChild(*it);
        assert(!gate->children().empty());
        gate->string_type("or");
        gate->type(1);
        int ch = *gate->children().begin();
        gate->SwapChild(ch, -ch);
        it = gate->children().begin();
        continue;
      } else if (parent_type == "atleast") {
        assert(gate->vote_number() > 1);
        assert(gate->children().size() > 2);
        gate->EraseChild(*it);
        gate->vote_number(gate->vote_number() - 1);
        if (gate->vote_number() == 1) {
          gate->type(1);
          gate->string_type("or");
        }
        it = gate->children().begin();
        continue;
      }
    }
  }
}

void IndexedFaultTree::GatherParentInformation(
    const IndexedGate* parent_gate,
    std::set<int>* processed_gates) {
  if (processed_gates->count(parent_gate->index())) return;
  processed_gates->insert(parent_gate->index());

  std::set<int>::const_iterator it;
  for (it = parent_gate->children().begin();
       it != parent_gate->children().end(); ++it) {
    int index = std::abs(*it);
    if (index > top_event_index_) {
      IndexedGate* child = indexed_gates_.find(index)->second;
      child->AddParent(parent_gate->index());
      IndexedFaultTree::GatherParentInformation(child, processed_gates);
    }
  }
}

void IndexedFaultTree::DetectModules(int num_basic_events) {
  // At this stage only AND/OR/XOR/ATLEAST gates are present.
  // All one element gates and non-coherent gates are converted and processed.
  // All constants are propagated and there is only gates and basic events.
  // First stage, traverse the tree depth-first for gates and indicate
  // visit time for each node.

  LOG() << "Detecting modules in a fault tree.";

  // First and last visits of basic events.
  // Basic events are indexed 1 to the number of basic events sequentially.
  int visit_basics[num_basic_events + 1][2];
  for (int i = 0; i < num_basic_events + 1; ++i) {
    visit_basics[i][0] = 0;
    visit_basics[i][1] = 0;
  }

  IndexedGate* top_gate = indexed_gates_.find(top_event_index_)->second;
  int time = 0;
  IndexedFaultTree::AssignTiming(time, top_gate, visit_basics);

  LOG() << "Timings are assigned to nodes.";

  int min_time = 0;
  int max_time = 0;
  std::map<int, std::pair<int, int> > visited_gates;
  IndexedFaultTree::FindOriginalModules(top_gate, visit_basics,
                                        &visited_gates,
                                        &min_time, &max_time);
  assert(min_time == 1);
  assert(max_time == top_gate->visits()[2]);

  LOG() << "Detected number of original modules: " << modules_.size();
}

int IndexedFaultTree::AssignTiming(int time, IndexedGate* gate,
                                   int visit_basics[][2]) {
  if (gate->Visit(++time)) return time;  // Revisited gate.

  std::set<int>::const_iterator it;
  for (it = gate->children().begin(); it != gate->children().end(); ++it) {
    int index = std::abs(*it);
    if (index < top_event_index_) {
      if (!visit_basics[index][0]) {
        visit_basics[index][0] = ++time;
        visit_basics[index][1] = time;
      } else {
        visit_basics[index][1] = ++time;
      }
    } else {
      time = IndexedFaultTree::AssignTiming(time,
                                            indexed_gates_.find(index)->second,
                                            visit_basics);
    }
  }
  bool re_visited = gate->Visit(++time);  // Exiting the gate in second visit.
  assert(!re_visited);  // No cyclic visiting.
  return time;
}

void IndexedFaultTree::FindOriginalModules(
    IndexedGate* gate,
    const int visit_basics[][2],
    std::map<int, std::pair<int, int> >* visited_gates,
    int* min_time,
    int* max_time) {
  /// @todo This must get optimized if needed.
  if (visited_gates->count(gate->index())) {
    *min_time = visited_gates->find(gate->index())->second.first;
    *max_time = visited_gates->find(gate->index())->second.second;
    return;
  }
  *min_time = gate->visits()[0];
  *max_time = gate->visits()[1];

  std::set<int>::const_iterator it;
  for (it = gate->children().begin(); it != gate->children().end(); ++it) {
    int index = std::abs(*it);
    int min = 0;
    int max = 0;
    if (index < top_event_index_) {
        min = visit_basics[index][0];
        max = visit_basics[index][1];

    } else {
      IndexedFaultTree::FindOriginalModules(indexed_gates_.find(index)->second,
                                            visit_basics, visited_gates,
                                            &min, &max);
    }
    assert(min != 0);
    assert(max != 0);
    if (min < *min_time) *min_time = min;
    if (max > *max_time) *max_time = max;
  }

  // Determine if this gate is module itself.
  if (*min_time == gate->visits()[0] && *max_time == gate->visits()[1]) {
    modules_.insert(gate->index());
  }
  if (gate->visits()[2] > *max_time) *max_time = gate->visits()[2];
  visited_gates->insert(std::make_pair(gate->index(),
                                       std::make_pair(*min_time, *max_time)));
}

void IndexedFaultTree::PropagateComplements(
    IndexedGate* gate,
    std::map<int, int>* gate_complements,
    std::set<int>* processed_gates) {
  // If the child gate is complement, then create a new gate that propagates
  // its sign to its children and itself becomes non-complement.
  // Keep track of complement gates for optimization of repeted complements.
  std::set<int>::const_iterator it;
  for (it = gate->children().begin(); it != gate->children().end();) {
    if (std::abs(*it) > top_event_index_ && !modules_.count(std::abs(*it))) {
      if (*it < 0) {
        if (gate_complements->count(-*it)) {
          gate->SwapChild(*it, gate_complements->find(-*it)->second);
        } else {
          IndexedGate* complement_gate = new IndexedGate(++new_gate_index_);
          indexed_gates_.insert(std::make_pair(complement_gate->index(),
                                               complement_gate));
          gate_complements->insert(std::make_pair(-*it,
                                                  complement_gate->index()));
          int existing_type = indexed_gates_.find(-*it)->second->type();
          complement_gate->type(existing_type == 1 ? 2 : 1);
          complement_gate->children(
              indexed_gates_.find(-*it)->second->children());
          complement_gate->InvertChildren();
          gate->SwapChild(*it, complement_gate->index());
          processed_gates->insert(complement_gate->index());
          IndexedFaultTree::PropagateComplements(complement_gate,
                                                 gate_complements,
                                                 processed_gates);
        }
        // Note that the iterator is invalid now.
        it = gate->children().begin();  // The negative gates at the start.
        continue;
      } else if (!processed_gates->count(*it)) {
        // Continue with the positive gate children.
        processed_gates->insert(*it);
        IndexedFaultTree::PropagateComplements(indexed_gates_.find(*it)->second,
                                               gate_complements,
                                               processed_gates);
      }
    }
    ++it;
  }
}

void IndexedFaultTree::JoinGates(IndexedGate* gate,
                                 std::set<int>* processed_gates) {
  if (processed_gates->count(gate->index())) return;
  processed_gates->insert(gate->index());
  int parent = gate->type();
  std::set<int>::const_iterator it;
  for (it = gate->children().begin(); it != gate->children().end();) {
    if (std::abs(*it) > top_event_index_ && !modules_.count(std::abs(*it))) {
      assert(*it > 0);  // All the gates must be positive.
      IndexedGate* child_gate = indexed_gates_.find(std::abs(*it))->second;
      int child = child_gate->type();
      if (parent == child) {
        if (!gate->MergeGate(indexed_gates_.find(*it)->second)) {
          break;
        } else {
          it = gate->children().begin();
          continue;
        }
      } else if (child_gate->children().size() == 1) {
        // This must be from null, not, or some reduced gate.
        bool ret = gate->SwapChild(*it, *child_gate->children().begin());
        if (!ret) {
          break;
        } else {
          it = gate->children().begin();
          continue;
        }
      } else {
        IndexedFaultTree::JoinGates(child_gate, processed_gates);
      }
    }
    ++it;
  }
}

void IndexedFaultTree::ProcessNullGates(IndexedGate* gate,
                                        std::set<int>* processed_gates) {
  // NULL gates' parent: OR->Remove the child and AND->NULL the parent.
  // At this stage, only positive gates are left.
  // The tree structure is repeating ...->OR->AND->OR->...

  if (processed_gates->count(gate->index())) return;
  processed_gates->insert(gate->index());

  if (gate->state() == "null") {
    assert(gate->type() == 2);
    gate->EraseAllChildren();
    return;
  }

  if (gate->type() == 1) {
    assert(gate->state() != "null");
    std::set<int>::const_iterator it;
    for (it = gate->children().begin(); it != gate->children().end();) {
      if (std::abs(*it) > top_event_index_ && !modules_.count(std::abs(*it))) {
        assert(*it > 0);
        IndexedGate* child_gate = indexed_gates_.find(*it)->second;
        assert(child_gate->type() == 2);
        IndexedFaultTree::ProcessNullGates(child_gate, processed_gates);
        if (child_gate->state() == "null") {
          gate->EraseChild(*it);
          it = gate->children().begin();
          continue;
        }
      }
      ++it;
    }
    if (gate->children().empty()) gate->Nullify();
  } else {
    std::set<int>::const_iterator it;
    for (it = gate->children().begin(); it != gate->children().end(); ++it) {
      if (std::abs(*it) > top_event_index_ && !modules_.count(std::abs(*it))) {
        assert(*it > 0);
        IndexedGate* child_gate = indexed_gates_.find(*it)->second;
        assert(child_gate->type() == 1);
        IndexedFaultTree::ProcessNullGates(child_gate, processed_gates);
        if (child_gate->state() == "null") {
          gate->Nullify();
          return;
        }
      }
    }
  }
}

boost::shared_ptr<SimpleGate> IndexedFaultTree::CreateSimpleTree(
    int gate_index,
    std::map<int, SimpleGatePtr>* processed_gates) {
  assert(gate_index > 0);
  if (processed_gates->count(gate_index))
    return processed_gates->find(gate_index)->second;
  IndexedGate* gate = indexed_gates_.find(gate_index)->second;
  SimpleGatePtr simple_gate(new SimpleGate(gate->type()));
  processed_gates->insert(std::make_pair(gate_index, simple_gate));

  std::set<int>::iterator it;
  for (it = gate->children().begin(); it != gate->children().end(); ++it) {
    if (std::abs(*it) > top_event_index_) {
      if (modules_.count(std::abs(*it))) {
        simple_gate->InitiateWithModule(*it);
      } else {
        simple_gate->AddChildGate(
            IndexedFaultTree::CreateSimpleTree(*it, processed_gates));
      }
    } else {
      assert(std::abs(*it) < top_event_index_);  // No negative gates.
      simple_gate->InitiateWithBasic(*it);
    }
  }
  return simple_gate;
}

void IndexedFaultTree::ExpandOrLayer(SimpleGatePtr& gate,
                                     std::vector<SimpleGatePtr>* cut_sets) {
  if (gate->gates().empty()) return;
  std::vector<SimpleGatePtr> new_gates;
  std::set<SimpleGatePtr>::iterator it;
  for (it = gate->gates().begin(); it != gate->gates().end(); ++it) {
    if ((*it)->basic_events().size() > limit_order_) {
      continue;
    } else if ((*it)->gates().empty()) {
      cut_sets->push_back(*it);
      continue;  // This may leave some larger cut sets for top event.
    }
    SimpleGatePtr new_gate(new SimpleGate(**it));
    IndexedFaultTree::ExpandAndLayer(new_gate);  // The gate becomes OR.
    IndexedFaultTree::ExpandOrLayer(new_gate, cut_sets);
    new_gates.push_back(new_gate);
  }
  gate->gates().clear();
  gate->gates().insert(new_gates.begin(), new_gates.end());
}

void IndexedFaultTree::ExpandAndLayer(SimpleGatePtr& gate) {
  assert(gate->type() == 2);
  assert(gate->basic_events().size() <= limit_order_);
  assert(!gate->gates().empty());
  // Create a new gate with OR logic instead of AND.
  SimpleGatePtr substitute(new SimpleGate(1));

  // The starting basic events for expansion.
  SimpleGatePtr child(new SimpleGate(2));
  child->basic_events(gate->basic_events());
  child->modules(gate->modules());
  substitute->AddChildGate(child);

  // Processes underlying OR layer gates.
  std::set<SimpleGatePtr>::iterator it_v;
  for (it_v = gate->gates().begin(); it_v != gate->gates().end(); ++it_v) {
    assert((*it_v)->type() == 1);
    // Create new sets for multiplication.
    std::set<SimpleGatePtr> children = substitute->gates();
    substitute->gates().clear();  // Prepare for new children gates.
    std::set<SimpleGatePtr>::iterator it;
    for (it = children.begin(); it != children.end(); ++it) {
      // Add basic events.
      std::set<int>::const_iterator it_b;
      for (it_b = (*it_v)->basic_events().begin();
           it_b != (*it_v)->basic_events().end(); ++it_b) {
        SimpleGatePtr new_child(new SimpleGate(**it));
        if (new_child->AddBasic(*it_b) &&
            new_child->basic_events().size() <= limit_order_)
          substitute->AddChildGate(new_child);
      }
      // Add modules just like basic events.
      std::set<int>::const_iterator it_m;
      for (it_m = (*it_v)->modules().begin();
           it_m != (*it_v)->modules().end(); ++it_m) {
        SimpleGatePtr new_child(new SimpleGate(**it));
        if (new_child->AddModule(*it_m)) substitute->AddChildGate(new_child);
      }
      // Join underlying AND layer gates.
      std::set<SimpleGatePtr>::iterator it_g;
      for (it_g = (*it_v)->gates().begin();
           it_g != (*it_v)->gates().end(); ++it_g) {
        SimpleGatePtr new_child(new SimpleGate(**it));
        if (new_child->MergeGate(*it_g) &&
            new_child->basic_events().size() <= limit_order_) {
          // This must be underlying AND layer.
          substitute->AddChildGate(new_child);
        }
      }
    }
  }
  gate = substitute;
}

void IndexedFaultTree::MinimizeCutSets(
    const std::vector<SimpleGatePtr>& cut_sets,
    const std::vector<SimpleGatePtr>& mcs_lower_order,
    int min_order,
    std::vector<SimpleGatePtr>* min_gates) {
  if (cut_sets.empty()) return;

  std::vector<SimpleGatePtr> temp_sets;  // For mcs of a level above.
  std::vector<SimpleGatePtr> temp_min_sets;  // For mcs of this level.

  std::vector<SimpleGatePtr>::const_iterator it_uniq;
  for (it_uniq = cut_sets.begin(); it_uniq != cut_sets.end(); ++it_uniq) {
    bool include = true;  // Determine to keep or not.

    std::vector<SimpleGatePtr>::const_iterator it_min;
    for (it_min = mcs_lower_order.begin(); it_min != mcs_lower_order.end();
         ++it_min) {
      if (std::includes((*it_uniq)->basic_events().begin(),
                        (*it_uniq)->basic_events().end(),
                        (*it_min)->basic_events().begin(),
                        (*it_min)->basic_events().end()) &&
          std::includes((*it_uniq)->modules().begin(),
                        (*it_uniq)->modules().end(),
                        (*it_min)->modules().begin(),
                        (*it_min)->modules().end())) {
        // Non-minimal cut set is detected.
        include = false;
        break;
      }
    }
    // After checking for non-minimal cut sets,
    // all minimum sized cut sets are guaranteed to be minimal.
    if (include) {
      if (((*it_uniq)->basic_events().size() + (*it_uniq)->modules().size()) ==
          min_order) {
        temp_min_sets.push_back(*it_uniq);
      } else {
        temp_sets.push_back(*it_uniq);
      }
    }
    // Ignore the cut set because include = false.
  }
  min_gates->insert(min_gates->end(), temp_min_sets.begin(),
                    temp_min_sets.end());
  min_order++;
  IndexedFaultTree::MinimizeCutSets(temp_sets, temp_min_sets, min_order,
                                    min_gates);
}

}  // namespace scram
