#pragma once
#include <QObject>
#include <QProcess>
namespace droppix {
class MdnsAdvertiser : public QObject {
  Q_OBJECT
 public:
  explicit MdnsAdvertiser(QObject* p=nullptr);
  bool available() const;
  void start(quint16 port);
  void stop();
 private:
  QProcess proc_;
};
}  // namespace droppix
