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


#ifndef DOCMNGR_MRPCCLI_H
#define DOCMNGR_MRPCCLI_H

#include "mobs/objgen.h"

#include <QObject>
#include <QtNetwork>
#include <QProgressDialog>
#include <QEventLoop>

class MrpcClientData;

class MrpcClient : public QObject {
  Q_OBJECT

public:
  explicit MrpcClient(QWidget *parent = nullptr);
  ~MrpcClient();

  int exec();
  void close();
  void waitReady(int percent);
  void waitDone();
  mobs::ObjectBase *sendAndWaitObj(const mobs::ObjectBase *obj, int percent);
  mobs::ObjectBase *execNextObj(int percent);

  u_char *getAttachment(int64_t sz, int percent);

  QElapsedTimer elapsed;


public slots:
  void connected();
  void disconnected();
  void readyRead();
  void bytesWritten(qint64 bytes);
  void errorOccurred(QAbstractSocket::SocketError);
  void canceled();

  void send(const mobs::ObjectBase *obj);
  void attachment(QFile *f);

private:
  QTcpSocket *socket = nullptr;
  QProgressDialog *progress = nullptr;
  QEventLoop *eventLoop = nullptr;
  MrpcClientData *data = nullptr;


  void sendLogin();
  void flush();


  void error();
};


#endif //DOCMNGR_MRPCCLI_H
