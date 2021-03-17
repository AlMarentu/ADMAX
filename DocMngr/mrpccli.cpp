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


#include <QTimer>
#include <mobs/converter.h>
#include <iomanip>
#include "mrpccli.h"

#include "mobs/logging.h"
#include "mobs/xmlread.h"
#include "mobs/xmlwriter.h"
#include "mobs/xmlout.h"
#include "mobs/csb.h"
#include "mobs/aes.h"
#include "mobs/rsa.h"
#include "mrpc.h"

inline std::basic_ios<char> &operator<<(std::basic_ios<char> &s, QString q) {
  s << q.toUtf8().data();
  return s;
}



class BlockIstBuf : public std::basic_streambuf<char> {
public:
  using Base = std::basic_streambuf<char>;
  using char_type = typename Base::char_type;
  using Traits = std::char_traits<char_type>;
  using int_type = typename Base::int_type;

  BlockIstBuf() : Base() {}
  void addBuff(QByteArray &&move) {
    if (move.isEmpty())
      return;
//    LOG(LM_DEBUG, "BLOCK READ " << std::string(move.data(), move.size()) << " " << split);
    if (split and false) {
      auto i = move.indexOf('\0');
      if (i >= 0) { // Block bei =-Char aufsplitten
        eob = true;
        LOG(LM_INFO, "SPLIT at " << i);
        split = false;
        if (i == move.size() -1)
          move.resize(i);
        else {
          auto last = move.right(move.size() - i - 1);
          move.resize(i);
          if (i > 0) {
            buffers.emplace_back(qMove(move));
//            LOG(LM_DEBUG, "BLOCK READ " << std::string(move.data(), move.size()));
          }
          buffers.emplace_back(qMove(last));
//          std::stringstream ss;
//          for (auto b:buffers.back()) {
//            ss << " " << std::hex << std::setw(2) << std::setfill('0') << int (u_char (b));
//          }
//
//          LOG(LM_INFO, "BLOCK READ " << ss.str() << " #");
          return;
        }
      }
    }
    std::string s = std::string(move.data(), move.size());
//    LOG(LM_DEBUG, "BLOCK READ " << s);
    buffers.emplace_back(qMove(move));
//    if (not split) {
//      std::stringstream ss;
//      for (auto b:buffers.back()) {
//        ss << " " << std::hex << std::setw(2) << std::setfill('0') << int (u_char (b));
//      }
//      LOG(LM_INFO, "BLOCK READ " << ss.str() << " #");
//    }
//    else
  }

  int_type underflow() override;

  size_t avail() const {
    size_t res = 0;
    bool first = true;
    for (auto const &b:buffers) {
      if (first and initialized)
        res += std::distance(Base::gptr(), Base::egptr());
      else
        res += b.size();
      first = false;
    }
    return res;
  }

  bool findGT() { return findChar('>'); }

  bool findChar(char c) {
    if (buffers.empty())
      return false;
    bool first = true;
    for (auto const &b:buffers) {
      if (first and initialized) {
        if (std::find(Base::gptr(), Base::egptr(), c) != Base::egptr())
          return true;
      } else if (b.indexOf(c) >= 0)
        return true;
      first = false;
    }
    return false;
  }

  bool blockFinished() {
    if (not eob)
      eob = findChar('\0');
    return eob;
  }

  void startNewBlock() {
    LOG(LM_INFO, "startNewBlock");
    split = true;
    eob = false;
  }

//protected:
//  std::streamsize showmanyc() {
//    return avail();
//  }

 private:
  std::list<QByteArray> buffers;
  bool initialized = false;
  bool eob = false;   // '\0' found
  bool split = true;
};

BlockIstBuf::int_type BlockIstBuf::underflow() {
  if (buffers.empty())
    return Traits::eof();
  if (initialized)
    buffers.erase(buffers.begin());
  initialized = true;
  if (buffers.empty())
    return Traits::eof();

  if (split) {
    auto idx = buffers.front().indexOf('\0');
    if (idx >= 0) {
      auto last = buffers.front().right(buffers.front().size() - idx - 1);
      LOG(LM_DEBUG, "rest " << last.size());
      if (last.size() > 0)
        buffers.insert(std::next(buffers.begin()), last);
      buffers.front().resize(idx);
      split = false;
      if (idx == 0) {
        buffers.erase(buffers.begin());
        return underflow();
      }
      LOG(LM_DEBUG, "READ S " << std::string(buffers.front().data(), buffers.front().size()));
    }
    else
      LOG(LM_DEBUG, "READ N " << std::string(buffers.front().data(), buffers.front().size()));
  }
  Base::setg(buffers.front().data(), buffers.front().data(), buffers.front().data() + buffers.front().size());
  LOG(LM_DEBUG, "new Buffer " << Base::egptr() - Base::gptr());
  return Traits::to_int_type(*Base::gptr());
}



class XmlInput : public mobs::XmlReader {
public:
  explicit XmlInput(std::wistream &str) : XmlReader(str) { }
  ~XmlInput() { delete objReturn; }

  void StartTag(const std::string &element) override {
    LOG(LM_INFO, "start " << element);
    // Wenn passendes Tag gefunden, dann Objekt einlesen
    auto o = mobs::ObjectBase::createObj(element);
    if (o)
      fill(o);
  }
  void EndTag(const std::string &element) override {
    LOG(LM_INFO, "end " << element <<  " " << level());
    ready = (level() <= 2);
  }
  void Encrypt(const std::string &algorithm, const std::string &keyName, const std::string &cipher, mobs::CryptBufBase *&cryptBufp) override {
    LOG(LM_INFO, "Encryption " << algorithm << " keyName " << keyName << " cipher " << cipher);
    if (algorithm == "aes-256-cbc") {
      cryptBufp = new mobs::CryptBufAes(sessionKey);
      encryptedInput = true;
      stop();
    }
  }
  void EncryptionFinished() override {
    LOG(LM_INFO, "Encryption finished " << level());
    encryptedInput = false;
    skipDelim = true;
    // weiteres parsen abbrechen
    stop();
  }
  void filled(mobs::ObjectBase *obj, const std::string &error) override {
    if (not error.empty()) {
      LOG(LM_ERROR, "Parser error " << error);
      delete obj;
      errorMsg = error;
      stop();
      return;
    }
//    LOG(LM_INFO, "filled " << obj->to_string() << (error.empty() ? " OK":" ERROR = ") << error);
    if (objReturn) {
      delete objReturn;
      objReturn = nullptr;
    }
    if (auto *sess = dynamic_cast<SessionError *>(obj)) {
      LOG(LM_ERROR, "SESSIONERROR " << sess->error.toStr(mobs::ConvObjToString()));
      if (sess->error() == SErrNeedCredentioal)
      {
        XmlInput::sessionKey.clear();
        XmlInput::sessionId = 0;
      }
      // parsen abbrechen
      stop();
    } else if (auto *sess = dynamic_cast<SessionResult *>(obj)) {
      LOG(LM_INFO, "SESSIORESULT " << sess->to_string());
      sessionId = sess->id();
      // Session-Key mit privatem Schlüssel entschlüsseln
      mobs::decryptPrivateRsa(sess->key(), sessionKey, "keystore/client_priv.pem", "12345");
      // parsen abbrechen
      stop();
    } else {
      objReturn = obj;
      stop();
      return;
    }
    delete obj;
//    stop(); // optionaler Zwischenstop
  }

  mobs::ObjectBase *objReturn = nullptr;
  bool encryptedInput = false;
  bool ready = false;
  bool skipDelim = false;
  bool newBlock = false;

  std::string errorMsg;

  static uint sessionId;
  static std::vector<u_char> sessionKey;

};
uint XmlInput::sessionId = 0;
std::vector<u_char> XmlInput::sessionKey;

class MrpcClientData {
public:
  enum ConState {Connecting, Authenticating, Online, SessionClosed, Error };
  MrpcClientData() : streambufO(ostr), x2out(&streambufO), xf(x2out, mobs::XmlWriter::CS_utf8, true),
  iBstr(&iBlkStr), streambufI(iBstr), x2in(&streambufI), xr(x2in) {
    xr.readTillEof(false);
    iBstr.unsetf(std::ios::skipws);


//    std::locale loc(std::locale::classic(), new codec_binary);
//    iBstr.imbue(loc);
  }
  ~MrpcClientData() {

  }
  void setPercent(int percent) {
    percentStart = percentEnd;
    percentEnd = percent;
  }

  std::ostringstream ostr{};
  mobs::CryptOstrBuf streambufO;
  std::wostream x2out;
  mobs::XmlWriter xf;

  BlockIstBuf iBlkStr{};
  std::istream iBstr;
  mobs::CryptIstrBuf streambufI;
  std::wistream x2in;
  XmlInput xr;

  mobs::CryptBufAes *crypt = nullptr;
  std::istream *attachmentStream = nullptr;
  std::vector<u_char> attachment;
  int64_t attachmentSize = 0;

  QFile *sendFile = nullptr;

  ConState state = Connecting;
  int percentStart = 0;
  int percentEnd = 0;
};

MrpcClient::MrpcClient(QWidget *parent) : QObject() {
  LOG(LM_INFO, "MrpcClient");
  data = new MrpcClientData;

  eventLoop = new QEventLoop(this);

  progress = new QProgressDialog(tr("Verbindung"), tr("Abbruch"), 0, 100, parent);
  connect(progress, SIGNAL(canceled()), this, SLOT(canceled()));
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(800);
  progress->setValue(0);

  socket = new QTcpSocket();
  connect(socket, SIGNAL(connected()), this, SLOT(connected()));
  connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
  connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
  connect(socket, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWritten(qint64)));
  connect(socket, SIGNAL(errorOccurred(QAbstractSocket::SocketError)), this, SLOT(errorOccurred(QAbstractSocket::SocketError)));
  socket->connectToHost("localhost", 4444);

}

MrpcClient::~MrpcClient() {
  LOG(LM_INFO, "MrpcClient destruktor ");
  socket->deleteLater();
  eventLoop->deleteLater();
  if (progress)
    progress->deleteLater();
  delete data;
}



void MrpcClient::close() {
  if (socket->isOpen())
    socket->abort();
  if (data->state != MrpcClientData::Error)
    data->state = MrpcClientData::SessionClosed;
  else if (progress) {
    progress->setValue(progress->maximum());
    progress->deleteLater();
    progress = nullptr;
  }
  deleteLater();
}

void MrpcClient::flush() {
  data->xf.sync();
  const std::string &o = data->ostr.str();
//  LOG(LM_DEBUG, "->" << o);
  if (data->state <= MrpcClientData::Online) {
    auto sz = socket->write(o.c_str(), o.length());
    if (sz == -1) {
      LOG(LM_ERROR, "Write-Error");
      data->xr.errorMsg = "write error";
      socket->abort();
    }
    if (sz != o.length()) {
      LOG(LM_ERROR, "PART " << sz << " of " << o.length());
      data->xr.errorMsg = "write incpomplete";
      socket->abort();
    }
  }
  data->ostr.str("");
  data->ostr.clear();
}

void MrpcClient::sendLogin() {
  data->state = MrpcClientData::Authenticating;
  SessionLoginData sess;
  sess.login("client");
  sess.software("mrpcclient");

  std::string buffer = sess.to_string(mobs::ConvObjToString().exportJson().noIndent());
  std::vector<u_char> inhalt;
  copy(buffer.begin(), buffer.end(), back_inserter(inhalt));
  std::vector<u_char> cipher;
  mobs::encryptPublicRsa(inhalt, cipher, "keystore/server.pem");
  SessionLogin login;
  login.cipher(cipher);

  mobs::ConvObjToString cth;
  mobs::XmlOut xo(&data->xf, cth);
  login.traverse(xo);
//    xf.writeTagEnd();

  data->xf.sync();
  LOG(LM_INFO, "login gesendet");

  flush();


}

void MrpcClient::connected() {
  LOG(LM_INFO, "connected");
//  std::ostringstream ostr;
//  mobs::CryptOstrBuf streambufO(ostr);
//  std::wostream x2out(&streambufO);
//  mobs::XmlWriter xf(x2out, mobs::XmlWriter::CS_utf8, true);
  mobs::ConvObjToString cth;
  mobs::XmlOut xo(&data->xf, cth);
  data->xf.writeHead();
  data->xf.writeTagBegin(L"methodCall");

  if (XmlInput::sessionId == 0) {
    sendLogin();
  }
  else {
    LOG(LM_INFO, "Session Id = " << XmlInput::sessionId);
    data->state = MrpcClientData::Online;
    Session sess;
    sess.id(XmlInput::sessionId);
    sess.traverse(xo);
    data->xr.ready = true;
    eventLoop->exit(0);
  }

}

void MrpcClient::disconnected() {
  LOG(LM_INFO, "disconnected");
  if (data->state != MrpcClientData::Error)
    data->state = MrpcClientData::SessionClosed;
  eventLoop->exit(0);
}

void MrpcClient::bytesWritten(qint64 bytes) {
  LOG(LM_INFO, "BytesWritten " << bytes << " rest " << socket->bytesToWrite());
}

void MrpcClient::attachment(QFile *f) {
  LOG(LM_INFO, "Start attachment");

  if (not f->open(QIODevice::ReadOnly)) {
    error();
    THROW("file not opn");
  }
  std::vector<u_char> iv;
  iv.resize(mobs::CryptBufAes::iv_size());
  mobs::CryptBufAes::getRand(iv);
  mobs::CryptBufAes cry(data->xr.sessionKey, iv, "", true);
  data->ostr << '\0';
  cry.setOstr(data->ostr);
  data->sendFile = f;

  while (not f->atEnd()) {
    auto data = f->read(4096);
    if (data.isEmpty()) {
      error();
      THROW("EOF");
    }
    LOG(LM_INFO, "FILEBUF " << data.size());
    cry.sputn(data.data(), data.size());
  }
  cry.finalize();
  flush();
  data->sendFile->close();
  data->sendFile = nullptr;


  //    std::streamsize a = xi.streambufO.getOstream().tellp();

}


int MrpcClient::exec() {
  LOG(LM_INFO, "EXEC start");
  int r = 0;
  if (data->xr.errorMsg.empty()) {
    if (data->state != MrpcClientData::Error)
      QTimer::singleShot(0, this, SLOT(readyRead()));
    LOG(LM_INFO, "start event loop");
    r = eventLoop->exec(QEventLoop::WaitForMoreEvents);
    if (not XmlInput::sessionId and data->state == MrpcClientData::Online and data->xr.errorMsg.empty()) {
      LOG(LM_INFO, "resend login");
      sendLogin();
      r = eventLoop->exec(QEventLoop::WaitForMoreEvents);
      if (XmlInput::sessionId and data->xr.errorMsg.empty()) {
        data->state = MrpcClientData::Online;
        LOG(LM_INFO, "login ok");
        r = 99;
      }
      else if (data->xr.errorMsg.empty())
        data->xr.errorMsg = u8"login error";
      }
  }
   LOG(LM_INFO, "EXEC ende");
  if (not data->xr.errorMsg.empty()) {
    error();
    THROW("Error " << data->xr.errorMsg);
  }
  return r;
}

void MrpcClient::readyRead() {
  LOG(LM_INFO, "readyRead");
  try {
    while (data->state <= MrpcClientData::Online and not socket->atEnd()) {
      QByteArray bytes = socket->read(128*1024); // 8192 = 3.5s, 1024 = 25.5s, 64k 1.2s, 256k = 0.7sec auf localhost
//      LOG(LM_DEBUG, "READ " << std::string(bytes.data(), bytes.size()));
      LOG(LM_INFO, "READ " << bytes.size());
      data->iBlkStr.addBuff(qMove(bytes));
    }

    if (data->attachmentStream and not data->xr.encryptedInput) {
      auto avail = data->iBlkStr.avail();
      LOG(LM_INFO, "Attachment wait avail " << avail);
      int p = data->percentStart +
              (data->percentEnd - data->percentStart) * data->attachment.size() / data->attachmentSize;
      if (avail > 0 and data->attachment.empty())
        avail--;  // '\0' char
      if (progress and p < 100)
        progress->setValue(p);
      while (data->state == MrpcClientData::SessionClosed or
              (avail = data->iBlkStr.avail()) >= 2048 or avail >= data->crypt->getLimitRemain()) {
        char c;
//        if (data->attachment.empty()) {
//          c = data->iBstr.get();
//          while (c != '\0') {
//            LOG(LM_ERROR, "'\\0' expected " << int(c) << " " << c);
//            c = data->iBstr.get();
//          }
//        }
        if (data->attachmentStream->get(c).eof()) {
          if (data->attachmentStream->bad()) {
            data->xr.errorMsg = "attachment read error";
            LOG(LM_ERROR, "Attachment read error");
          }
          else
            LOG(LM_INFO, "Attachment DONE");
          data->iBlkStr.startNewBlock();

          delete data->attachmentStream;
          data->attachmentStream = nullptr;
          if (eventLoop)
            eventLoop->exit();
          return;
        }
        data->attachment.push_back(c);
      }
      LOG(LM_INFO, "Attachment stop avail " << data->iBlkStr.avail());
      return;
    }

    bool findGT = false;
    bool eob = data->state == MrpcClientData::SessionClosed or data->iBlkStr.blockFinished();
    if (data->state <= MrpcClientData::Online and not (eob = data->iBlkStr.blockFinished()) and
        not (findGT = data->iBlkStr.findGT())) { // avail() < 128
      LOG(LM_INFO, "Parse wait avail " << data->iBlkStr.avail() << " GT " << findGT);
      return;
    }
    if (not eob and data->state == MrpcClientData::Online) {
      LOG(LM_INFO, "Parse wait avail2 " << data->iBlkStr.avail() << " GT " << findGT);
      return;
    }

    LOG(LM_INFO, "Parse start avail " << data->iBlkStr.avail() << " GT " << findGT << " EOB " << eob << " state " << int(data->state));
//    std::cout << "Parse start ===========\n";
    if (not eob and data->xr.encryptedInput)  // wenn kein Blockende, dann sicherheitshalber nach jedem Token stoppen
      data->xr.stop();
    data->xr.parse();
    if (data->xr.newBlock) {
      data->iBlkStr.startNewBlock();
      data->xr.newBlock = false;
    }
    if (data->xr.eot()) {
      LOG(LM_INFO, "Parse Ende erreicht " << " " << data->xr.level());
      socket->disconnect();
      if (data->state != MrpcClientData::Error)
        data->state = MrpcClientData::SessionClosed;
      data->xr.ready = true;
//      if (progress)
//        progress->setValue(progress->maximum());
      if (data->iBlkStr.avail()) {
        LOG(LM_ERROR, "Data avail at last Token " << data->iBlkStr.avail());
        THROW("Extra Data at eot");
      }
      // TODO aufräumen
    }
    else if (data->state == MrpcClientData::SessionClosed and data->iBlkStr.avail()) {
      LOG(LM_DEBUG, "Session closed before last Token");
    } else if (data->state == MrpcClientData::Authenticating and not data->xr.sessionKey.empty())
      data->state = MrpcClientData::Online;
    LOG(LM_INFO, "Parse ende remain " << data->iBlkStr.avail());
  } catch (std::exception &e) {
    LOG(LM_ERROR, "Exception " << e.what());
    data->xr.errorMsg = e.what();
  }

  eventLoop->exit(0);
}

void MrpcClient::errorOccurred(QAbstractSocket::SocketError e) {
  LOG(LM_INFO, "error " << int(e) /*<< " " << socket->errorString()*/);
  if (e == QAbstractSocket::HostNotFoundError) {
    data->xr.errorMsg = "host not found";
    data->state = MrpcClientData::Error;
    eventLoop->exit(0);
  } else if (e == QAbstractSocket::RemoteHostClosedError) {
    LOG(LM_INFO, "Remote Host Closed");
//    if (data->state < MrpcClientData::Online) {
//      data->state = MrpcClientData::Error;
//      data->xr.errorMsg = "remote host closed";
//      data->xr.sessionId = 0;
//      eventLoop->exit(0);
//    }
  } else {
    data->xr.errorMsg = "unknown error";
    data->state = MrpcClientData::Error;
    eventLoop->exit(0);
  }
}

void MrpcClient::canceled() {
  LOG(LM_INFO, "canceled");
  data->state = MrpcClientData::Error;
  socket->abort();
  progress = nullptr;
  data->xr.errorMsg = "cancelled";
  eventLoop->exit(0);

}

void MrpcClient::error() {
  LOG(LM_INFO, "error");
  data->state = MrpcClientData::Error;
  socket->abort();
  if (progress) {
    progress->setValue(progress->maximum());
    progress->deleteLater();
    progress = nullptr;
  }
}

void MrpcClient::send(const mobs::ObjectBase *obj) {
  if (data->state > MrpcClientData::Online or not obj)
    return;
//  try {
    LOG(LM_INFO, "send " << obj->to_string());
    data->xr.ready = false;
    mobs::ConvObjToString cth;
    mobs::XmlOut xo(&data->xf, cth);

    std::vector<u_char> iv;
    iv.resize(mobs::CryptBufAes::iv_size());
    mobs::CryptBufAes::getRand(iv);

    data->xf.startEncrypt(new mobs::CryptBufAes(XmlInput::sessionKey, iv, "", true));

    // Objekt schreiben

    obj->traverse(xo);

    data->xf.stopEncrypt();
    // Listen-Tag schließen
//    data->xf.writeTagEnd();

    // file schließen
//    x2out << mobs::CryptBufBase::base64(false);

//    x2out << mobs::CryptBufBase::final();
    data->streambufO.finalize();
    flush();
    LOG(LM_INFO, "send done");
//  } catch (std::exception &e) {
//    LOG(LM_ERROR, "Exception " << e.what());
//  }
}

mobs::ObjectBase *MrpcClient::sendAndWaitObj(const mobs::ObjectBase *obj, int percent) {
  LOG(LM_INFO, "SEND AND WAIT NEXT begin");
  data->setPercent(percent);
  send(obj);
  data->iBlkStr.startNewBlock();
  for (;;) {
    int r = exec();
    if (r == 99) {
      LOG(LM_INFO, "RELOG");
      data->state = MrpcClientData::Online;
      send(obj);
      continue;
    }
    auto tmp = data->xr.objReturn;
    data->xr.objReturn = nullptr;
    if (tmp) {
      LOG(LM_INFO, "WAIT NEXT end");
      if (progress)
        progress->setValue(data->percentEnd);
      return tmp;
    }
  }
}

mobs::ObjectBase *MrpcClient::execNextObj(int percent) {
  LOG(LM_INFO, "WAIT NEXT begin");
  data->setPercent(percent);
  data->iBlkStr.startNewBlock();
  for (;;) {
    exec();
    auto tmp = data->xr.objReturn;
    data->xr.objReturn = nullptr;
    if (tmp) {
      LOG(LM_INFO, "WAIT NEXT end");
      if (progress)
        progress->setValue(data->percentEnd);
      return tmp;
    }
  }
}

void MrpcClient::waitDone() {
  LOG(LM_INFO, "WAIT DONE begin");
  // Listen-Tag schließen
  data->xf.writeTagEnd();

  // file schließen
//    x2out << mobs::CryptBufBase::base64(false);

//  data->streambufO.finalize();
  flush();
  LOG(LM_INFO, "waitDone done");


  data->setPercent(99);
  data->iBlkStr.startNewBlock();
  while (data->state != MrpcClientData::Error and not data->xr.eot()) {
    exec();
  }
  if (progress) {
    progress->setValue(progress->maximum());
    progress->deleteLater();
    progress = nullptr;
  }
  LOG(LM_INFO, "WAIT DONE end");
}

void MrpcClient::waitReady(int percent) {
  LOG(LM_INFO, "WAIT READY begin");
  data->setPercent(percent);
//  data->iBlkStr.startNewBlock();
  if (data->state == MrpcClientData::SessionClosed and data->iBlkStr.avail() == 0) {
    if (data->xr.eot())
      return;
    error();
    THROW("message truncated");
  }
  while (data->state != MrpcClientData::Error and not data->xr.ready)
    exec();
  if (progress)
    progress->setValue(data->percentEnd);

  LOG(LM_INFO, "WAIT READY end");
}

u_char *MrpcClient::getAttachment(int64_t sz, int percent) {
  LOG(LM_INFO, "WAIT ATTACHMENT begin " << sz << " " << mobs::CryptBufAes::iv_size() + (sz + 16) / 16 * 16);
  data->setPercent(percent);
//  while (data->xr.encryptedInput) {// Auf Ende XML warten
//    LOG(LM_INFO, "Warte auf Encryption End");
//    exec();
//  }
  data->attachmentSize = sz;
  std::vector<u_char> iv;
  iv.resize(mobs::CryptBufAes::iv_size());
  mobs::CryptBufAes::getRand(iv);
  data->crypt = new mobs::CryptBufAes(data->xr.sessionKey, iv, "", true);
  data->crypt->setIstr(data->iBstr);
  data->attachmentStream = new std::istream(data->crypt);
  data->crypt->setReadLimit(mobs::CryptBufAes::iv_size() + (sz + 16) / 16 * 16);
  while (data->attachmentStream) {
    exec();
  }
//  data->iBstr.setf(std::ios::skipws);
  bool ok = not data->crypt->bad();
  delete data->crypt;
  data->crypt = nullptr;
  if (not ok) {
    error();
    THROW("Attachment decrypt failed");
  }
  if (data->attachment.size() != sz) {
    error();
    THROW("Attachment size mismatch " << data->attachment.size());
  }
  if (progress)
    progress->setValue(data->percentEnd);
  LOG(LM_INFO, "WAIT ATTACHMENT end " << sz << " buf " << mobs::CryptBufAes::iv_size() + (sz + 16) / 16 * 16);
  return &data->attachment[0];
}



