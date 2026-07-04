#include "mdns_browse.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace droppix {

namespace {

std::vector<std::string> split(const std::string& line, char delim) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream iss(line);
  while (std::getline(iss, field, delim)) {
    fields.push_back(field);
  }
  return fields;
}

// Extract the value of the `id=` TXT record from an avahi parseable txt field,
// which looks like: "id=abc-123" "other=x"  (each record double-quoted).
std::string parse_txt_id(const std::string& txt) {
  // Anchor to the quoted record boundary so a future attribute whose key
  // merely ends in "id" (e.g. "uuid=x") can't be matched instead.
  const std::string key = "\"id=";
  auto pos = txt.find(key);
  if (pos == std::string::npos) return "";
  pos += key.size();
  std::string val;
  for (; pos < txt.size() && txt[pos] != '"' && txt[pos] != ' '; ++pos) val.push_back(txt[pos]);
  return val;
}

}  // namespace

std::vector<MdnsDevice> parse_avahi_browse(const std::string& text) {
  std::vector<MdnsDevice> devices;

  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty() || line[0] != '=') continue;

    std::vector<std::string> fields = split(line, ';');
    // =;iface;proto;name;type;domain;hostname;address;port;txt
    if (fields.size() < 9) continue;
    if (fields[2] != "IPv4") continue;

    MdnsDevice dev;
    dev.name = fields[3];
    dev.address = fields[7];
    dev.port = static_cast<uint16_t>(std::atoi(fields[8].c_str()));
    if (fields.size() >= 10) dev.id = parse_txt_id(fields[9]);

    // Dedup by name: last occurrence wins.
    auto it = std::find_if(devices.begin(), devices.end(),
                            [&](const MdnsDevice& d) { return d.name == dev.name; });
    if (it != devices.end()) {
      *it = dev;
    } else {
      devices.push_back(dev);
    }
  }

  return devices;
}

}  // namespace droppix
