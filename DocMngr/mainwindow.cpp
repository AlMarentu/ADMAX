// ADMAX Advanced Document Management And Xtras
//
// Copyright 2021 Matthias Lautner
//
// This is part of MObs https://github.com/AlMarentu/ADMAX.git
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "mobs/logging.h"
#include "mobs/objgen.h"
#include "mobs/xmlread.h"
#include "mobs/xmlwriter.h"
#include "mobs/xmlout.h"
#include "mobs/mchrono.h"
#include "mobs/converter.h"
#include "mobs/csb.h"
#include "mobs/aes.h"
#include "mobs/rsa.h"
#include "mrpc.h"

#include <sstream>

#include <QMessageBox>
#include <QPixmap>
#include <QFileDialog>
#include <QRadioButton>
#include <QCheckBox>
#include <QFormLayout>
#include <QButtonGroup>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QLabel>
#include <set>
#include <fstream>

#include "mrpccli.h"
#include "viewer.h"

//class MRpcClient {
//public:
////  MRpcClient : streambufO(output)
////  std::stringstream input;
////  std::stringstream output;
////  mobs::CryptOstrBuf streambufO;
////  mobs::CryptIstrBuf streambufO;
//};

inline std::basic_ios<char> &operator<<(std::basic_ios<char> &s, const QString &q) {
  s << q.toUtf8().data();
  return s;
}

//Objektdefinitionen aus mrpc.h registrieren
ObjRegister(SessionError);
ObjRegister(CommandResult);
ObjRegister(SessionLogin);
ObjRegister(SessionResult);

ObjRegister(Document);
ObjRegister(DocumentRaw);
ObjRegister(SearchDocumentResult);
ObjRegister(ConfigResult);



MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  logging::currentLevel = LM_INFO;
  LOG(LM_INFO, "start");


  ui->pushButtonSave->setEnabled(false);

//  ui->widget->showPdfFile(QString("../fritz.pdf"));
//  ui->widget->showPdfFile("../Stunden.pdf");

  QTimer::singleShot(1, this, SLOT(getConfiguration()));

//  initTags(1);
}

MainWindow::~MainWindow()
{
    delete ui;
}


class SearchTag {
public:
  bool evaluate(mobs::MemberVector<DocumentTags> &tags, bool search);

  QString maskName;
  mobs::StringFormatter formatter{};
  bool useFormatter = false;
  bool isMaster = false;
  std::vector<std::string> tags; // bzgl. formatter
  std::vector<std::string> content; // bzgl. ButtonGroup
  QLineEdit *l1 = nullptr;
  QLineEdit *l2 = nullptr;
  QDateEdit *d1 = nullptr;
  QDateEdit *d2 = nullptr;
  QCheckBox *c1 = nullptr;
  QButtonGroup *bg = nullptr;
};

class ActionTemplate {
public:
  std::string pool;
  std::string name;
  std::list<SearchTag *> searchTags;
  std::set<std::string> extraTags;  // zusätzliche gefundene Tags

  void foundTag(const std::string &tag);
  void clear();
  bool setTag(const std::string &tag, const std::string &content);

  QFormLayout *layout = nullptr;

};

void ActionTemplate::foundTag(const std::string &tag) {
  if (extraTags.find(tag) != extraTags.end())
    return;
  QWidget *parent = searchTags.front()->l1; // TODO
  for (auto s:searchTags) {
    if (std::find(s->tags.begin(), s->tags.end(), tag) != s->tags.end())
      return;
  }
  LOG(LM_INFO, "Found unknown Tag " << tag);
  if (extraTags.empty()) {
  }
  extraTags.emplace(tag);
  auto sTag = new SearchTag;
  searchTags.emplace_back(sTag);
  sTag->maskName = QString::fromUtf8(tag.c_str());
  sTag->tags.emplace_back(tag);
  sTag->l1 = new QLineEdit(parent);
  layout->addRow(sTag->maskName, sTag->l1);
}

bool ActionTemplate::setTag(const std::string &tag, const std::string &content) {
  for (auto s:searchTags)
    if (std::find(s->tags.begin(), s->tags.end(), tag) != s->tags.end()) {
      if (s->l1) {
        s->l1->setText(QString::fromUtf8(content.c_str()));
        return true;
      }
      // TODO
    };
  return false;
}

void ActionTemplate::clear() {
  for (auto s:searchTags) {
    if (s->l1)
      s->l1->clear();
    if (s->c1)
      s->c1->setChecked(false);
    if (s->bg and s->bg->button(-1))
      s->bg->button(-1)->setChecked(true);
  }
}


bool SearchTag::evaluate(mobs::MemberVector<DocumentTags> &tList, bool search) {
  try {
    LOG(LM_INFO, "evaluate " << maskName.toUtf8().data());
    if (tags.empty())
      return true;
    if (bg) {
      int pos = bg->checkedId();
      if (pos >= 0 and pos < content.size()) {
        LOG(LM_INFO, "SEARCH " << tags.front() << " " << content[size_t(pos)]);
        auto &dt = tList[mobs::MemBaseVector::nextpos];
        dt.name(tags.front());
        dt.content(content[size_t(pos)]);
      }
    } else if (d1) {
      if (c1 and not c1->isChecked())
        return true;
      std::string von = d1->date().toString(Qt::ISODate).toStdString();
      std::string bis;
      if (d2)
        bis = d2->date().toString(Qt::ISODate).toStdString();
      if (von.empty())
        von.swap(bis);
      if (von.empty())
        return true;
      if (bis.empty() or von == bis or not search) {
        LOG(LM_INFO, "SEARCH " << tags.front() << " " << von);
        auto &dt = tList[mobs::MemBaseVector::nextpos];
        dt.name(tags.front());
        dt.content(von);
      } else {
        LOG(LM_INFO, "SEARCH " << tags.front() << " >=" << von);
        LOG(LM_INFO, "SEARCH " << tags.front() << " <=" << bis);
        auto &dt1 = tList[mobs::MemBaseVector::nextpos];
        dt1.name(tags.front());
        dt1.content(std::string(">=") + von);
        auto &dt2 = tList[mobs::MemBaseVector::nextpos];
        dt2.name(tags.front());
        dt2.content(std::string("<=") + bis);
      }
    } else if (c1) {
      if (c1->isChecked()) {
        LOG(LM_INFO, "SEARCH " << tags.front());
        auto &dt = tList[mobs::MemBaseVector::nextpos];
        dt.name(tags.front());
      }
    } else if (l1) {
      if (useFormatter) {
        std::wstring c = l1->text().toStdWString();
        if (c.empty())
          return true;
        std::wstring res;
        int pos = formatter.format(c, res);
        if (pos <= 0 or pos >= tags.size()) {
          l1->setFocus();
          l1->selectAll();
          return false;
        }
        LOG(LM_INFO, "SEARCH " << tags[size_t(pos)] << " " << mobs::to_string(res));
        auto &dt = tList[mobs::MemBaseVector::nextpos];
        dt.name(tags[size_t(pos)]);
        dt.content(mobs::to_string(res));
      } else {
        std::string c = l1->text().toUtf8().data();
        if (c.empty())
          return true;
        LOG(LM_INFO, "SEARCH " << tags.front() << " " << c);
        auto &dt = tList[mobs::MemBaseVector::nextpos];
        dt.name(tags.front());
        dt.content(c);
      }
    }
  } catch (std::exception &e) {
    LOG(LM_ERROR, "exception in evaluate " << e.what());
    if (l1) {
      l1->setFocus();
      l1->selectAll();
    }
    return false;
  }
  return true;
}

void MainWindow::initTags(int templateId) {
  TemplateInfo ti2;
  ti2.name("Doc");
  ti2.pool("doc");
  ti2.maskText("Suche Dokument");
  {
    auto &tag = ti2.tags[mobs::MemBaseVector::nextpos];
    tag.type(TagString);
    tag.name("Name");
    tag.maskText("Name");
  }
  {
   auto &tag = ti2.tags[mobs::MemBaseVector::nextpos];
    tag.type(TagString);
    tag.name("Ablage");
    tag.maskText("Ablage");
 }
 {
   auto &tag = ti2.tags[mobs::MemBaseVector::nextpos];
    tag.type(TagString);
    tag.name("Schlagwort");
    tag.maskText("Schlagwort");
  }
  {
   auto &tag = ti2.tags[mobs::MemBaseVector::nextpos];
    tag.type(TagEnumeration);
    tag.name("Status");
    tag.maskText("Status");
   tag.enums[mobs::MemBaseVector::nextpos]("verkauft");
 }

  const TemplateInfo &templateInfo = templateId > 1 ? ti2 : ti2;

  std::cout << templateInfo.to_string() << std::endl;
  initTags(templateInfo);
}

void MainWindow::initTags(const TemplateInfo &templateInfo) {
  int count = 0;
  QWidget *parent = ui->tabWidgetTagsPage1; // ui->groupBoxTags;
  if (not actionTemplates.empty()) {
    parent = new QWidget(ui->tabWidgetTags);
    count = ui->tabWidgetTags->addTab(parent, "");
  }
  LOG(LM_INFO, "TAB " << count);
  auto &currentTemplate = actionTemplates[count];
  currentTemplate.extraTags.emplace("OriginalFileName");  // TODO
  currentTemplate.pool = templateInfo.pool();
  currentTemplate.name = templateInfo.name();

  ui->tabWidgetTags->setTabText(count, QString::fromUtf8(templateInfo.maskText().c_str()));

//  ui->groupBoxTags->setTitle(QString::fromUtf8(ti.maskText().c_str()));
//  if (ui->groupBoxTags->layout())
//    ui->groupBoxTags->layout()->deleteLater();
  currentTemplate.layout = new QFormLayout(parent);
  if (templateInfo.type() == TemplateCreate) {
    auto layout1 = new QGridLayout(parent);
    auto sTag = new SearchTag;
    sTag->maskName = tr("File");
    sTag->tags.push_back("OriginalFileName");
    sTag->l1 = new QLineEdit(parent);
    sTag->l1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto but = new QPushButton(tr("open"), parent);
    connect(but, SIGNAL(pressed()), this, SLOT(loadFile()));
    layout1->addWidget(but, 0, 0);
    layout1->addWidget(sTag->l1, 0, 1);
    currentTemplate.layout->addRow(sTag->maskName, layout1);
    currentTemplate.searchTags.emplace_back(sTag);
  }
  for (auto &t:templateInfo.tags) {
    LOG(LM_INFO, "TAG " << t.type.toStr(mobs::ConvToStrHint(false)) << " " << t.maskText());
    QString n = QString::fromUtf8(t.maskText().c_str());
    SearchTag *sTag = nullptr;
    if (t.type() == TagIdent and not t.regex().empty()) {
      for (auto s:currentTemplate.searchTags) {
        if (s->maskName == n and s->useFormatter) {
          sTag = s;
          break;
        }
      }
    }
    if (not sTag) {
      sTag = new SearchTag;
      sTag->maskName = n;
      currentTemplate.searchTags.emplace_back(sTag);
    } else {
      size_t i = sTag->formatter.insertPattern(mobs::to_wstring(t.regex()), mobs::to_wstring(t.format()));
      if (i > 0) {
        sTag->tags.resize(i + 1);
        sTag->tags[i] = t.name();
        continue;
      }
    }

    switch (t.type()) {
      case TagIdent:
        sTag->isMaster = true;
      case TagString: {
        if (not t.regex().empty()) {
          size_t i = sTag->formatter.insertPattern(mobs::to_wstring(t.regex()), mobs::to_wstring(t.format()));
          if (i > 0) {
            sTag->tags.resize(i + 1);
            sTag->tags[i] = t.name();
            sTag->useFormatter = true;
          }
        } else
          sTag->tags.emplace_back(t.name());
        sTag->l1 = new QLineEdit(parent);

        currentTemplate.layout->addRow(n, sTag->l1);
        if (not sTag->isMaster and templateInfo.type() == TemplateSearch) {
          for (auto s:currentTemplate.searchTags) {
            if (s->isMaster)
              connect(s->l1, &QLineEdit::textChanged,
                      [s, sTag] { if (not s->l1->text().isEmpty()) sTag->l1->clear(); });
          }
        }
        break;
      }
      case TagDate: {
        sTag->tags.emplace_back(t.name());
        auto layout1 = new QGridLayout(parent);
        sTag->d1 = new QDateEdit(parent);
        sTag->d1->setCalendarPopup(true);
        sTag->d1->setMinimumDate(QDate::currentDate().addYears(-40));
        sTag->d1->setMaximumDate(QDate::currentDate().addYears(+20));
        sTag->d1->setDate(QDate::currentDate());
        layout1->addWidget(sTag->d1, 0, 1);
        if (templateInfo.type() == TemplateSearch) {
          layout1->addWidget(new QLabel("-", sTag->d1), 0, 2);
          sTag->d2 = new QDateEdit(parent);
          sTag->d2->setCalendarPopup(true);
          sTag->d2->setMinimumDate(QDate::currentDate().addYears(-40));
          sTag->d2->setMaximumDate(QDate::currentDate().addYears(+20));
          sTag->d2->setDate(QDate::currentDate());
          layout1->addWidget(sTag->d2, 0, 3);
        }
        sTag->c1 = new QCheckBox(sTag->d1);
        sTag->c1->setChecked(true);
        connect(sTag->c1, &QCheckBox::stateChanged, [this, sTag] {
          sTag->d1->setEnabled(sTag->c1->isChecked());
          if (sTag->d2) sTag->d2->setEnabled(sTag->c1->isChecked());
        });
//        sTag->l1 = new QLineEdit(parent);
//        sTag->l2 = new QLineEdit(parent);
        layout1->addWidget(sTag->c1, 0, 0);
        layout1->setAlignment(sTag->c1, Qt::AlignVCenter);
        currentTemplate.layout->addRow(n, layout1);
        currentTemplate.layout->setAlignment(Qt::AlignVCenter);
        if (templateInfo.type() == TemplateSearch) {
          for (auto s:currentTemplate.searchTags) {
            if (s->isMaster)
              connect(s->l1, &QLineEdit::textChanged, [s, sTag] { sTag->c1->setChecked(s->l1->text().isEmpty()); });
          }
        }
        break;
      }
      case TagEnumeration:
        sTag->tags.emplace_back(t.name());
        if (t.enums.size() == 0) {
          sTag->c1 = new QCheckBox(parent);
          currentTemplate.layout->addRow(n, sTag->c1);
          for (auto s:currentTemplate.searchTags) {
            if (s->isMaster)
              connect(s->l1, &QLineEdit::textChanged,
                      [s, sTag] { if (not s->l1->text().isEmpty()) sTag->c1->setChecked(false); });
          }
        } else {
          auto layout1 = new QGridLayout(parent);
          sTag->bg = new QButtonGroup(parent);
          int c = 0;
          for (auto &e:t.enums) {
            auto b = new QRadioButton(QString::fromUtf8(e().c_str()), parent);
            sTag->bg->addButton(b, c);
            sTag->content.emplace_back(e());
            layout1->addWidget(b, 0, c++);
          }
          auto b = new QRadioButton(templateInfo.type() == TemplateSearch ? tr("alle") : tr("--"), parent);
          sTag->bg->addButton(b, -1);
          b->setChecked(true);
          layout1->addWidget(b, 0, c);
          currentTemplate.layout->addRow(n, layout1);
        }
        break;
    }


  }


}

void MainWindow::load() {

}

void MainWindow::save() {
  try {
    LOG(LM_INFO, "save");
    auto i = actionTemplates.find(ui->tabWidgetTags->currentIndex());
    if (i == actionTemplates.end())
      return;
    auto &currentTemplate = i->second;

    QFile *file = new QFile(currentFile);
    if (not file)
      return;

    SaveDocument doc;
    int pos = currentFile.lastIndexOf('/');
    doc.name(currentFile.midRef(pos+1).toUtf8().data());
    doc.size(file->size());
    doc.templateName(currentTemplate.name);
    if (currentFile.endsWith(".pdf", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentPdf);
    else if (currentFile.endsWith(".jpg", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentJpeg);
    else if (currentFile.endsWith(".jpeg", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentJpeg);
    else if (currentFile.endsWith(".tif", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentTiff);
    else if (currentFile.endsWith(".tiff", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentTiff);
    auto ft = file->fileTime(QFileDevice::FileModificationTime);
    if (ft.isValid()) {
      std::chrono::system_clock::time_point tp{}; // sollte genau auf "epoch" stehen
      tp += std::chrono::milliseconds(ft.currentMSecsSinceEpoch());
      doc.creationTime(std::chrono::time_point_cast<std::chrono::microseconds>(tp));
    }

    for (auto s:currentTemplate.searchTags)
      if (not s->evaluate(doc.tags, false))
        return;

    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected save " << file->size());

    mrpc->send(&doc);

    mrpc->attachment(file);

    LOG(LM_INFO, "MAIN sent");
    int t1 = mrpc->elapsed.nsecsElapsed() / 1000000;

    mobs::ObjectBase *obj = mrpc->execNextObj(90);
//    mobs::ObjectBase *obj = mrpc->execNextObj(10);
    int t2 = mrpc->elapsed.nsecsElapsed() / 1000000;

    LOG(LM_INFO, "MAIN command send");

    LOG(LM_INFO, "MAIN received");

    std::string result;
    if (obj) {
      LOG(LM_INFO, "RESULT ");
      if (auto res = dynamic_cast<CommandResult *>(obj)) {
        LOG(LM_INFO, "RESULT MSG = " << res->msg());
        result = res->msg();
      } else {
        result = "invalid answer";
        LOG(LM_INFO, "RESULT unused " << obj->to_string());
      }
    }
    else
      result = "now answer";
    mrpc->waitDone();
    LOG(LM_INFO, "MAIN ready");
    ui->pushButtonSave->setEnabled(false);

    mrpc->close();
    ui->statusbar->showMessage(tr("%1 %2").arg(t1).arg(t2), 10000);

    QMessageBox::information(this, windowTitle(), QString::fromUtf8(result.c_str()));

  } catch (std::exception &e) {
    LOG(LM_ERROR, "Exception in load " << e.what());
    QMessageBox::information(this, windowTitle(), QString::fromUtf8(e.what()));

  }
  mrpc = nullptr;


}

void MainWindow::loadFile() {
  QString filter;
  QString file = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("PDF (*.pdf);;Images (*.png *.xpm *.jpg *.tif)"), &filter);
  if (not file.isEmpty()) {
    LOG(LM_INFO, "FIL " << filter.toStdString());
    if (filter.startsWith("PDF")) {
      ui->widget->showPdfFile(file);
    } else {
      QPixmap pixmap;
      pixmap.load(file);
      if (pixmap.isNull())
        return;
      ui->widget->showPicture(pixmap);
    }
    currentFile = file;
    ui->pushButtonSave->setEnabled(true);
    auto i = actionTemplates.find(ui->tabWidgetTags->currentIndex());
    if (i != actionTemplates.end()) {
      auto &currentTemplate = i->second;
      int pos = file.lastIndexOf('/');
      currentTemplate.clear();
      if (i->second.pool == "doc")
        currentTemplate.setTag("Name", file.midRef(pos + 1).toUtf8().data());
      currentTemplate.setTag("OriginalFileName", file.toUtf8().data());
    }

  }

}

void MainWindow::getConfiguration() {
  try {

    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected");

    GetConfig gc;
    gc.start(true);

    LOG(LM_INFO, "MAIN sent");
    int t1 = mrpc->elapsed.nsecsElapsed() / 1000000;

    mobs::ObjectBase *obj = mrpc->sendAndWaitObj(&gc, 10);
    mrpc->waitDone();

    int t2 = mrpc->elapsed.nsecsElapsed() / 1000000;
    ui->statusbar->showMessage(tr("ms: %1 %2").arg(t1).arg(t2), 10000);

    mrpc->close();
    LOG(LM_INFO, "MAIN received " << obj->to_string());
    std::ofstream oo("conf.json");
    oo << obj->to_string(mobs::ConvObjToString().exportJson().doIndent());
    oo.close();
    if (auto c = dynamic_cast<ConfigResult *>(obj)) {
      for (auto &i:c->templates)
        initTags(i);
    } else
      THROW("invalid result type");

  } catch (std::exception &e) {
    LOG(LM_ERROR, "Exception in getConfig " << e.what());
    QMessageBox::information(this, windowTitle(), QString::fromUtf8(e.what()));

  }
  mrpc = nullptr;
}

void MainWindow::searchDocument() {
  auto i = actionTemplates.find(ui->tabWidgetTags->currentIndex());
  if (i == actionTemplates.end())
    return;
  auto &currentTemplate = i->second;

  ui->tableWidget->clear();
  ui->tableWidget->setColumnCount(1);
  ui->tableWidget->setRowCount(0);
  ui->tableWidget->hideColumn(0);

  try {

    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected");

    SearchDocument sd;
    sd.templateName(currentTemplate.name);
    for (auto s:currentTemplate.searchTags)
      if (not s->evaluate(sd.tags, true))
        return;

    LOG(LM_INFO, "MAIN sent");
    int t1 = mrpc->elapsed.nsecsElapsed() / 1000000;

    mobs::ObjectBase *obj = mrpc->sendAndWaitObj(&sd, 10);
//    mobs::ObjectBase *obj = mrpc->execNextObj(10);
    int t2 = mrpc->elapsed.nsecsElapsed() / 1000000;
    ui->statusbar->showMessage(tr("ms: %1 %2").arg(t1).arg(t2), 10000);

    LOG(LM_INFO, "MAIN received");

    std::map<std::string, int> columns;
    if (obj) {
      LOG(LM_INFO, "RESULT " << obj->to_string());
      if (auto res = dynamic_cast<SearchDocumentResult *>(obj)) {
        for(auto &i:res->tags) {
          LOG(LM_INFO, "Result: " << i.docId());
          int row = ui->tableWidget->rowCount();
          ui->tableWidget->setRowCount(row+1);
          ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString::number(i.docId())));
          for (auto &j:i.tags) {
            int col;
            auto it = columns.find(j.name());
            if (it == columns.end()) {
              col = ui->tableWidget->columnCount();
              ui->tableWidget->setColumnCount(col +1);
              columns[j.name()] = col;
              auto tagName = QString::fromUtf8(j.name().c_str());
              ui->tableWidget->setHorizontalHeaderItem(col, new QTableWidgetItem(tagName));
              currentTemplate.foundTag(j.name());
            } else
              col = it->second;
            LOG(LM_INFO, "T " << j.name() << "=" << j.content());
            ui->tableWidget->setItem(row, col, new QTableWidgetItem(QString::fromUtf8(j.content().c_str())));
          }
        }
      }
      else
        LOG(LM_INFO, "RESULT unused " << obj->to_string());
    }
    mrpc->waitDone();
    LOG(LM_INFO, "MAIN ready");

    mrpc->close();
  } catch (std::exception &e) {
    LOG(LM_ERROR, "Exception in load " << e.what());
    QMessageBox::information(this, windowTitle(), QString::fromUtf8(e.what()));

  }
  mrpc = nullptr;


}

void MainWindow::getDocument() {
//  loadDocument(1);
  int row = ui->tableWidget->currentRow();
  if (row >= 0)
    searchRowClicked(row, 0);
}



void MainWindow::loadDocument(int64_t doc)
{
  ui->pushButtonSave->setEnabled(false);
  try {
    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected");

    GetDocument gd;
    //gd.name("Auto.jpg");
    gd.docId(doc);
    gd.allowAttach(true);
//    mrpc->send(&gd);
    LOG(LM_INFO, "MAIN sent");
    int t1 = mrpc->elapsed.nsecsElapsed() / 1000000;

    mobs::ObjectBase *obj = mrpc->sendAndWaitObj(&gd, 10);
//    mobs::ObjectBase *obj = mrpc->execNextObj(10);
    int t2 = mrpc->elapsed.nsecsElapsed() / 1000000;
    ui->statusbar->showMessage(tr("ms: %1 %2").arg(t1).arg(t2), 10000);

    LOG(LM_INFO, "MAIN received");

    if (obj) {
      LOG(LM_INFO, "RESULT ");
      if (auto pic = dynamic_cast<Document *>(obj)) {
        if (pic->type() == DocumentPdf) {
          ui->widget->showPdfBuffer(QByteArray((char *) &pic->content()[0], int(pic->content().size())));
        } else {
          QPixmap pixmap;
          pixmap.loadFromData(&pic->content()[0], pic->content().size());
          ui->widget->showPicture(pixmap);
        }
      } else if (auto pic = dynamic_cast<DocumentRaw *>(obj)) {
        QPixmap pixmap;
        LOG(LM_INFO, "READ " << pic->size() << " type " << pic->type.toStr(mobs::ConvToStrHint(false)));
        u_char *p = mrpc->getAttachment(pic->size(), 95);
        int t3 = mrpc->elapsed.nsecsElapsed() / 1000000;
        ui->statusbar->showMessage(tr("ms: %1 %2 %3").arg(t1).arg(t2).arg(t3), 10000);

        if (pic->type() == DocumentPdf) {
          ui->widget->showPdfBuffer(QByteArray((char *) p, int(pic->size())));
        } else {
          pixmap.loadFromData(p, pic->size());
          ui->widget->showPicture(pixmap);
        }
      } else
        LOG(LM_INFO, "RESULT unused " << obj->to_string());
    }
    mrpc->waitDone();
    LOG(LM_INFO, "MAIN ready");

    mrpc->close();
  } catch (std::exception &e) {
    LOG(LM_ERROR, "Exception in load " << e.what());
    QMessageBox::information(this, windowTitle(), QString::fromUtf8(e.what()));

  }
  mrpc = nullptr;


}

void MainWindow::searchRowClicked(int row, int col) {
  auto item = ui->tableWidget->item(row, 0);
  if (item) {
    int64_t doc = item->text().toLong();
    if (doc > 0)
      loadDocument(doc);
  }
}

