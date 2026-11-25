#include "gvariant_text.h"
#include <cctype>
#include <cstdlib>

namespace droppix {

const GVal* GVal::get(const std::string& key) const {
  for (const auto& e : entries)
    if (e.first.kind == Str && e.first.str == key) return &e.second;
  return nullptr;
}

namespace {

struct Parser {
  const std::string& t;
  size_t i = 0;

  void ws() { while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i; }
  bool eat(char c) { ws(); if (i < t.size() && t[i] == c) { ++i; return true; } return false; }

  // "@<type> " annotation: a type string runs to the next space.
  void skip_annotation() {
    ws();
    if (i < t.size() && t[i] == '@') {
      while (i < t.size() && !std::isspace(static_cast<unsigned char>(t[i]))) ++i;
    }
  }

  // Bare type-name prefixes gdbus prints before numbers/strings ("uint32 7", "objectpath '/x'").
  void skip_type_word() {
    static const char* kWords[] = {"byte", "int16", "uint16", "int32", "uint32", "int64",
                                   "uint64", "double", "handle", "boolean", "string",
                                   "objectpath", "signature"};
    ws();
    for (const char* w : kWords) {
      const std::string s(w);
      if (t.compare(i, s.size(), s) == 0 && i + s.size() < t.size() &&
          std::isspace(static_cast<unsigned char>(t[i + s.size()]))) {
        i += s.size();
        ws();
        return;
      }
    }
  }

  bool string(GVal& v) {
    const char q = t[i++];
    v.kind = GVal::Str;
    while (i < t.size() && t[i] != q) {
      char c = t[i++];
      if (c == '\\') {
        if (i >= t.size()) return false;
        c = t[i++];
        if (c == 'n') c = '\n'; else if (c == 't') c = '\t';
      }
      v.str.push_back(c);
    }
    if (i >= t.size()) return false;
    ++i;
    return true;
  }

  bool number(GVal& v) {
    const char* start = t.c_str() + i;
    char* end = nullptr;
    if (t.compare(i, 2, "0x") == 0) v.num = static_cast<double>(std::strtoull(start, &end, 16));
    else v.num = std::strtod(start, &end);
    if (end == start) return false;
    i += static_cast<size_t>(end - start);
    v.kind = GVal::Num;
    return true;
  }

  // Elements up to `close`, comma-separated; a lone trailing comma (1-tuples) is allowed.
  bool list(char close, std::vector<GVal>& out) {
    if (eat(close)) return true;
    for (;;) {
      GVal e;
      if (!value(e)) return false;
      out.push_back(std::move(e));
      if (eat(close)) return true;
      if (!eat(',')) return false;
      if (eat(close)) return out.size() == 1;
    }
  }

  bool value(GVal& v) {
    skip_annotation();
    skip_type_word();
    ws();
    if (i >= t.size()) return false;
    const char c = t[i];
    if (c == '(') { ++i; v.kind = GVal::Tuple; return list(')', v.items); }
    if (c == '[') { ++i; v.kind = GVal::Array; return list(']', v.items); }
    if (c == '<') {
      ++i; v.kind = GVal::Variant;
      GVal inner;
      if (!value(inner) || !eat('>')) return false;
      v.items.push_back(std::move(inner));
      return true;
    }
    if (c == '{') {
      ++i; v.kind = GVal::Dict;
      if (eat('}')) return true;
      for (;;) {
        GVal k, val;
        if (!value(k) || !eat(':') || !value(val)) return false;
        v.entries.emplace_back(std::move(k), std::move(val));
        if (eat('}')) return true;
        if (!eat(',')) return false;
      }
    }
    if (c == '\'' || c == '"') return string(v);
    if (t.compare(i, 4, "true") == 0) { i += 4; v.kind = GVal::Bool; v.b = true; return true; }
    if (t.compare(i, 5, "false") == 0) { i += 5; v.kind = GVal::Bool; v.b = false; return true; }
    return number(v);
  }
};

}  // namespace

bool parse_gvariant_text(const std::string& text, GVal& out) {
  Parser p{text};
  GVal v;
  if (!p.value(v)) return false;
  p.ws();
  if (p.i != text.size()) return false;
  out = std::move(v);
  return true;
}

}  // namespace droppix
