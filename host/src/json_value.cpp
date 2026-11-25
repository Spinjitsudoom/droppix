#include "json_value.h"
#include <cctype>
#include <cstdlib>

namespace droppix {

const JVal* JVal::get(const std::string& key) const {
  if (kind != Object) return nullptr;
  for (const auto& f : fields) if (f.first == key) return &f.second;
  return nullptr;
}
std::string JVal::get_str(const std::string& key) const {
  const JVal* v = get(key);
  return v && v->kind == Str ? v->str : std::string{};
}
double JVal::get_num(const std::string& key, double dflt) const {
  const JVal* v = get(key);
  return v && v->kind == Num ? v->num : dflt;
}
bool JVal::get_bool(const std::string& key, bool dflt) const {
  const JVal* v = get(key);
  return v && v->kind == Bool ? v->b : dflt;
}

namespace {

void put_utf8(std::string& s, unsigned cp) {
  if (cp < 0x80) s.push_back(static_cast<char>(cp));
  else if (cp < 0x800) { s.push_back(static_cast<char>(0xC0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
  else { s.push_back(static_cast<char>(0xE0 | (cp >> 12))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
         s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
}

struct P {
  const std::string& t;
  size_t i = 0;
  int depth = 0;

  void ws() { while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i; }
  bool lit(const char* w) {
    const std::string s(w);
    if (t.compare(i, s.size(), s) != 0) return false;
    i += s.size();
    return true;
  }
  bool string(std::string& out) {
    if (i >= t.size() || t[i] != '"') return false;
    ++i;
    while (i < t.size() && t[i] != '"') {
      char c = t[i++];
      if (c != '\\') { out.push_back(c); continue; }
      if (i >= t.size()) return false;
      c = t[i++];
      switch (c) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'u': {
          if (i + 4 > t.size()) return false;
          const std::string hex = t.substr(i, 4);
          char* end = nullptr;
          const unsigned cp = static_cast<unsigned>(std::strtoul(hex.c_str(), &end, 16));
          if (end != hex.c_str() + 4) return false;
          i += 4;
          put_utf8(out, cp);
          break;
        }
        default: out.push_back(c);   // \" \\ \/
      }
    }
    if (i >= t.size()) return false;
    ++i;
    return true;
  }
  bool value(JVal& v) {
    if (++depth > 64) return false;
    ws();
    if (i >= t.size()) return false;
    const char c = t[i];
    bool ok = false;
    if (c == '{') {
      ++i; v.kind = JVal::Object; ws();
      if (i < t.size() && t[i] == '}') { ++i; ok = true; }
      else for (;;) {
        ws();
        std::string k;
        JVal val;
        if (!string(k)) break;
        ws();
        if (i >= t.size() || t[i++] != ':') break;
        if (!value(val)) break;
        v.fields.emplace_back(std::move(k), std::move(val));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == '}') { ++i; ok = true; }
        break;
      }
    } else if (c == '[') {
      ++i; v.kind = JVal::Array; ws();
      if (i < t.size() && t[i] == ']') { ++i; ok = true; }
      else for (;;) {
        JVal e;
        if (!value(e)) break;
        v.items.push_back(std::move(e));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == ']') { ++i; ok = true; }
        break;
      }
    } else if (c == '"') {
      v.kind = JVal::Str; ok = string(v.str);
    } else if (lit("true")) { v.kind = JVal::Bool; v.b = true; ok = true; }
    else if (lit("false")) { v.kind = JVal::Bool; v.b = false; ok = true; }
    else if (lit("null")) { v.kind = JVal::Null; ok = true; }
    else {
      const char* start = t.c_str() + i;
      char* end = nullptr;
      v.num = std::strtod(start, &end);
      if (end != start) { v.kind = JVal::Num; i += static_cast<size_t>(end - start); ok = true; }
    }
    --depth;
    return ok;
  }
};

}  // namespace

bool parse_json(const std::string& text, JVal& out) {
  P p{text};
  JVal v;
  if (!p.value(v)) return false;
  p.ws();
  if (p.i != text.size()) return false;
  out = std::move(v);
  return true;
}

}  // namespace droppix
