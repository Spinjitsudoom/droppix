#pragma once
#include <QWidget>

namespace droppix {

// Static "About" section: app identity, version, a few key/value facts about
// the current build (protocol/backend/encoders), and links out to the
// project page + issue tracker. No controllers — content is fixed at
// construction time.
class AboutPage : public QWidget {
  Q_OBJECT
 public:
  explicit AboutPage(QWidget* parent = nullptr);
};

}  // namespace droppix
