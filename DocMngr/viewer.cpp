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


#include "viewer.h"
#include "ui_viewer.h"
#include "poppler-qt5.h"
#include "poppler-form.h"
#include <QLabel>
#include <QPainter>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QScrollBar>
#include <QPaintEvent>
#include "mobs/logging.h"

inline std::basic_ios<char> &operator<<(std::basic_ios<char> &s, QString q) {
  s << q.toUtf8().data();
  return s;
}

namespace {

class PdfPage;

class MyLabel : public QLabel {
public:
  MyLabel(QWidget *parent, PdfPage *p, ViewerData *v) : QLabel("...", parent), page(p), viewer(v) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
//    auto f = font();
//    LOG(LM_INFO, "FONT " << f.pointSizeF());
  }
  QRect getRect(const QRectF &r) {
    return QRect(r.x() * width(), r.y() * height(), r.width() * width(), r.height() * height());
  }
  void rerender() {
    done = false;
  }

protected:
  PdfPage *page;
  ViewerData *viewer;
  bool done = false;
  void paintEvent(QPaintEvent *event) override;
};

class PdfFormField {
public:
  PdfFormField(Poppler::FormField *f, MyLabel *l) : formField(f), label(l) {};
  Poppler::FormField *formField;
  MyLabel *label;
  QWidget *widget = nullptr;
  int fontSize = 0;

  void rePos(double dpi) {
    if (widget) {
      auto rect = label->getRect(formField->rect());
      widget->resize(rect.width(), rect.height());
      widget->move(rect.x(), rect.y());
      if (fontSize > 3) {
        QFont font = widget->font();
        font.setPointSizeF( dpi / 75 * fontSize); // TODO scaling testen
        widget->setFont(font);
      }
    }
  }

};

class PdfPage {
  friend class MyLabel;
public:
  MyLabel *label = nullptr;
  QLabel *labelThumb = nullptr;
  Poppler::Page* docPage = nullptr;
  int pgNum = 0;
  int width = 0;
  int height = 0;
  QSizeF size{};

  ~PdfPage() {
    delete docPage;
  }

  void resize(double dpi) {
    if (label) {
      label->resize(dpi * size.width() / 72.0, dpi * size.height() / 72.0);
      label->setMaximumSize(dpi * size.width() / 72.0, dpi * size.height() / 72.0);
      label->setMinimumSize(dpi * size.width() / 72.0, dpi * size.height() / 72.0);
    }
  }
  void render(Poppler::Document* document, double dpi) {
    Poppler::Page* pdfPage = docPage;
    if (not pdfPage)
      document->page(pgNum);  // Document starts at page 0
    if (not pdfPage) {
      // ... error message ...
      return;
    }

    // Generate a QImage of the rendered page
    QImage image = pdfPage->renderToImage(dpi, dpi);
    if (image.isNull()) {
      // ... error message ...
      return;
    }

    width = image.width();
    height = image.height();

    LOG(LM_INFO, "SZ " << image.width() << " " << image.height() << " " << size.width() << "x" << size.height());
    QPixmap pix;
    if (pix.convertFromImage(image))
      label->setPixmap(pix);
//    ui->scrollArea->setWidget(label);
// ... use image ...

// after the usage, the page must be deleted
    if (not docPage)  // Page wurde lokal erzeugt
      delete pdfPage;
  }


};


}

class ViewerData {
public:
  ~ViewerData() {
    delete document;
  }
  std::vector<PdfPage> pages;
  QGridLayout *grid = nullptr;
//  QGridLayout *gridThumb = nullptr;
  QWidget *thumbs = nullptr;
  QSize thumbSize{};
  int thumbCols = 0;
  double aktDpi = 150.0;
  double xPos = 0.0;
  double yPos = 0.0;
  Poppler::Document* document = nullptr;
  std::list<PdfFormField> formFields;
  QPixmap pixmap;
};

void MyLabel::paintEvent(QPaintEvent *event) {
  if (not done) {
    LOG(LM_INFO, "painter " << page->pgNum << "  " << event->rect().x() << "," << event->rect().y() << " " << event->rect().width() << "x" << event->rect().height());
    page->render(viewer->document, viewer->aktDpi);
    done = true;
  }

  QLabel::paintEvent(event);
}

Viewer::Viewer(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Viewer),
    data(new ViewerData)
{
    ui->setupUi(this);
    ui->pushButtonEditSave->setVisible(false);
    ui->pushButtonEditCancel->setEnabled(false);

    ui->horizontalSliderZoom->setRange(20, 150);
    ui->horizontalSliderZoom->setValue(150);
    connect(ui->scrollArea->verticalScrollBar(), SIGNAL(rangeChanged(int, int)), this, SLOT(vRange(int, int)));
    connect(ui->scrollArea->horizontalScrollBar(), SIGNAL(rangeChanged(int, int)), this, SLOT(hRange(int, int)));
    connect(ui->scrollArea->verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(vChange(int)));
    connect(ui->scrollArea->horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(hChange(int)));
    connect(this, SIGNAL(sizeChanged()), this, SLOT(formRepos()));

}

Viewer::~Viewer()
{
  delete data;
  delete ui;
}

void Viewer::showPdfFile(QString filename) {
  clearViewer();
  Poppler::Document *document = Poppler::Document::load(filename);
  if (!document || document->isLocked()) {

    // ... error message ....

    delete document;
    return;
  }


  // Paranoid safety check
  if (document == 0) {
    // ... error message ...
    return;
  }
  data->document = document;
  showDocument();
}

void Viewer::showPdfFile(const QByteArray &bytes) {
  clearViewer();
  Poppler::Document *document = Poppler::Document::loadFromData(bytes);
  if (!document) {

    // ... error message ....

    delete document;
    return;
  }

  data->document = document;
  showDocument();
}

void Viewer::showDocument() {

  ui->horizontalSliderZoom->setRange(20, 150);
  ui->horizontalSliderZoom->setValue(150);

  QWidget *widget = new QWidget(this);
  data->grid = new QGridLayout(widget);
  ui->scrollArea->setWidget(widget);


  int np = data->document->numPages();
  data->pages.resize(np);
  for (int i = 0; i < np; i++) {
    PdfPage &page = data->pages[i];
    page.pgNum = i;
    page.docPage = data->document->page(i);
    page.size = page.docPage->pageSizeF();
    page.label = new MyLabel(this, &page, data);
    data->grid->addWidget(page.label, i, 0);
    page.label->setScaledContents(true);
    page.resize(data->aktDpi);
    QList<Poppler::FormField *> fields = page.docPage->formFields();
    for (auto f:fields) {
      if (f)
        data->formFields.emplace_back(f, page.label);
    }
  }
  ui->spinBoxPage->setMinimum(1);
  ui->spinBoxPage->setMaximum(np +1);

//  data->grid->invalidate();
  LOG(LM_INFO, "START RENDER");
//  for (auto &p:data->pages) {
//    p.render(data->document, data->aktDpi);
//    LOG(LM_INFO, "PG " << p.pgNum << " " << p.label->isVisibleTo(ui->scrollArea));
//  }
  ui->spinBoxPage->setValue(1);
  ui->pushButtonEdit->setEnabled(not data->formFields.empty());


}

void Viewer::setZoom(int z) {
  LOG(LM_INFO, "setZoom " << z << " " << ui->scrollArea->widget()->width() << "x" << ui->scrollArea->widget()->height() << " "
  << ui->scrollArea->widget()->x() << ", " << ui->scrollArea->widget()->y());

  if (data->document) {
    data->aktDpi = z;
    double res = z;
    for (auto &p:data->pages) {
      p.resize(res);
    }
  } else {
    auto widget = dynamic_cast<QLabel *>(ui->scrollArea->widget());
    if (widget) {
      int a = data->pixmap.width() * z / 100;
      LOG(LM_INFO, "Resie w=" << a);
      widget->setPixmap(data->pixmap.scaledToWidth(a));
    }
  }
}



void Viewer::butt() {
  LOG(LM_INFO, "BUTT");

}

void Viewer::editForm() {
  ui->pushButtonEditSave->setVisible(true);
  ui->pushButtonEdit->setVisible(false);
  ui->pushButtonEditCancel->setEnabled(true);
  ui->pushButtonThumb1->setEnabled(false);
  std::map<int, QButtonGroup *> buttonGrpups;
  for (auto &ff:data->formFields) {
//    QRect rect = ff.label->getRect(ff.formField->rect());
//    LOG(LM_INFO, "Field " << ff.formField->name().toUtf8().data() << " " << rect.x() << "," << rect.y() << " " << rect.width() << "x" << rect.right());
    LOG(LM_INFO, "FQ " << ff.formField->fullyQualifiedName().toUtf8().data());
    if (auto l = ff.formField->activationAction()) {
      LOG(LM_INFO, "has actionActivation");
    }
    if (auto l = ff.formField->additionalAction(Poppler::FormField::FormatField)) {
      LOG(LM_INFO, "has action Format");
    }
    if (auto l = ff.formField->additionalAction(Poppler::FormField::FieldModified)) {
      LOG(LM_INFO, "has action Modified");
    }
    if (auto l = ff.formField->additionalAction(Poppler::FormField::ValidateField)) {
      LOG(LM_INFO, "has action Validate");
    }
    if (auto l = ff.formField->additionalAction(Poppler::FormField::CalculateField)) {
      LOG(LM_INFO, "has action Calculate");
    }
    if (auto but = dynamic_cast<Poppler::FormFieldButton *>(ff.formField)) {
      LOG(LM_INFO, "Button " << ff.formField->id() << " " << but->caption().toUtf8().data());
      if (but->buttonType() == Poppler::FormFieldButton::CheckBox) {
        auto e = new QCheckBox("", ff.label);
        e->setChecked(but->state());
        ff.widget = e;
      } else if (but->buttonType() == Poppler::FormFieldButton::Radio) {
        auto it = buttonGrpups.find(ff.formField->id());
        if (it == buttonGrpups.end()) {
          it = buttonGrpups.emplace(ff.formField->id(), new QButtonGroup(ff.label)).first;
          auto lst = but->siblings();
          for (auto i:lst)  // bei siblings buttongroup eintragen (falls noch nicht vorhanden)
            buttonGrpups.emplace(i, it->second);
        }
        auto e = new QRadioButton("", ff.label);
        it->second->addButton(e);
        e->setChecked(but->state());
        ff.widget = e;
      } else {
        // TODO PushButton
      }
    }
    if (auto txt = dynamic_cast<Poppler::FormFieldText *>(ff.formField)) {
      LOG(LM_INFO, "Text " << txt->maximumLength() << " " << txt->getFontSize());
      ff.fontSize = txt->getFontSize();
      if (txt->textType() == Poppler::FormFieldText::Multiline) {
        auto e = new QPlainTextEdit(txt->text(), ff.label);
        ff.widget = e;
      } else if (txt->textType() == Poppler::FormFieldText::Normal) {
        int l = txt->maximumLength();
        auto e = new QLineEdit(txt->text(), ff.label);
        e->setAlignment(txt->textAlignment());
        if (txt->isPassword())
          e->setEchoMode(QLineEdit::PasswordEchoOnEdit);
        if (l > 0)
          e->setMaxLength(l);
        ff.widget = e;
      } else {
        // TODO FileSelect
      }
    }
    if (auto cho = dynamic_cast<Poppler::FormFieldChoice *>(ff.formField)) {
      LOG(LM_INFO, "Choice ");
    }
    if (auto sig = dynamic_cast<Poppler::FormFieldSignature *>(ff.formField)) {
      LOG(LM_INFO, "Signature ");
    }
    if (ff.widget) {
      ff.rePos(data->aktDpi);
      ff.widget->show();
    }
  }
}


void Viewer::cancelEditForm() {
  LOG(LM_INFO, "CANCEL FORM");
  ui->pushButtonEditSave->setVisible(false);
  ui->pushButtonEdit->setVisible(true);
  ui->pushButtonEditCancel->setEnabled(false);
  ui->pushButtonThumb1->setEnabled(true);
  for (auto &ff:data->formFields) {
    if (auto but = dynamic_cast<Poppler::FormFieldButton *>(ff.formField)) {
      LOG(LM_INFO, "Button " << ff.formField->id() << " " << but->caption().toUtf8().data());
      if (but->buttonType() == Poppler::FormFieldButton::Radio) {
        if (auto e = dynamic_cast<QRadioButton *>(ff.widget)) {
          auto bg = e->group();
          if (bg)
            bg->deleteLater();
        }
      }
    }
    delete ff.widget;
    ff.widget = nullptr;
  }
}

void Viewer::saveEditForm() {
  LOG(LM_INFO, "SAVE FORM");
  ui->pushButtonEditSave->setVisible(false);
  ui->pushButtonEdit->setVisible(true);
  ui->pushButtonEditCancel->setEnabled(false);
  ui->pushButtonThumb1->setEnabled(true);
  for (auto &ff:data->formFields) {
    if (auto but = dynamic_cast<Poppler::FormFieldButton *>(ff.formField)) {
      LOG(LM_INFO, "Button ");
      if (auto e = dynamic_cast<QCheckBox *>(ff.widget))
        but->setState(e->isChecked());
      else if (auto e = dynamic_cast<QRadioButton *>(ff.widget)) {
        auto bg = e->group();
        if (bg)
          bg->deleteLater();
        but->setState(e->isChecked());
      }
      ff.label->rerender();
    }
    if (auto txt = dynamic_cast<Poppler::FormFieldText *>(ff.formField)) {
      LOG(LM_INFO, "Text " << txt->maximumLength());
      if (txt->textType() == Poppler::FormFieldText::Multiline) {
        if (auto e = dynamic_cast<QPlainTextEdit *>(ff.widget)) {
          txt->setText(e->toPlainText());
          LOG(LM_INFO, "Neu: " << txt->text().toUtf8().data());
        }
      } else {
        if (auto e = dynamic_cast<QLineEdit *>(ff.widget)) {
          txt->setText(e->text());
          LOG(LM_INFO, "Neu: " << txt->text().toUtf8().data());
        }
      }
      ff.label->rerender();
    }
    if (auto cho = dynamic_cast<Poppler::FormFieldChoice *>(ff.formField)) {
      LOG(LM_INFO, "Choice ");
    }
    if (auto sig = dynamic_cast<Poppler::FormFieldSignature *>(ff.formField)) {
      LOG(LM_INFO, "Signature ");
    }
    delete ff.widget;
    ff.widget = nullptr;
  }
}

void Viewer::vRange(int min, int max) {
  QScrollBar *vertSb = ui->scrollArea->verticalScrollBar();
  int pos = data->yPos * max;
//  LOG(LM_INFO, "vRange " << max << " " << pos);
  vertSb->setValue(pos);
  emit sizeChanged();
}

void Viewer::hRange(int min, int max) {
  QScrollBar *horiSb = ui->scrollArea->horizontalScrollBar();
  int pos = data->xPos * max;
//  LOG(LM_INFO, "hRange " << max << " " << pos);
  horiSb->setValue(pos);
  emit sizeChanged();
}


void Viewer::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (ui->stackedWidget->currentIndex() == 1) {
    thumbReorg();
  }
}

void Viewer::vChange(int) {
 if (not ui->scrollArea)
   return;
  QScrollBar *vertSb = ui->scrollArea->verticalScrollBar();
  data->yPos = double(vertSb->value()) / vertSb->maximum();
  QWidget *w = ui->scrollArea->childAt(ui->scrollArea->width() / 2, ui->scrollArea->height() / 2);
  if (w) {
    for (auto &p:data->pages) {
      if (p.label == w) {
        ui->spinBoxPage->setValue(p.pgNum +1);
        break;
      }
    }
  }
}

void Viewer::hChange(int) {
  QScrollBar *horiSb = ui->scrollArea->horizontalScrollBar();
  data->xPos = double(horiSb->value()) / horiSb->maximum();
}

void Viewer::setPage(int p) {
  LOG(LM_INFO, "setPage " << p);
  ui->scrollArea->setFocus();
  if (p > 0 and p <= data->pages.size()) {
    auto &page = data->pages[p -1];
    ui->scrollArea->ensureWidgetVisible(page.label);
  }
}

void Viewer::formRepos() {
  LOG(LM_INFO, "REPOS");
  for (auto &ff:data->formFields)
    ff.rePos(data->aktDpi);
}

void Viewer::thumbnails(bool on) {
  if (on) {
    ui->pushButtonThumb2->setChecked(true);
    ui->stackedWidget->setCurrentIndex(1);

    if (not data->thumbs) {
      data->thumbs = new QWidget(this);
//      data->gridThumb = new QGridLayout(widget);
      ui->scrollAreaThumb->setWidget(data->thumbs);

      int cols = 3;
      double dpi = 25.0;
      int maxX = 0;
      int maxY = 0;
      for (auto &p:data->pages) {
        p.labelThumb = new QLabel(data->thumbs);
//        data->gridThumb->addWidget(p.labelThumb, p.pgNum / cols, p.pgNum % cols);
//        p.labelThumb->setScaledContents(true);
//        p.labelThumb->resize(dpi * p.size.width() / 72.0, dpi * p.size.height() / 72.0);
//        p.labelThumb->setMaximumSize(dpi * p.size.width() / 72.0, dpi * p.size.height() / 72.0);
//        p.labelThumb->setMinimumSize(dpi * p.size.width() / 72.0, dpi * p.size.height() / 72.0);

        Poppler::Page *pdfPage = p.docPage;
        if (not pdfPage)
          data->document->page(p.pgNum);  // Document starts at page 0
        if (not pdfPage) {
          // ... error message ...
          return;
        }

        // Generate a QImage of the rendered page
        QImage image = pdfPage->thumbnail();
        if (image.isNull())
          image = pdfPage->renderToImage(dpi, dpi);
        else
          LOG(LM_INFO, "USING THUMBNAIL");
        if (image.isNull()) {
          // ... error message ...
          continue;
        }
        if (image.width() > maxX)
          maxX = image.width();
        if (image.height() > maxY)
          maxY = image.height();

//        width = image.width();
//        height = image.height();

        LOG(LM_INFO,
            "SZ " << image.width() << " " << image.height() << " " << p.size.width() << "x" << p.size.height());
        QPixmap pix;
        if (pix.convertFromImage(image))
          p.labelThumb->setPixmap(pix);
//    ui->scrollArea->setWidget(label);
// ... use image ...

// after the usage, the page must be deleted
        if (not p.docPage)  // Page wurde lokal erzeugt
          delete pdfPage;
      }
      data->thumbSize = QSize(maxX, maxY);
    }
    thumbReorg();
  } else {
    ui->pushButtonThumb1->setChecked(false);
    ui->stackedWidget->setCurrentIndex(0);

  }

}

void Viewer::thumbReorg() {
  if (data->thumbs) {
    int maxX = data->thumbSize.width() + 6;
    int maxY = data->thumbSize.height() + 6;
    int xMarging = 6;
    int yMArgin = 6;

    int w = ui->scrollAreaThumb->viewport()->width();
    int cols = (w - xMarging) / maxX;
    if (cols < 1)
      cols = 1;

    if (cols != data->thumbCols) {
      data->thumbCols = cols;
      data->thumbs->setMinimumSize(cols * maxX + xMarging, (data->pages.size() + cols - 1) / cols * maxY + yMArgin);

      for (auto &p:data->pages) {
        p.labelThumb->move(xMarging + p.pgNum % cols * maxX, yMArgin + p.pgNum / cols * maxY);
        p.labelThumb->show();
      }
    }
  }
}

void Viewer::showPicture(const QPixmap &pixmap) {
  clearViewer();
  data->pixmap = pixmap;
  QLabel *widget = new QLabel(this);
  widget->setScaledContents(false);
  ui->scrollArea->setWidget(widget);
  int a = ui->scrollArea->viewport()->width()  * 100 / pixmap.width();
  if (a > 100)
    a = 100;

  int w = data->pixmap.width() * a / 100;
  widget->setPixmap(data->pixmap.scaledToWidth(w));
  ui->horizontalSliderZoom->setRange(a / 2, 150);
  ui->horizontalSliderZoom->setValue(a);
}

void Viewer::clearViewer() {
  cancelEditForm();
  data->formFields.clear();
  if (not data->grid)
    return;
  if (auto w = data->grid->widget()) {
    w->hide();
    w->deleteLater();
    for (auto &p:data->pages) {
      delete p.label;
    }
  }
  if (auto w = data->thumbs) {
    w->hide();
    w->deleteLater();
    for (auto &p:data->pages) {
      delete p.labelThumb;
    }
  }
  data->pages.clear();
  data->thumbs = nullptr;
  data->grid = nullptr;
  delete data->document;
  data->document = nullptr;

}


