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
#include "mobs/csb.h"
#include "mobs/aes.h"
#include "mobs/rsa.h"
#include "mrpc.h"

#include <sstream>

#include <QMessageBox>
#include <QPixmap>
#include <QFileDialog>

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

inline std::basic_ios<char> &operator<<(std::basic_ios<char> &s, QString q) {
  s << q.toUtf8().data();
  return s;
}





//Objektdefinitionen


MOBS_ENUM_DEF(DocumenType, DocumentUnknown, DocumentPdf, DocumentJpeg, DocumentTiff, DocumentHtml, DocumentText);
MOBS_ENUM_VAL(DocumenType, "unk",           "pdf",       "jpg",        "tif",        "htm",        "txt");

class DocumentTags : virtual public mobs::ObjectBase
{
public:
  ObjInit(DocumentTags);

  MemVar(std::string, name);
  MemVar(std::string, content);
};


class Document : virtual public mobs::ObjectBase
{
public:
  ObjInit(Document);

  MemVar(uint64_t, docId);
  MemMobsEnumVar(DocumenType, type);
  MemVar(std::string, name);
  MemVar(std::vector<u_char>, content);

};
ObjRegister(Document);

class GetDocument : virtual public mobs::ObjectBase
{
public:
  ObjInit(GetDocument);

  MemVar(uint64_t, docId);
  MemVar(std::string, type);
  MemVar(bool, allowAttach);


};

class SearchDocument : virtual public mobs::ObjectBase
{
public:
  ObjInit(SearchDocument);

  MemVector(DocumentTags, tags);

};

class DocumentInfo : virtual public mobs::ObjectBase {
public:
  ObjInit(DocumentInfo);

  MemVar(uint64_t, docId);
  MemVector(DocumentTags, tags);
};

class SearchDocumentResult : virtual public mobs::ObjectBase {
public:
  ObjInit(SearchDocumentResult);

  MemVector(DocumentInfo, tags);

};
ObjRegister(SearchDocumentResult);

class DocumentRaw : virtual public mobs::ObjectBase
{
public:
  ObjInit(DocumentRaw);

  MemVar(uint64_t, docId);
  MemMobsEnumVar(DocumenType, type);
  MemVar(std::string, name);
  MemVar(int64_t, size);
};
ObjRegister(DocumentRaw);

class GetDocument_2 : virtual public mobs::ObjectBase {
public:
  ObjInit(GetDocument_2);

  MemVar(std::string, type);
  MemVar(std::string, name);
};


class SaveDocument : virtual public mobs::ObjectBase
{
public:
  ObjInit(SaveDocument);

  MemMobsEnumVar(DocumenType, type);
  MemVar(std::string, name);  /// obsolet
  MemVar(int64_t, size);
  MemVector(DocumentTags, tags);
  MemVar(uint64_t, supersedeId);  /// ersetzt objekt
  MemVar(uint64_t, parentId);     /// Abgeleitetes Objekt
  MemVar(std::string, creationInfo); /// Art ser Erzeugung/Ableitung/Ersetzung
  MemVar(mobs::MTime, creationTime); /// Zeitpunkt der Erzeugung, wenn ungleich Eintragezeitpunkt

  // TODO kleine Objekte embedded senden

};


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  ui->lineEditCnt->setText(QString::number(cnt));
  logging::currentLevel = LM_INFO;
  LOG(LM_INFO, "start");

  QStringList initialItems;
  initialItems << "" << tr("Name") << tr("Ablage") << tr("Schlagwort");

  ui->comboBoxTag1->addItems(initialItems);
  ui->comboBoxTag2->addItems(initialItems);
  ui->comboBoxTag3->addItems(initialItems);
  ui->comboBoxTag4->addItems(initialItems);
  ui->comboBoxTag5->addItems(initialItems);


  ui->pushButtonSave->setEnabled(false);

//  ui->widget->showPdfFile(QString("../fritz.pdf"));
//  ui->widget->showPdfFile("../Stunden.pdf");

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::load() {
#if 0
  try {
    LOG(LM_INFO, "load");
    ui->widget->clearViewer();
    ui->lineEdit1->clear();
    ui->lineEdit2->clear();

    elapsed.start();

    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected");

#if 0
    Gespann f1;
    f1.id(1);
    f1.typ("Brauereigespann");
    f1.zugmaschiene.typ("Sechsspänner");
    f1.zugmaschiene.achsen(0);
    f1.zugmaschiene.antrieb(true);
    f1.haenger[0].typ("Bräuwagen");
    f1.haenger[0].achsen(2);
    mrpc->send(&f1);
#else
    GetDocument_2 gd;
    gd.name("Auto.jpg");
//    mrpc->send(&gd);
#endif
    LOG(LM_INFO, "MAIN sent");
    ui->lineEdit1->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

    mobs::ObjectBase *obj = mrpc->sendAndWaitObj(&gd, 10);
//    mobs::ObjectBase *obj = mrpc->execNextObj(10);
    ui->lineEdit2->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

    LOG(LM_INFO, "MAIN received");

    if (obj) {
      ui->lineEditCnt->setText(QString::number(++cnt));
      LOG(LM_INFO, "RESULT ");
      if (auto pic = dynamic_cast<Document *>(obj)) {
        QPixmap pixmap;
        pixmap.loadFromData(&pic->content()[0], pic->content().size());
      } else if (auto pic = dynamic_cast<DocumentRaw *>(obj)) {
        QPixmap pixmap;
        LOG(LM_INFO, "READ " << pic->size() << " type " << pic->type.toStr(mobs::ConvToStrHint(false)));
        u_char *p = mrpc->getAttachment(pic->size(), 95);
        ui->lineEdit2->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

        pixmap.loadFromData(p, pic->size());
        ui->widget->showPicture(pixmap);
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
#endif
}

void MainWindow::save() {
  try {
    LOG(LM_INFO, "save");

    QString n = ui->lineEditName->text();
    QFile *file = new QFile(n);
    if (not file)
      return;
    elapsed.start();

    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected save " << file->size());

    SaveDocument doc;
    int pos = n.lastIndexOf('/');
    doc.name(n.midRef(pos+1).toUtf8().data());
    doc.size(file->size());
    if (n.endsWith(".pdf", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentPdf);
    else if (n.endsWith(".jpg", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentJpeg);
    else if (n.endsWith(".jpeg", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentJpeg);
    else if (n.endsWith(".tif", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentTiff);
    else if (n.endsWith(".tiff", Qt::CaseSensitivity::CaseInsensitive))
      doc.type(DocumenType::DocumentTiff);
    auto ft = file->fileTime(QFileDevice::FileModificationTime);
    if (ft.isValid()) {
      std::chrono::system_clock::time_point tp{}; // sollte genau auf "epoch" stehen
      tp += std::chrono::milliseconds(ft.currentMSecsSinceEpoch());
      doc.creationTime(std::chrono::time_point_cast<std::chrono::microseconds>(tp));
    }
    std::string tag, val;
    tag = ui->comboBoxTag1->currentText().toUtf8().data();
    val = ui->lineEditSearch1->text().toUtf8().data();
    if (not tag.empty()) {
      auto &t1 = doc.tags[mobs::MemBaseVector::nextpos];
      t1.name(tag);
      t1.content(val);
    }
    tag = ui->comboBoxTag2->currentText().toUtf8().data();
    val = ui->lineEditSearch2->text().toUtf8().data();
    if (not tag.empty()) {
      auto &t1 = doc.tags[mobs::MemBaseVector::nextpos];
      t1.name(tag);
      t1.content(val);
    }
    tag = ui->comboBoxTag3->currentText().toUtf8().data();
    val = ui->lineEditSearch3->text().toUtf8().data();
    if (not tag.empty()) {
      auto &t1 = doc.tags[mobs::MemBaseVector::nextpos];
      t1.name(tag);
      t1.content(val);
    }
    tag = ui->comboBoxTag4->currentText().toUtf8().data();
    val = ui->lineEditSearch4->text().toUtf8().data();
    if (not tag.empty()) {
      auto &t1 = doc.tags[mobs::MemBaseVector::nextpos];
      t1.name(tag);
      t1.content(val);
    }
    tag = ui->comboBoxTag5->currentText().toUtf8().data();
    val = ui->lineEditSearch5->text().toUtf8().data();
    if (not tag.empty()) {
      auto &t1 = doc.tags[mobs::MemBaseVector::nextpos];
      t1.name(tag);
      t1.content(val);
    }
    auto &t2 = doc.tags[mobs::MemBaseVector::nextpos];
    t2.name("OriginalFileName");
    t2.content(n.toUtf8().data());

    mrpc->send(&doc);

    mrpc->attachment(file);

    LOG(LM_INFO, "MAIN sent");
    ui->lineEdit1->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

    mobs::ObjectBase *obj = mrpc->execNextObj(90);
//    mobs::ObjectBase *obj = mrpc->execNextObj(10);
    ui->lineEdit2->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

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
    QMessageBox::information(this, windowTitle(), QString::fromUtf8(result.c_str()));

  } catch (std::exception &e) {
    LOG(LM_ERROR, "Exception in load " << e.what());
    QMessageBox::information(this, windowTitle(), QString::fromUtf8(e.what()));

  }
  mrpc = nullptr;


}

void MainWindow::loadFile() {
  ui->pushButtonSave->setEnabled(false);
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
    ui->lineEditName->setText(file);
    int pos = file.lastIndexOf('/');
    ui->comboBoxTag1->setCurrentText(tr("Name"));
    ui->comboBoxTag2->setCurrentText(tr("Ablage"));
    ui->comboBoxTag3->setCurrentText(tr("Schlagwort"));
    ui->lineEditSearch1->setText(file.midRef(pos+1).toUtf8().data());
    ui->lineEditSearch2->clear();
    ui->lineEditSearch3->clear();

    ui->pushButtonSave->setEnabled(true);
  }

}

void MainWindow::searchDocument() {
  std::string s1 = ui->lineEditSearch1->text().toUtf8().data();
  std::string s2 = ui->lineEditSearch2->text().toUtf8().data();
  std::string s3 = ui->lineEditSearch3->text().toUtf8().data();
  std::string s4 = ui->lineEditSearch4->text().toUtf8().data();
  std::string s5 = ui->lineEditSearch5->text().toUtf8().data();
  std::string t1 = ui->comboBoxTag1->currentText().toUtf8().data();
  std::string t2 = ui->comboBoxTag2->currentText().toUtf8().data();
  std::string t3 = ui->comboBoxTag3->currentText().toUtf8().data();
  std::string t4 = ui->comboBoxTag4->currentText().toUtf8().data();
  std::string t5 = ui->comboBoxTag5->currentText().toUtf8().data();
  ui->tableWidget->clear();
  ui->tableWidget->setColumnCount(1);
  ui->tableWidget->setRowCount(0);
  ui->tableWidget->hideColumn(0);

  try {
    elapsed.start();

    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected");

    SearchDocument sd;
    if (not t1.empty()) {
      auto &dt = sd.tags[mobs::MemBaseVector::nextpos];
      dt.name(t1);
      dt.content(s1);
    }
    if (not t2.empty()) {
      auto &dt = sd.tags[mobs::MemBaseVector::nextpos];
      dt.name(t2);
      dt.content(s2);
    }
    if (not t3.empty()) {
      auto &dt = sd.tags[mobs::MemBaseVector::nextpos];
      dt.name(t3);
      dt.content(s3);
    }
    if (not t4.empty()) {
      auto &dt = sd.tags[mobs::MemBaseVector::nextpos];
      dt.name(t4);
      dt.content(s4);
    }
    if (not t5.empty()) {
      auto &dt = sd.tags[mobs::MemBaseVector::nextpos];
      dt.name(t5);
      dt.content(s5);
    }

    LOG(LM_INFO, "MAIN sent");
    ui->lineEdit1->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

    mobs::ObjectBase *obj = mrpc->sendAndWaitObj(&sd, 10);
//    mobs::ObjectBase *obj = mrpc->execNextObj(10);
    ui->lineEdit2->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

    LOG(LM_INFO, "MAIN received");

    std::map<std::string, int> columns;
    if (obj) {
      ui->lineEditCnt->setText(QString::number(++cnt));
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
              if (ui->comboBoxTag1->findText(tagName) < 0) {
                ui->comboBoxTag1->addItem(tagName);
                ui->comboBoxTag2->addItem(tagName);
                ui->comboBoxTag3->addItem(tagName);
                ui->comboBoxTag4->addItem(tagName);
                ui->comboBoxTag5->addItem(tagName);
              }
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
  try {
    elapsed.start();

    mrpc = new MrpcClient(this);
    mrpc->waitReady(5);
    LOG(LM_INFO, "MAIN connected");

    GetDocument gd;
    //gd.name("Auto.jpg");
    gd.docId(doc);
    gd.allowAttach(true);
//    mrpc->send(&gd);
    LOG(LM_INFO, "MAIN sent");
    ui->lineEdit1->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

    mobs::ObjectBase *obj = mrpc->sendAndWaitObj(&gd, 10);
//    mobs::ObjectBase *obj = mrpc->execNextObj(10);
    ui->lineEdit2->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

    LOG(LM_INFO, "MAIN received");

    if (obj) {
      ui->lineEditCnt->setText(QString::number(++cnt));
      LOG(LM_INFO, "RESULT ");
      if (auto pic = dynamic_cast<Document *>(obj)) {
        if (pic->type() == DocumentPdf) {
          ui->widget->showPdfFile(QByteArray((char *)&pic->content()[0], int(pic->content().size())));
        } else {
          QPixmap pixmap;
          pixmap.loadFromData(&pic->content()[0], pic->content().size());
          ui->widget->showPicture(pixmap);
        }
      } else if (auto pic = dynamic_cast<DocumentRaw *>(obj)) {
        QPixmap pixmap;
        LOG(LM_INFO, "READ " << pic->size() << " type " << pic->type.toStr(mobs::ConvToStrHint(false)));
        u_char *p = mrpc->getAttachment(pic->size(), 95);
        ui->lineEdit2->setText(QString::number(elapsed.nsecsElapsed() / 1000000));

        if (pic->type() == DocumentPdf) {
          ui->widget->showPdfFile(QByteArray((char *)p, int(pic->size())));
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

