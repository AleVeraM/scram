/*
 * Copyright (C) 2014-2018 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// @file
/// Implementation of fault tree and component containers.

#include "fault_tree.h"

#include "error.h"

namespace scram::mef {

Component::Component(std::string name, std::string base_path,
                     RoleSpecifier role)
    : Element(std::move(name)), Role(role, std::move(base_path)) {}

void Component::Add(Gate* gate) {
  CheckDuplicateEvent(*gate);
  gates_.insert(gate);
}

void Component::Add(BasicEvent* basic_event) {
  CheckDuplicateEvent(*basic_event);
  basic_events_.insert(basic_event);
}

void Component::Add(HouseEvent* house_event) {
  CheckDuplicateEvent(*house_event);
  house_events_.insert(house_event);
}

void Component::Add(CcfGroup* ccf_group) {
  if (ccf_groups_.count(ccf_group->name())) {
    SCRAM_THROW(DuplicateElementError())
        << errinfo_element(ccf_group->name(), "CCF group");
  }
  for (BasicEvent* member : ccf_group->members()) {
    const std::string& name = member->name();
    if (gates_.count(name) || basic_events_.count(name) ||
        house_events_.count(name)) {
      SCRAM_THROW(DuplicateElementError()) << errinfo_element(name, "event");
    }
  }
  for (const auto& member : ccf_group->members())
    basic_events_.insert(member);
  ccf_groups_.insert(ccf_group);
}

namespace {

/// Helper function to remove events from component containers.
template <class T>
void RemoveEvent(T* event, ElementTable<T*>* table) {
  auto it = table->find(event->name());
  if (it == table->end())
    SCRAM_THROW(
        UndefinedElement("Event " + event->id() + " is not in the component."));
  if (*it != event)
    SCRAM_THROW(UndefinedElement("Duplicate event " + event->id() +
                                 " does not belong to the component."));
  table->erase(it);
}

}  // namespace

void Component::Remove(HouseEvent* element) {
  return RemoveEvent(element, &house_events_);
}

void Component::Remove(BasicEvent* element) {
  return RemoveEvent(element, &basic_events_);
}

void Component::Remove(Gate* element) { return RemoveEvent(element, &gates_); }

void Component::GatherGates(std::unordered_set<Gate*>* gates) {
  gates->insert(gates_.begin(), gates_.end());
  for (const ComponentPtr& component : components())
    component->GatherGates(gates);
}

void Component::CheckDuplicateEvent(const Event& event) {
  const std::string& name = event.name();
  if (gates_.count(name) || basic_events_.count(name) ||
      house_events_.count(name)) {
    SCRAM_THROW(DuplicateElementError()) << errinfo_element(name, "event");
  }
}

FaultTree::FaultTree(const std::string& name) : Component(name) {}

void FaultTree::CollectTopEvents() {
  top_events_.clear();
  std::unordered_set<Gate*> gates;
  Component::GatherGates(&gates);
  // Detects top events.
  for (Gate* gate : gates)
    MarkNonTopGates(gate, gates);

  for (Gate* gate : gates) {
    if (gate->mark()) {  // Not a top event.
      gate->mark(NodeMark::kClear);  // Cleaning up.
    } else {
      top_events_.push_back(gate);
    }
  }
}

void FaultTree::MarkNonTopGates(Gate* gate,
                                const std::unordered_set<Gate*>& gates) {
  if (gate->mark())
    return;
  for (const Formula::Arg& arg : gate->formula().args()) {
    if (Gate* const* arg_gate = std::get_if<Gate*>(&arg.event)) {
      if (gates.count(*arg_gate)) {
        MarkNonTopGates(*arg_gate, gates);
        (*arg_gate)->mark(NodeMark::kPermanent);  // Any non clear mark.
      }
    }
  }
}

}  // namespace scram::mef
