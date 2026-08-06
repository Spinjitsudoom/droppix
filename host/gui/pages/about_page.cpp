#include "about_page.h"

#include "version.h"

#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace droppix {

namespace {
constexpr const char* kProjectUrl = "https://github.com/Spinjitsudoom/droppix";
constexpr const char* kIssuesUrl  = "https://github.com/Spinjitsudoom/droppix/issues";
}  // namespace

AboutPage::AboutPage(QWidget* parent) : QWidget(parent) {
  auto* card = new QFrame(this);
  card->setObjectName("card");

  auto* header = new QLabel("droppix", card);
  header->setObjectName("header");

  auto* caption = new QLabel("Spacedesk-style extended display for Linux", card);
  caption->setObjectName("caption");

  auto* version = new QLabel(QString("Version %1").arg(app_version()), card);
  version->setObjectName("aboutVersion");

  auto* facts = new QGridLayout;
  facts->setContentsMargins(0, 8, 0, 8);
  facts->setHorizontalSpacing(12);
  facts->setVerticalSpacing(4);
  const struct { const char* key; const char* value; } kFacts[] = {
      {"Protocol", "HELLO v6"},
      {"Backend",  "evdi · KWin/X11"},
      {"Encoders", "NVENC · VAAPI · x264"},
  };
  int row = 0;
  for (const auto& f : kFacts) {
    auto* keyLabel = new QLabel(f.key, card);
    keyLabel->setObjectName("caption");
    facts->addWidget(keyLabel, row, 0);
    facts->addWidget(new QLabel(f.value, card), row, 1);
    ++row;
  }

  auto* projectBtn = new QPushButton("Project page", card);
  connect(projectBtn, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl(kProjectUrl));
  });

  auto* issueBtn = new QPushButton("Report an issue", card);
  connect(issueBtn, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl(kIssuesUrl));
  });

  auto* btnRow = new QHBoxLayout;
  btnRow->addWidget(projectBtn);
  btnRow->addWidget(issueBtn);
  btnRow->addStretch();

  auto* cardLayout = new QVBoxLayout;
  cardLayout->addWidget(header);
  cardLayout->addWidget(caption);
  cardLayout->addWidget(version);
  cardLayout->addLayout(facts);
  cardLayout->addLayout(btnRow);
  card->setLayout(cardLayout);

  auto* root = new QVBoxLayout(this);
  root->addWidget(card);
  root->addStretch();
}

}  // namespace droppix
