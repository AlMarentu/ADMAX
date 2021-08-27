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


#ifndef VIEWER_H
#define VIEWER_H

#include <QWidget>
#include <QLabel>

namespace Ui {
class Viewer;
}

class ViewerData;

class Viewer : public QWidget
{
    Q_OBJECT

public slots:
  void setZoom(int);
  void butt();
  void editForm();
  void cancelEditForm();
  void saveEditForm();
  void vRange(int, int);
  void hRange(int, int);
  void vChange(int);
  void hChange(int);
  void setPage(int p);
  void formRepos();
  void thumbnails(bool);

signals:
  void sizeChanged();

public:
    explicit Viewer(QWidget *parent = nullptr);
    ~Viewer();

  void showPdfFile(const QString& file);
  void showPdfBuffer(const QByteArray &data);
  void showPicture(const QPixmap &pixmap);
  void clearViewer();
  void print();
  void saveFile();
private:
  void showDocument();
  void resizeEvent(QResizeEvent *event) override;
  void thumbReorg();
    Ui::Viewer *ui;
    ViewerData *data;

};

class LabelThumb : public QLabel {
Q_OBJECT
public:
  LabelThumb(QWidget *parent, int pg);
signals:
  void selectPage(int);
protected:
  int page;
  void mousePressEvent(QMouseEvent *ev) override;
};

#endif // VIEWER_H
