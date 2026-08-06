#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tutorial {

// The register separates current-cycle data from next-cycle data.
class ValueRegister {
 public:
  void Reset() {
    value_ = 0;
    next_value_ = 0;
    valid_ = false;
    next_valid_ = false;
  }

  int value() const { return value_; }
  bool valid() const { return valid_; }

  void WriteNext(int value) {
    next_value_ = value;
    next_valid_ = true;
  }

  void Hold() {
    next_value_ = value_;
    next_valid_ = valid_;
  }

  void Commit() {
    value_ = next_value_;
    valid_ = next_valid_;
  }

 private:
  int value_ = 0;
  int next_value_ = 0;
  bool valid_ = false;
  bool next_valid_ = false;
};

// Datapath component 1: produces 1, 2, 3, ...
class Counter {
 public:
  void Reset() { next_number_ = 1; }

  void Evaluate(bool enable, ValueRegister* output) {
    if (!enable) {
      output->Hold();
      return;
    }

    output->WriteNext(next_number_);
    ++next_number_;
  }

 private:
  int next_number_ = 1;
};

// Datapath component 2: adds values received through the register.
class Accumulator {
 public:
  void Reset() { sum_ = 0; }

  void Evaluate(bool enable, const ValueRegister& input) {
    if (enable && input.valid()) {
      sum_ += input.value();
    }
  }

  int sum() const { return sum_; }

 private:
  int sum_ = 0;
};

// Control path: this example advances only on even-numbered cycles.
class Controller {
 public:
  bool Enable(int cycle) const { return cycle % 2 == 0; }
};

enum class NodeKind {
  kDatapath,
  kRegister,
  kControl,
};

enum class EdgeKind {
  kData,
  kControl,
};

struct Node {
  std::string name;
  NodeKind kind;
};

struct Edge {
  int source;
  int destination;
  EdgeKind kind;
};

// This tiny builder only records and checks the example's coarse structure.
// The real ProcessorBuilder in pipesim.h follows the same general idea.
class ExampleBuilder {
 public:
  int AddNode(const std::string& name, NodeKind kind) {
    nodes_.push_back({name, kind});
    return static_cast<int>(nodes_.size()) - 1;
  }

  void Connect(int source, int destination, EdgeKind kind) {
    edges_.push_back({source, destination, kind});
  }

  void Build() const {
    RequireEdge("counter", "value_register", EdgeKind::kData);
    RequireEdge("value_register", "accumulator", EdgeKind::kData);
    RequireEdge("controller", "counter", EdgeKind::kControl);
    RequireEdge("controller", "value_register", EdgeKind::kControl);
    RequireEdge("controller", "accumulator", EdgeKind::kControl);
  }

  void Print() const {
    std::cout << "DATAPATH counter -> value_register -> accumulator\n";
    std::cout << "CONTROL controller -> counter\n";
    std::cout << "CONTROL controller -> value_register\n";
    std::cout << "CONTROL controller -> accumulator\n";
  }

 private:
  int FindNode(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
      if (nodes_[i].name == name) {
        return i;
      }
    }
    return -1;
  }

  void RequireEdge(const std::string& source_name,
                   const std::string& destination_name,
                   EdgeKind kind) const {
    int source = FindNode(source_name);
    int destination = FindNode(destination_name);

    for (const Edge& edge : edges_) {
      if (edge.source == source && edge.destination == destination &&
          edge.kind == kind) {
        return;
      }
    }

    throw std::runtime_error("missing required example connection");
  }

  std::vector<Node> nodes_;
  std::vector<Edge> edges_;
};

class ExampleSystem {
 public:
  void Reset() {
    cycle_ = 0;
    counter_.Reset();
    value_register_.Reset();
    accumulator_.Reset();
  }

  void Clock() {
    bool enable = controller_.Enable(cycle_);

    // Both datapath components read current state.
    accumulator_.Evaluate(enable, value_register_);
    counter_.Evaluate(enable, &value_register_);

    // New register data becomes visible only after all evaluation is done.
    value_register_.Commit();

    std::cout << "cycle " << cycle_
              << " enable " << (enable ? 1 : 0)
              << " register ";
    if (value_register_.valid()) {
      std::cout << value_register_.value();
    } else {
      std::cout << "EMPTY";
    }
    std::cout << " sum " << accumulator_.sum() << "\n";

    ++cycle_;
  }

 private:
  int cycle_ = 0;
  Counter counter_;
  ValueRegister value_register_;
  Accumulator accumulator_;
  Controller controller_;
};

ExampleSystem BuildExample() {
  ExampleBuilder builder;

  int counter = builder.AddNode("counter", NodeKind::kDatapath);
  int value_register =
      builder.AddNode("value_register", NodeKind::kRegister);
  int accumulator = builder.AddNode("accumulator", NodeKind::kDatapath);
  int controller = builder.AddNode("controller", NodeKind::kControl);

  builder.Connect(counter, value_register, EdgeKind::kData);
  builder.Connect(value_register, accumulator, EdgeKind::kData);

  builder.Connect(controller, counter, EdgeKind::kControl);
  builder.Connect(controller, value_register, EdgeKind::kControl);
  builder.Connect(controller, accumulator, EdgeKind::kControl);

  builder.Build();
  builder.Print();

  return ExampleSystem();
}

}  // namespace tutorial

int main() {
  tutorial::ExampleSystem system = tutorial::BuildExample();
  system.Reset();

  for (int i = 0; i < 8; ++i) {
    system.Clock();
  }

  return 0;
}
