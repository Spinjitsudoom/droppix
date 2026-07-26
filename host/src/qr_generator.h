#pragma once
#include <string>
#include <sstream>
#include <cctype>

namespace droppix {

/**
 * Generates a droppix QR code URI from connection parameters.
 * Format: droppix://host:port?code=NNNNNN
 */
inline std::string generate_qr_uri(
    const std::string& host,
    int port,
    const std::string& code
) {
    std::ostringstream oss;
    oss << "droppix://" << host << ":" << port << "?code=" << code;
    return oss.str();
}

/**
 * Validates a pairing code format (6 digits).
 */
inline bool is_valid_pairing_code(const std::string& code) {
    if (code.length() != 6) return false;
    for (char c : code) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

}  // namespace droppix
