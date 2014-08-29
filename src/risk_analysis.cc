/// @file risk_analysis.cc
#include "risk_analysis.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/pointer_cast.hpp>

namespace scram {

RiskAnalysis::RiskAnalysis(std::string config_file)
    : prob_requested_(false),
      input_file_(""),
      parent_(""),
      id_(""),
      type_(""),
      vote_number_(-1) {
  // Add valid gate types.
  gate_types_.insert("and");
  gate_types_.insert("or");
  gate_types_.insert("not");
  gate_types_.insert("nor");
  gate_types_.insert("nand");
  gate_types_.insert("xor");
  gate_types_.insert("null");
  gate_types_.insert("inhibit");
  gate_types_.insert("vote");
  gate_types_.insert("atleast");

  // Add valid primary event types.
  types_.insert("basic");
  types_.insert("undeveloped");
  types_.insert("house");
  types_.insert("conditional");

  // Initialize a fault tree with a default name.
  FaultTreePtr fault_tree_;

  fta_ = new FaultTreeAnalysis("default");
  env_ = new Env();
}

void RiskAnalysis::ProcessInput(std::string xml_file) {
  std::ifstream file_stream(xml_file.c_str());
  if (!file_stream) {
    throw IOError("The file '" + xml_file + "' could not be loaded.");
  }

  input_file_ = xml_file;

  std::stringstream stream;
  stream << file_stream.rdbuf();
  file_stream.close();

  boost::shared_ptr<XMLParser> parser(new XMLParser());
  parser->Init(stream);

  std::stringstream schema;
  std::string schema_path = env_->rng_schema();
  std::ifstream schema_stream(schema_path.c_str());
  schema << schema_stream.rdbuf();
  schema_stream.close();
  parser->Validate(schema);

  xmlpp::Document* doc = parser->Document();
  const xmlpp::Node* root = doc->get_root_node();
  assert(root->get_name() == "opsa-mef");
  xmlpp::Node::NodeList roots_children = root->get_children();
  xmlpp::Node::NodeList::iterator it_ch;
  for (it_ch = roots_children.begin(); it_ch != roots_children.end(); ++it_ch) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it_ch);
    if (!element) continue;  // Ignore non-elements.

    std::string name = element->get_name();
    /// @todo Switch to function pointers.
    if (name == "define-fault-tree") {
      // Handle the fault tree initialization.
      RiskAnalysis::DefineFaultTree(element);
    } else if (name == "model-data") {
      // Handle the data.
      RiskAnalysis::ProcessModelData(element);
    } else {
      // Not yet capable of handling other analysis.
      throw(scram::ValidationError("Cannot handle '" + name + "'"));
    }
  }

  // Check if all events are initialized.
  /// @todo Print Names of events.
  /// @todo Warning about extra events being initialized and unused.
  /// @todo all_events container is not being updated.
  if (!tbd_house_events_.empty()) {
    // This is default and assumed.
    prob_requested_ = true;
    boost::unordered_map<std::string, HouseEventPtr>::iterator it;
    for (it = tbd_house_events_.begin(); it != tbd_house_events_.end();
         ++it) {
      it->second->p(0);  // False House event is the default.
      primary_events_.insert(std::make_pair(it->first, it->second));
    }
  }
  if (!tbd_basic_events_.empty()) {
    prob_requested_ = false;
    boost::unordered_map<std::string, BasicEventPtr>::iterator it;
    for (it = tbd_basic_events_.begin(); it != tbd_basic_events_.end();
         ++it) {
      primary_events_.insert(std::make_pair(it->first, it->second));
    }
  }

  if (!tbd_events_.empty()) {
    prob_requested_ = false;
    boost::unordered_map<std::string, std::vector<GatePtr> >::iterator it;
    for (it = tbd_events_.begin(); it != tbd_events_.end(); ++it) {
      std::vector<GatePtr>::iterator itvec = it->second.begin();
      BasicEventPtr child(new BasicEvent(it->first));
      for (; itvec != it->second.end(); ++itvec) {
        (*itvec)->AddChild(child);
      }
    }
    /// @todo either clean tbd containers or warn the user.
  }
  if (!tbd_gates_.empty()) {
    std::stringstream ss;
    boost::unordered_map<std::string, GatePtr>::iterator it;
    for (it = tbd_gates_.begin(); it != tbd_gates_.end(); ++it) {
      std::string id = it->first;
      boost::to_upper(id);
      ss << id << "\n";
    }
    throw scram::ValidationError("Undefined gates:\n" + ss.str());
  }

  // Check if all gates have a right number of children.
  std::string bad_gates = RiskAnalysis::CheckAllGates();
  if (bad_gates != "") {
    std::stringstream msg;
    msg << "\nThere are problems with the following gates:\n";
    msg << bad_gates;
    throw scram::ValidationError(msg.str());
  }

  /// @todo This should give a warning or error depending on the user's
  /// choice.
  if (prob_requested_) {
    // Check if all primary events have probabilities initialized.
    std::string uninit_primaries = RiskAnalysis::PrimariesNoProb();
    if (uninit_primaries != "") {
      std::stringstream msg;
      msg << "Missing probabilities for following basic events:\n";
      msg << uninit_primaries;
      throw scram::ValidationError(msg.str());
    }
  }
}

void RiskAnalysis::GraphingInstructions() {
  /// @todo Make this exception safe with a smart pointer.
  Grapher* gr = new Grapher();
  gr->GraphFaultTree(fault_tree_, orig_ids_, prob_requested_, input_file_);
  delete gr;
}

void RiskAnalysis::Analyze() {
  fta_->Analyze(fault_tree_, prob_requested_);
}

void RiskAnalysis::Report(std::string output) {
  /// @todo Make this exception safe with a smart pointer.
  Reporter* rp = new Reporter();
  rp->ReportFta(fta_, orig_ids_, output);
  delete rp;
}

void RiskAnalysis::DefineGate(const xmlpp::Element* gate_node,
                              FaultTreePtr& ft) {
  // Only one child element is expected, which is a formulae.
  std::string orig_id = gate_node->get_attribute_value("name");
  std::string id = orig_id;
  boost::to_lower(id);
  orig_ids_.insert(std::make_pair(id, orig_id));

  xmlpp::NodeSet gates = gate_node->find("./*[name() != 'attribute' and name() != 'label']");
  // Assumes that there are no attributes and labels.
  assert(gates.size() == 1);
  // Check if the gate type is supported.
  const xmlpp::Node* gate_type = gates.front();
  std::string type = gate_type->get_name();
  boost::to_lower(type);
  if (!gate_types_.count(type)) {
    std::stringstream msg;
    msg << "Line " << gate_type->get_line() << ":\n";
    msg << "Invalid input arguments. '" << orig_ids_[id]
        << "' gate formulae is not supported.";
    throw scram::ValidationError(msg.str());
  }

  if (type == "atleast") {
    const xmlpp::Element* gate =
        dynamic_cast<const xmlpp::Element*>(gate_type);
    try {
      vote_number_ = boost::lexical_cast<int>(
          gate->get_attribute_value("min"));
    } catch (boost::bad_lexical_cast& err) {
      std::stringstream msg;
      msg << "Line " << gate_type->get_line() << " :\n "
          << "Incorrect Vote number or Min for Atleast Gate input.";
      throw scram::ValidationError(msg.str());
    }
  }


  /// @todo This should take private/public roles into account.
  if (gates_.count(id)) {
    // Doubly defined gate.
    std::stringstream msg;
    msg << "Line " << gate_node->get_line() << ":\n";
    msg << orig_ids_[id] << " gate is doubly defined.";
    throw scram::ValidationError(msg.str());
  }

  // Detect name clashes.
  if (primary_events_.count(id) || tbd_basic_events_.count(id) ||
      tbd_house_events_.count(id)) {
    std::stringstream msg;
    msg << "Line " << gate_node->get_line() << ":\n";
    msg << "The id " << orig_ids_[id]
        << " is already assigned to a primary event.";
    throw scram::ValidationError(msg.str());
  }

  GatePtr i_event(new scram::Gate(id));

  // Update to be defined events.
  if (tbd_gates_.count(id)) {
    i_event = tbd_gates_[id];
    tbd_gates_.erase(id);
  } else if (tbd_events_.count(id)) {
    std::vector<GatePtr>::iterator it;
    for (it = tbd_events_[id].begin(); it != tbd_events_[id].end(); ++it) {
      (*it)->AddChild(i_event);
    }
    tbd_events_.erase(id);
  }

  assert(!gates_.count(id));
  gates_.insert(std::make_pair(id, i_event));

  assert(!all_events_.count(id));
  all_events_.insert(std::make_pair(id, i_event));

  fault_tree_->AddGate(i_event);

  i_event->type(type);  // Setting the gate type.
  if (type == "atleast") i_event->vote_number(vote_number_);


  // Process children of this gate.
  xmlpp::Node::NodeList events = gate_type->get_children();
  xmlpp::Node::NodeList::iterator it;
  for (it = events.begin(); it != events.end(); ++it) {
    const xmlpp::Element* event = dynamic_cast<const xmlpp::Element*>(*it);
    if (!event) continue;  // Ignore non-elements.
    std::string orig_id = event->get_attribute_value("name");
    std::string id = orig_id;
    boost::to_lower(id);
    orig_ids_.insert(std::make_pair(id, orig_id));

    std::string name = event->get_name();
    assert(name == "event" || name == "gate" || name == "basic-event" ||
           name == "house-event");
    // This is for a case "<event name="id" type="type"/>".
    std::string type = event->get_attribute_value("type");
    if (type != "") {
      assert(type == "gate" || type == "basic-event" ||
             type == "house-event");
      name = type;  // Event type is defined.
    }

    EventPtr child(new Event(id));
    bool save_for_later = false;  // In case event type is unknown.
    if (name == "event") {  // Undefined type yet.
      // Check if this event is already defined earlier.
      if (primary_events_.count(id)) {
        child = primary_events_[id];
      } else if (gates_.count(id)) {
        child = gates_[id];
      } else if (tbd_gates_.count(id)) {
        child = tbd_gates_[id];
      } else if (tbd_basic_events_.count(id)) {
        child = tbd_basic_events_[id];
      } else if (tbd_house_events_.count(id)) {
        child = tbd_house_events_[id];
      } else {
        tbd_events_[id].push_back(i_event);
        save_for_later = true;
      }
    } else if (name == "gate") {
      // Detect name clashes.
      // @todo Provide line numbers of the repeated event.
      if (primary_events_.count(id) || tbd_basic_events_.count(id) ||
          tbd_house_events_.count(id)) {
        std::stringstream msg;
        msg << "Line " << event->get_line() << ":\n";
        msg << "The id " << orig_ids_[id]
            << " is already assigned to a primary event.";
        throw scram::ValidationError(msg.str());
      }
      if (gates_.count(id)) {
        child = gates_[id];
      } else if (tbd_gates_.count(id)) {
        child = tbd_gates_[id];
      } else {
        child = GatePtr(new Gate(id));
        tbd_gates_.insert(
            std::make_pair(id,
                           boost::dynamic_pointer_cast <scram::Gate>(child)));
        if (tbd_events_.count(id)) {
          std::vector<GatePtr>::iterator it;
          for (it = tbd_events_[id].begin(); it != tbd_events_[id].end();
               ++it) {
            (*it)->AddChild(child);
          }
          tbd_events_.erase(id);
        }
      }

    } else if (name == "basic-event") {
      // Detect name clashes.
      if (gates_.count(id) || tbd_gates_.count(id)) {
        std::stringstream msg;
        msg << "Line " << event->get_line() << ":\n";
        msg << "The id " << orig_ids_[id]
            << " is already assigned to a gate.";
        throw scram::ValidationError(msg.str());
      }
      if (primary_events_.count(id)) {
        child = primary_events_[id];
        if (boost::dynamic_pointer_cast<scram::BasicEvent>(child) == 0) {
          std::stringstream msg;
          msg << "Line " << event->get_line() << ":\n";
          msg << "The id " << orig_ids_[id]
              << " is already assigned to a house event.";
          throw scram::ValidationError(msg.str());
        }
      }
      if (tbd_basic_events_.count(id)) {
        child = tbd_basic_events_[id];
      } else {
        child = BasicEventPtr(new BasicEvent(id));
        tbd_basic_events_.insert(
            std::make_pair(id, boost::dynamic_pointer_cast
                                  <scram::BasicEvent>(child)));
        if (tbd_events_.count(id)) {
          std::vector<GatePtr>::iterator it;
          for (it = tbd_events_[id].begin(); it != tbd_events_[id].end();
               ++it) {
            (*it)->AddChild(child);
          }
          tbd_events_.erase(id);
        }
      }

    } else if (name == "house-event") {
      // Detect name clashes.
      if (gates_.count(id) || tbd_gates_.count(id)) {
        std::stringstream msg;
        msg << "Line " << event->get_line() << ":\n";
        msg << "The id " << orig_ids_[id]
            << " is already assigned to a gate.";
        throw scram::ValidationError(msg.str());
      }
      if (primary_events_.count(id)) {
        child = primary_events_[id];
        if (boost::dynamic_pointer_cast<scram::HouseEvent>(child) == 0) {
          std::stringstream msg;
          msg << "Line " << event->get_line() << ":\n";
          msg << "The id " << orig_ids_[id]
              << " is already assigned to a basic event.";
          throw scram::ValidationError(msg.str());
        }
      }
      if (tbd_house_events_.count(id)) {
        child = tbd_house_events_[id];
      } else {
        child = HouseEventPtr(new HouseEvent(id));
        tbd_house_events_.insert(
            std::make_pair(id, boost::dynamic_pointer_cast
                                  <scram::HouseEvent>(child)));
        if (tbd_events_.count(id)) {
          std::vector<GatePtr>::iterator it;
          for (it = tbd_events_[id].begin(); it != tbd_events_[id].end();
               ++it) {
            (*it)->AddChild(child);
          }
          tbd_events_.erase(id);
        }
      }
    }

    if (!save_for_later) {  // The event type is determined.
      child->AddParent(i_event);
      i_event->AddChild(child);
    }
  }
}

void RiskAnalysis::DefineBasicEvent(const xmlpp::Element* event_node,
                                    FaultTreePtr& ft) {
  std::string orig_id = event_node->get_attribute_value("name");
  std::string id = orig_id;
  boost::to_lower(id);
  orig_ids_.insert(std::make_pair(id, orig_id));
  // Detect name clashes.
  if (gates_.count(id) || tbd_gates_.count(id)) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << "The id " << orig_ids_[id]
        << " is already assigned to a gate.";
    throw scram::ValidationError(msg.str());
  }
  if (primary_events_.count(id)) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << "The id " << orig_ids_[id]
        << " is doubly defined.";
    throw scram::ValidationError(msg.str());
  }
  if (tbd_house_events_.count(id)) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << "The id " << orig_ids_[id]
        << " is already used by a house event.";
    throw scram::ValidationError(msg.str());
  }
  /// @todo Expression class to analyze more complex probabilities.
  /// only float for now.
  /// @todo Create get_element(name) method for elements.
  double p = 0;
  xmlpp::Node::NodeList children = event_node->get_children();
  xmlpp::Node::NodeList::iterator it;
  for (it = children.begin(); it != children.end(); ++it) {
    const xmlpp::Element* sub_el = dynamic_cast<const xmlpp::Element*>(*it);
    if (!sub_el) continue;  // Ignore non-elements.
    if (sub_el->get_name() == "float") break;
  }
  const xmlpp::Element* float_prob = dynamic_cast<const xmlpp::Element*>(*it);
  if (!float_prob) assert(false);

  try {
    p = boost::lexical_cast<double>(
        float_prob->get_attribute_value("value"));
  } catch (boost::bad_lexical_cast& err) {
    std::stringstream msg;
    msg << "Line " << float_prob->get_line() << " :\n "
        << "Incorrect probability input.";
    throw scram::ValidationError(msg.str());
  }

  if (tbd_basic_events_.count(id)) {
    tbd_basic_events_[id]->p(p);
    primary_events_.insert(std::make_pair(id, tbd_basic_events_[id]));
    all_events_.insert(std::make_pair(id, tbd_basic_events_[id]));
    tbd_basic_events_.erase(id);

  } else {
    BasicEventPtr child(new BasicEvent(id));
    child->p(p);
    primary_events_.insert(std::make_pair(id, child));
    all_events_.insert(std::make_pair(id, child));
    if (tbd_events_.count(id)) {
      std::vector<GatePtr>::iterator it;
      for (it = tbd_events_[id].begin(); it != tbd_events_[id].end();
           ++it) {
        (*it)->AddChild(child);
      }
      tbd_events_.erase(id);
    }
  }
}

void RiskAnalysis::DefineHouseEvent(const xmlpp::Element* event_node,
                                    FaultTreePtr& ft) {
  std::string orig_id = event_node->get_attribute_value("name");
  std::string id = orig_id;
  boost::to_lower(id);
  orig_ids_.insert(std::make_pair(id, orig_id));
  // Detect name clashes.
  if (gates_.count(id) || tbd_gates_.count(id)) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << "The id " << orig_ids_[id]
        << " is already assigned to a gate.";
    throw scram::ValidationError(msg.str());
  }
  if (primary_events_.count(id)) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << "The id " << orig_ids_[id]
        << " is doubly defined.";
    throw scram::ValidationError(msg.str());
  }
  if (tbd_basic_events_.count(id)) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << "The id " << orig_ids_[id]
        << " is already used by a basic event.";
    throw scram::ValidationError(msg.str());
  }
  // Only boolean for now.
  xmlpp::Node::NodeList children = event_node->get_children();
  xmlpp::Node::NodeList::iterator it;
  for (it = children.begin(); it != children.end(); ++it) {
    const xmlpp::Element* sub_el = dynamic_cast<const xmlpp::Element*>(*it);
    if (!sub_el) continue;  // Ignore non-elements.
    if (sub_el->get_name() == "constant") break;
  }
  const xmlpp::Element* b_const = dynamic_cast<const xmlpp::Element*>(*it);
  if (!b_const) assert(false);

  std::string val = b_const->get_attribute_value("value");
  boost::to_lower(val);

  int p = 0;
  if (val == "true") p = 1;

  if (tbd_house_events_.count(id)) {
    tbd_house_events_[id]->p(p);
    primary_events_.insert(std::make_pair(id, tbd_house_events_[id]));
    all_events_.insert(std::make_pair(id, tbd_house_events_[id]));
    tbd_house_events_.erase(id);

  } else {
    HouseEventPtr child(new HouseEvent(id));
    child->p(p);
    primary_events_.insert(std::make_pair(id, child));
    all_events_.insert(std::make_pair(id, child));
    if (tbd_events_.count(id)) {
      std::vector<GatePtr>::iterator it;
      for (it = tbd_events_[id].begin(); it != tbd_events_[id].end();
           ++it) {
        (*it)->AddChild(child);
      }
      tbd_events_.erase(id);
    }
  }
}

void RiskAnalysis::DefineFaultTree(const xmlpp::Element* ft_node) {
  fault_tree_ = FaultTreePtr(new FaultTree(ft_node->get_attribute_value("name")));
  xmlpp::Node::NodeList children = ft_node->get_children();
  xmlpp::Node::NodeList::iterator it;
  for (it = children.begin(); it != children.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);

    if (!element) continue;  // Ignore non-elements.
    std::string name = element->get_name();
    // Design by contract.
    assert(name == "define-gate" || name == "define-basic-event" ||
           name == "define-house-event");

    if (!prob_requested_ &&
        (name == "define-basic-event" || name == "define-house-event")) {
      prob_requested_ = true;
    }

    /// @todo Use function pointers.
    if (name == "define-gate") {
      // Define and add gate here.
      RiskAnalysis::DefineGate(element, fault_tree_);
    } else if (name == "define-basic-event") {
      RiskAnalysis::DefineBasicEvent(element, fault_tree_);
    } else if (name == "define-house-event") {
      RiskAnalysis::DefineHouseEvent(element, fault_tree_);
    }
  }
}

void RiskAnalysis::ProcessModelData(const xmlpp::Element* model_data) {
  prob_requested_ = true;

  xmlpp::Node::NodeList children = model_data->get_children();
  xmlpp::Node::NodeList::iterator it;
  for (it = children.begin(); it != children.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);

    if (!element) continue;  // Ignore non-elements.
    std::string name = element->get_name();
    if (name == "define-basic-event") {
      RiskAnalysis::DefineBasicEvent(element, fault_tree_);
    } else if (name == "define-house-event") {
      RiskAnalysis::DefineHouseEvent(element, fault_tree_);
    }
  }
}

std::string RiskAnalysis::CheckAllGates() {
  std::stringstream msg;
  msg << "";  // An empty default message is the indicator of no problems.

  boost::unordered_map<std::string, GatePtr>::iterator it;
  for (it = gates_.begin(); it != gates_.end(); ++it) {
    msg << RiskAnalysis::CheckGate(it->second);
  }

  return msg.str();
}

std::string RiskAnalysis::CheckGate(const GatePtr& event) {
  std::stringstream msg;
  msg << "";  // An empty default message is the indicator of no problems.
  try {
    std::string gate = event->type();
    // This line throws an error if there are no children.
    int size = event->children().size();

    // Gate dependent logic.
    if (gate == "and" || gate == "or" || gate == "nor" || gate == "nand") {
      if (size < 2) {
        boost::to_upper(gate);
        msg << orig_ids_[event->id()] << " : " << gate
            << " gate must have 2 or more "
            << "children.\n";
      }
    } else if (gate == "xor") {
      if (size != 2) {
        boost::to_upper(gate);
        msg << orig_ids_[event->id()] << " : " << gate
            << " gate must have exactly 2 children.\n";
      }
    } else if (gate == "inhibit") {
      if (size != 2) {
        boost::to_upper(gate);
        msg << orig_ids_[event->id()] << " : " << gate
            << " gate must have exactly 2 children.\n";
      } else {
        bool conditional_found = false;
        std::map<std::string, EventPtr> children = event->children();
        std::map<std::string, EventPtr>::iterator it;
        for (it = children.begin(); it != children.end(); ++it) {
          if (primary_events_.count(it->first)) {
            std::string type = primary_events_[it->first]->type();
            if (type == "conditional") {
              if (!conditional_found) {
                conditional_found = true;
              } else {
                boost::to_upper(gate);
                msg << orig_ids_[event->id()] << " : " << gate
                    << " gate must have exactly one conditional event.\n";
              }
            }
          }
        }
        if (!conditional_found) {
          boost::to_upper(gate);
          msg << orig_ids_[event->id()] << " : " << gate
              << " gate is missing a conditional event.\n";
        }
      }
    } else if (gate == "not" || gate == "null") {
      if (size != 1) {
        boost::to_upper(gate);
        msg << orig_ids_[event->id()] << " : " << gate
            << " gate must have exactly one child.";
      }
    } else if (gate == "vote" || gate == "atleast") {
      if (size <= event->vote_number()) {
        boost::to_upper(gate);
        msg << orig_ids_[event->id()] << " : " << gate
            << " gate must have more children that its vote number "
            << event->vote_number() << ".";
      }
    } else {
      boost::to_upper(gate);
      msg << orig_ids_[event->id()] << " : Gate Check failure. No check for "
          << gate << " gate.";
    }
  } catch (scram::ValueError& err) {
    msg << orig_ids_[event->id()] << " : No children detected.";
  }

  return msg.str();
}

std::string RiskAnalysis::PrimariesNoProb() {
  std::string uninit_primaries = "";
  boost::unordered_map<std::string, PrimaryEventPtr>::iterator it;
  for (it = primary_events_.begin(); it != primary_events_.end(); ++it) {
    try {
      it->second->p();
    } catch (scram::ValueError& err) {
      uninit_primaries += orig_ids_[it->first];
      uninit_primaries += "\n";
    }
  }

  return uninit_primaries;
}

}  // namespace scram
