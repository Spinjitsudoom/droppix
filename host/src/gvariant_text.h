#pragma once
#include <string>
#include <utility>
#include <vector>

namespace droppix {

// A value parsed from GVariant's text form (what `gdbus call` prints). Only the shapes
// droppix reads are modeled: tuples, arrays, dicts, variants, strings, numbers, booleans.
// Type annotations ("uint32 ", "@a{sv} ") are accepted and dropped; numbers are doubles.
struct GVal {
  enum Kind { Tuple, Array, Dict, Variant, Str, Num, Bool };
  Kind kind = Tuple;
  std::string str;
  double num = 0;
  bool b = false;
  std::vector<GVal> items;                        // Tuple / Array elements; Variant: its one value
  std::vector<std::pair<GVal, GVal>> entries;     // Dict

  // Dict lookup by string key; a Variant value is returned as the Variant (see items[0]).
  const GVal* get(const std::string& key) const;
};

// PURE (unit-tested). Parses one complete value; false on malformed or truncated text.
bool parse_gvariant_text(const std::string& text, GVal& out);

}  // namespace droppix
