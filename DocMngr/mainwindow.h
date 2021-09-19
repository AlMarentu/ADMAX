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


#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>
#include <QtNetwork/QTcpSocket>
#include <mrpc.h>

#include "mrpccli.h"

namespace Ui {
class MainWindow;
}

class ActionTemplate;
class QTreeWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

  void loadDocument(int64_t doc);
  void initTags(int templateId);

public slots:
  void server();
  void load();
  void save();
  void print();
  void loadFile();
  void saveFile();
  void getDocument();
  void searchDocument();
  void searchRowClicked(int row, int col);
  void searchRowClicked(QTreeWidgetItem*,int);
  void initKey();
  void changePass();
  void getConfiguration();

private:
  Ui::MainWindow *ui;
  MrpcClient *mrpc = nullptr;
  QString currentFile;
  std::map<int, ActionTemplate> actionTemplates;

  void initTags(const TemplateInfo &templateInfo);

};

#endif // MAINWINDOW_H
