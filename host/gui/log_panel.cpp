#include "log_panel.h"
#include "log_buffer.h"
#include "log_model.h"
#include "log_entry.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QScrollBar>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace droppix {

LogPanel::LogPanel(LogBuffer* buffer, QWidget* parent)
    : QDockWidget(tr("Debug log"), parent), buffer_(buffer) {
  setObjectName(QStringLiteral("logPanel"));

  auto* root = new QWidget(this);
  auto* col = new QVBoxLayout(root);
  col->setContentsMargins(4, 4, 4, 4);

  // toolbar row
  auto* bar = new QHBoxLayout();
  search_ = new QLineEdit(root);
  search_->setPlaceholderText(tr("search…"));
  bar->addWidget(search_, 1);

  auto* infoBtn = new QToolButton(root);
  infoBtn->setText(QStringLiteral("INF")); infoBtn->setCheckable(true); infoBtn->setChecked(true);
  auto* warnBtn = new QToolButton(root);
  warnBtn->setText(QStringLiteral("WRN")); warnBtn->setCheckable(true); warnBtn->setChecked(true);
  auto* errBtn = new QToolButton(root);
  errBtn->setText(QStringLiteral("ERR")); errBtn->setCheckable(true); errBtn->setChecked(true);
  bar->addWidget(infoBtn); bar->addWidget(warnBtn); bar->addWidget(errBtn);

  sourceBox_ = new QComboBox(root);
  sourceBox_->addItem(tr("all sources"), QString());
  bar->addWidget(sourceBox_);

  autoscroll_ = new QCheckBox(tr("autoscroll"), root);
  autoscroll_->setChecked(true);
  bar->addWidget(autoscroll_);

  auto* clearBtn = new QToolButton(root); clearBtn->setText(tr("Clear"));
  auto* copyBtn  = new QToolButton(root); copyBtn->setText(tr("Copy"));
  auto* saveBtn  = new QToolButton(root); saveBtn->setText(tr("Save…"));
  bar->addWidget(clearBtn); bar->addWidget(copyBtn); bar->addWidget(saveBtn);
  col->addLayout(bar);

  // view
  model_ = new LogModel(buffer_, this);
  proxy_ = new LogFilterProxy(this);
  proxy_->setSourceModel(model_);
  view_ = new QListView(root);
  view_->setModel(proxy_);
  view_->setUniformItemSizes(true);
  view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  QFont mono(QStringLiteral("monospace"));
  mono.setStyleHint(QFont::Monospace);
  view_->setFont(mono);
  col->addWidget(view_, 1);

  setWidget(root);

  // wiring
  connect(search_, &QLineEdit::textChanged, proxy_, &LogFilterProxy::setSearchText);
  connect(infoBtn, &QToolButton::toggled, this, [this](bool on) { proxy_->setLevelEnabled(LogLevel::Info, on); });
  connect(warnBtn, &QToolButton::toggled, this, [this](bool on) { proxy_->setLevelEnabled(LogLevel::Warn, on); });
  connect(errBtn,  &QToolButton::toggled, this, [this](bool on) { proxy_->setLevelEnabled(LogLevel::Error, on); });
  connect(sourceBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { proxy_->setSourceFilter(sourceBox_->currentData().toString()); });
  connect(clearBtn, &QToolButton::clicked, buffer_, &LogBuffer::clear);
  connect(copyBtn,  &QToolButton::clicked, this, &LogPanel::copySelection);
  connect(saveBtn,  &QToolButton::clicked, this, &LogPanel::saveToFile);

  // autoscroll: follow the tail; pause when scrolled up; resume at the bottom
  connect(model_, &QAbstractItemModel::rowsInserted, this, [this] {
    if (autoscroll_->isChecked()) view_->scrollToBottom();
  });
  connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
    autoscroll_->setChecked(v == view_->verticalScrollBar()->maximum());
  });

  // keep the source dropdown current as new sources appear
  connect(buffer_, &LogBuffer::entryAdded, this, [this](const LogEntry& e) {
    if (!e.source.isEmpty() && sourceBox_->findData(e.source) < 0)
      sourceBox_->addItem(e.source, e.source);
  });
  refreshSources();
}

void LogPanel::refreshSources() {
  for (const auto& e : buffer_->entries())
    if (!e.source.isEmpty() && sourceBox_->findData(e.source) < 0)
      sourceBox_->addItem(e.source, e.source);
}

void LogPanel::copySelection() {
  const QModelIndexList sel = view_->selectionModel()->selectedIndexes();
  QStringList lines;
  for (const QModelIndex& i : sel) lines << i.data(Qt::DisplayRole).toString();
  if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join('\n'));
}

void LogPanel::saveToFile() {
  const QString suggested =
      QStringLiteral("droppix-%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
  const QString path = QFileDialog::getSaveFileName(this, tr("Save log"), suggested,
                                                    tr("Log files (*.log);;All files (*)"));
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
  QTextStream out(&f);
  for (const auto& e : buffer_->entries()) {
    const QString ts = QDateTime::fromMSecsSinceEpoch(e.epochMs).toString(Qt::ISODate);
    out << ts << ' '
        << (e.session.isEmpty() ? QString() : "[" + e.session + "]")
        << (e.source.isEmpty() ? QString() : "[" + e.source + "]") << ' '
        << e.text << '\n';
  }
}

}  // namespace droppix
