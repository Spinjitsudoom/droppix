#pragma once
#include <string>
#include <utility>
#include <vector>

namespace droppix {

// A parsed JSON value (what compositor CLIs like swaymsg/hyprctl print with -r / -j).
struct JVal {
  enum Kind { Null, Bool, Num, Str, Array, Object };
  Kind kind = Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<JVal> items;                          // Array
  std::vector<std::pair<std::string, JVal>> fields; // Object (in document order)

  const JVal* get(const std::string& key) const;    // Object field, or nullptr
  // Convenience readers with defaults for absent / wrong-typed fields.
  std::string get_str(const std::string& key) const;
  double get_num(const std::string& key, double dflt = 0) const;
  bool get_bool(const std::string& key, bool dflt = false) const;
};

// PURE (unit-tested). One complete JSON document; false on malformed or trailing text.
bool parse_json(const std::string& text, JVal& out);

}  // namespace droppix
