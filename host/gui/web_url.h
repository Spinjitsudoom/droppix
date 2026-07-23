#pragma once
#include <QImage>
#include <QString>
#include <string>

namespace droppix {

// CWD/env resolver plus paths next to the GUI binary.
std::string resolve_web_root_for_gui();

// First non-loopback IPv4 address, or "127.0.0.1".
QString primary_lan_ipv4();

QString session_web_url(int port);              // uses primary_lan_ipv4()
QString session_web_url(const QString& ip, int port);   // for a chosen adapter IP

// Render URL as a QR code image (modules scaled). Empty image on failure.
QImage make_qr_image(const QString& text, int scale = 4);

}  // namespace droppix
