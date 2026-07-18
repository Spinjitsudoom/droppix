#pragma once
#include <QDockWidget>

class QLineEdit;
class QListView;
class QComboBox;
class QCheckBox;

namespace droppix {

class LogBuffer;
class LogModel;
class LogFilterProxy;

// Bottom dock panel showing the LogBuffer with search / level / source filters,
// autoscroll, copy, clear, and save-to-file.
class LogPanel : public QDockWidget {
  Q_OBJECT
 public:
  explicit LogPanel(LogBuffer* buffer, QWidget* parent = nullptr);

 private:
  void refreshSources();
  void copySelection();
  void saveToFile();

  LogBuffer*      buffer_;
  LogModel*       model_;
  LogFilterProxy* proxy_;
  QListView*      view_;
  QLineEdit*      search_;
  QComboBox*      sourceBox_;
  QCheckBox*      autoscroll_;
};

}  // namespace droppix
