/// @file grapher.cc
/// Implements Grapher.
#include "grapher.h"

#include <boost/algorithm/string.hpp>
#include <boost/assign.hpp>

#include "fault_tree_analysis.h"

namespace scram {

std::map<std::string, std::string> Grapher::gate_colors_ =
    boost::assign::map_list_of ("or", "blue") ("and", "green") ("not", "red")
                               ("xor", "brown") ("inhibit", "yellow")
                               ("atleast", "cyan") ("null", "gray")
                               ("nor", "magenta") ("nand", "orange");

std::map<std::string, std::string> Grapher::event_colors_ =
    boost::assign::map_list_of ("basic", "black") ("undeveloped", "blue")
                               ("house", "green") ("conditional", "red");

void Grapher::GraphFaultTree(const GatePtr& top_event, bool prob_requested,
                             std::ostream& out) {
  // The structure of the output:
  // List gates with their children.
  // List common intermediate events as transfer symbols.
  // List gates and primary events' descriptions.

  out << "digraph " << top_event->name() << " {\n";

  FaultTreeAnalysis* fta = new FaultTreeAnalysis(top_event);

  // Keep track of number of repetitions of nodes.
  // These repetitions are needed so that the graph links to separate nodes
  // with the same display name.
  boost::unordered_map<EventPtr, int> node_repeat;

  // Populate intermediate and primary events of the top.
  Grapher::GraphGate(fta->top_event(), &node_repeat, out);
  // Do the same for all intermediate events.
  boost::unordered_map<std::string, GatePtr>::const_iterator it_inter;
  for (it_inter = fta->inter_events().begin();
       it_inter != fta->inter_events().end(); ++it_inter) {
    Grapher::GraphGate(it_inter->second, &node_repeat, out);
  }

  // Format events.
  Grapher::FormatTopEvent(fta->top_event(), out);
  Grapher::FormatIntermediateEvents(fta->inter_events(), node_repeat, out);
  Grapher::FormatBasicEvents(fta->basic_events(), node_repeat, prob_requested,
                             out);
  Grapher::FormatHouseEvents(fta->house_events(), node_repeat, prob_requested,
                             out);
  out << "}\n";
  delete fta;
}

void Grapher::GraphGate(const GatePtr& gate,
                        boost::unordered_map<EventPtr, int>* node_repeat,
                        std::ostream& out) {
  // Populate intermediate and primary events of the input gate.
  const std::map<std::string, EventPtr>* events =
      &gate->formula()->event_args();
  std::map<std::string, EventPtr>::const_iterator it_child;
  for (it_child = events->begin(); it_child != events->end(); ++it_child) {
    if (node_repeat->count(it_child->second)) {
      node_repeat->find(it_child->second)->second++;
    } else {
      node_repeat->insert(std::make_pair(it_child->second, 0));
    }
    out << "\"" << gate->name() << "_R0\" -> "
        << "\"" << it_child->second->name() <<"_R"
        << node_repeat->find(it_child->second)->second << "\";\n";
  }
}

void Grapher::FormatTopEvent(const GatePtr& top_event, std::ostream& out) {
  std::string gate = top_event->formula()->type();

  // Special case for inhibit gate.
  if (gate == "and" && top_event->HasAttribute("flavor"))
    gate = top_event->GetAttribute("flavor").value;

  std::string gate_color = "black";
  if (gate_colors_.count(gate)) {
    gate_color = gate_colors_.find(gate)->second;
  }

  boost::to_upper(gate);
  out << "\"" <<  top_event->name()
      << "_R0\" [shape=ellipse, "
      << "fontsize=12, fontcolor=black, fontname=\"times-bold\", "
      << "color=" << gate_color << ", "
      << "label=\"" << top_event->name() << "\\n"
      << "{ " << gate;
  if (gate == "ATLEAST") {
    out << " " << top_event->formula()->vote_number()
        << "/" << top_event->formula()->num_args();
  }
  out << " }\"]\n";
}

void Grapher::FormatIntermediateEvents(
    const boost::unordered_map<std::string, GatePtr>& inter_events,
    const boost::unordered_map<EventPtr, int>& node_repeat,
    std::ostream& out) {
  boost::unordered_map<std::string, GatePtr>::const_iterator it;
  for (it = inter_events.begin(); it != inter_events.end(); ++it) {
    std::string gate = it->second->formula()->type();

    if (it->second->HasAttribute("flavor") && gate == "and")
      gate = it->second->GetAttribute("flavor").value;

    std::string gate_color = gate_colors_.find(gate)->second;
    boost::to_upper(gate);  // This is for graphing.
    std::string name = it->second->name();
    int repetition = node_repeat.find(it->second)->second;
    for (int i = 0; i <= repetition; ++i) {
        out << "\"" << name << "_R" << i << "\"";
      if (i == 0) {
        out << " [shape=box, ";
      } else {
        // Repetition is a transfer symbol.
        out << " [shape=triangle, ";
      }
      out << "fontsize=10, fontcolor=black, "
          << "color=" << gate_color << ", "
          << "label=\"" << name << "\\n"  // This is a new line in the label.
          << "{ " << gate;
      if (gate == "ATLEAST") {
        out << " " << it->second->formula()->vote_number()
            << "/" << it->second->formula()->num_args();
      }
      out << " }\"]\n";
    }
  }
}

void Grapher::FormatBasicEvents(
    const boost::unordered_map<std::string, BasicEventPtr>& basic_events,
    const boost::unordered_map<EventPtr, int>& node_repeat,
    bool prob_requested,
    std::ostream& out) {
  boost::unordered_map<std::string, BasicEventPtr>::const_iterator it;
  for (it = basic_events.begin(); it != basic_events.end(); ++it) {
    std::string prob_msg = "";
    if (prob_requested) {
      std::stringstream snippet;
      snippet << it->second->p();
      prob_msg = "\\n";
      prob_msg += snippet.str();
    }
    int repetition = node_repeat.find(it->second)->second;
    Grapher::FormatPrimaryEvent(it->second, repetition, prob_msg, out);
  }
}

void Grapher::FormatHouseEvents(
    const boost::unordered_map<std::string, HouseEventPtr>& house_events,
    const boost::unordered_map<EventPtr, int>& node_repeat,
    bool prob_requested,
    std::ostream& out) {
  boost::unordered_map<std::string, HouseEventPtr>::const_iterator it;
  for (it = house_events.begin(); it != house_events.end(); ++it) {
    std::string prob_msg = "";
    if (prob_requested) {
      prob_msg = "\\n";
      prob_msg += it->second->state() ? "True" : "False";
    }
    int repetition = node_repeat.find(it->second)->second;
    Grapher::FormatPrimaryEvent(it->second, repetition, prob_msg, out);
  }
}

void Grapher::FormatPrimaryEvent(const PrimaryEventPtr& primary_event,
                                 int repetition,
                                 std::string prob_msg,
                                 std::ostream& out) {
  std::string type = primary_event->type();
  // Detect undeveloped or conditional event.
  if (type == "basic" && primary_event->HasAttribute("flavor")) {
    type = primary_event->GetAttribute("flavor").value;
  }
  std::string color = event_colors_.find(type)->second;
  for (int i = 0; i <= repetition; ++i) {
    out << "\"" << primary_event->name() << "_R" << i
        << "\" [shape=circle, "
        << "height=1, fontsize=10, fixedsize=true, "
        << "fontcolor=" << color
        << ", " << "label=\"" << primary_event->name() << "\\n["
        << type << "]" << prob_msg << "\"]\n";
  }
}

}  // namespace scram
