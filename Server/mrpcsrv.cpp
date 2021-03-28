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


#include "mobs/logging.h"
#include "mobs/objgen.h"
#include "mobs/xmlout.h"
#include "mobs/xmlwriter.h"
#include "mobs/xmlread.h"
#include "mobs/csb.h"
#include "mobs/aes.h"
#include "mobs/rsa.h"
#include "mobs/tcpstream.h"
#include "mobs/converter.h"
#include "mrpc.h"
#include "filestore.h"
#include <fstream>
#include <array>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <sys/stat.h>

using namespace std;

#define AES_BYTES(fileSize) (mobs::CryptBufAes::iv_size() + (fileSize + 16) / 16 * 16)

class SessionContext;
class XmlInput;

class MRpcServer {
public:
  std::string service = "4444";
  std::string name = "server";
  std::string keystorePath = "keystore";
  // TODO passphrase

  void server();

  void genKey() const {
    mobs::generateRsaKey(STRSTR(keystorePath << '/' << name << "_priv.pem"),
                         STRSTR(keystorePath << '/' << name << ".pem"), "12345");
  }

  SessionContext *getSession(u_int id);
  SessionContext *newSession(u_int &id, const std::string &login);

protected:
  static void worker_thread(int id, MRpcServer *);
  mobs::TcpAccept tcpAccept;
  map<u_int, SessionContext> sessions;
  u_int sessCntr = 0;
  mutex m;

};


class MrpcObject {
public:
  virtual void execute(mobs::XmlOut &xmlOut, XmlInput &xi) {
    THROW("no action");
  }
  virtual void executeAttachment(mobs::CryptBufAes &crypt, mobs::XmlOut &xmlOut, XmlInput &xi)  {
    THROW("no action");
  }


};

class SessionContext {
public:
  SessionContext(u_int id, std::string  l, std::vector<u_char>  k) : sessionId(id), login(std::move(l)), key(std::move(k)) {}
  u_int sessionId;
  string login;
  vector<u_char> key;

  void enter();
  void release();

};



SessionContext *MRpcServer::getSession(u_int id) {
  std::lock_guard<std::mutex> lock(m);
  auto it = sessions.find(id);
  if (it != sessions.end()) {
    it->second.enter();
    return &it->second;
  }
  return nullptr;
}

SessionContext *MRpcServer::newSession(u_int &id, const std::string &login) {
  // Key außerhalb mutex erzeugen
  vector<u_char> k;
  k.resize(mobs::CryptBufAes::key_size());
  mobs::CryptBufAes::getRand(k);

  std::lock_guard<std::mutex> lock(m);
  id = ++sessCntr;
  auto it = sessions.insert(map<u_int, SessionContext>::value_type(id, SessionContext(id, login, k))).first;
  if (it != sessions.end()) {
    LOG(LM_INFO, "CREATE " << id);
    it->second.enter();
    return &it->second;
  }
  return nullptr;
}

// innerhalb mutex
void SessionContext::enter() {
  LOG(LM_INFO, "ENTER " << sessionId);
}

void SessionContext::release() {
  LOG(LM_INFO, "RELEASE " << sessionId);
}


// Worker-Context mit XML-Parser
class XmlInput : public mobs::XmlReader {
public:
  explicit XmlInput(wistream &str, mobs::XmlWriter &res, mobs::CryptIstrBuf &stbi, mobs::CryptOstrBuf &stbo, int tId, MRpcServer *s)
          : XmlReader(str), xmlResult(res), streambufI(stbi), streambufO(stbo), taskId(tId), server(s) { }
  ~XmlInput() { if (ctx) ctx->release(); }

  void StartTag(const std::string &element) override {
    LOG(LM_INFO, "start " << element);
    // Wenn passendes Tag gefunden, dann Objekt einlesen
    auto o = mobs::ObjectBase::createObj(element);
    if (o)
      fill(o);
  }
  void EndTag(const std::string &element) override {
    LOG(LM_INFO, "end " << element);
  }
  void Encrypt(const std::string &algorithm, const std::string &keyName, const std::string &cipher, mobs::CryptBufBase *&cryptBufp) override {
    LOG(LM_INFO, "Encryption " << algorithm << " keyName " << keyName << " cipher " << cipher);
    if (algorithm == "aes-256-cbc") {
      if (not ctx) {
        cryptBufp = new mobs::CryptBufNull;
        return;
        THROW("no sessions");
      }
      vector<u_char> iv;
      iv.resize(mobs::CryptBufAes::iv_size());
      mobs::CryptBufAes::getRand(iv);
      cryptBufp = new mobs::CryptBufAes(ctx->key);
      encryptedInput = true;
    }
  }
  void EncryptionFinished() override {
    LOG(LM_INFO, "Encryption finished");
    encryptedInput = false;
    // weiteres parsen abbrechen
    stop();
  }
  void filled(mobs::ObjectBase *obj, const string &error) override {
    LOG(LM_INFO, "filled " << obj->to_string() << (error.empty() ? " OK":" ERROR = ") << error);
    mobs::ConvObjToString cth;
    mobs::XmlOut xo(&xmlResult, cth);
    if (error.empty()) {
      if (auto *sess = dynamic_cast<SessionLogin *>(obj)) {
        LOG(LM_INFO, "LOGIN " << sess->cipher().size());
        std::vector<u_char> inhalt;
        mobs::decryptPrivateRsa(sess->cipher(), inhalt, STRSTR(server->keystorePath << '/' << server->name << "_priv.pem"), "12345");
        string buf((char *)&inhalt[0], inhalt.size());
        SessionLoginData data;
        mobs::string2Obj(buf, data, mobs::ConvObjFromStr());
        LOG(LM_DEBUG, "INFO = " << data.to_string());

        u_int id;
        if (not(ctx = server->newSession(id, data.login())))
          THROW("no more sessions");
        SessionResult result;
        result.id(id);

        string keyFile = STRSTR(server->keystorePath << '/' << ctx->login << ".pem");
        vector<u_char> cipher;
        // Der Session-Key wird mit dem privaten Schlüssel des Clients codiert
        mobs::encryptPublicRsa(ctx->key, cipher, keyFile);
        result.key(cipher);

        result.traverse(xo);
//        if (needDelimiter)
//          xmlResult.putc(0);
        xmlResult.sync();

      } else if (auto *sess = dynamic_cast<Session *>(obj)) {
        LOG(LM_INFO, "SESSION = " << sess->id());
        if (ctx and ctx->sessionId != sess->id())
          THROW("session already assigned");
        if (not ctx)
          ctx = server->getSession(sess->id());
        if (not ctx) {
          LOG(LM_ERROR, "missing sessionId");
          SessionError error1;
          error1.error(SErrNeedCredentioal);
          error1.traverse(xo);
          if (needDelimiter)
            xmlResult.putc(0);
          xmlResult.sync();
          // weiteres parsen abbrechen
          stop();
        }
      } else if (not ctx) {
        THROW("invalid sessionId");
      }
      else if (ctx->login.empty()) {
        SessionError error1;
        error1.error(SErrNeedCredentioal);
        error1.traverse(xo);
        // weiteres parsen abbrechen
        stop();
      } else if (auto mrpc = dynamic_cast<MrpcObject *>(obj)) {
        needEncryption();

        mrpc->execute(xo, *this);

        if (attachmentObj)
          return;
       }
      else
        THROW("no MRpc object");
    }
    else
      THROW(error);

    LOG(LM_INFO, "endEncryption; finish=" << finish);
    endEncryption();

    delete obj;
  }

  void endEncryption(bool sync = true) {
    if (not encryptedOutput)
      return;
    xmlResult.stopEncrypt();
    encryptedOutput = false;
    if (needDelimiter) {
      LOG(LM_INFO, "WRITE DELIMITER");
      xmlResult.putc(0);
    }
    if (sync)
      xmlResult.sync();
//    streambufO.getOstream() << '\0';

  }
  void needEncryption() {
    if (encryptedOutput)
      return;
    vector<u_char> iv;
    iv.resize(mobs::CryptBufAes::iv_size());
    mobs::CryptBufAes::getRand(iv);
    xmlResult.startEncrypt(new mobs::CryptBufAes(ctx->key, iv, "", true));
    encryptedOutput = true;
  }


  mobs::XmlWriter &xmlResult;
  mobs::CryptIstrBuf &streambufI;
  mobs::CryptOstrBuf &streambufO;
  int taskId;
  MRpcServer *server;
  bool encryptedOutput = false;
  bool finish = false;
  bool needDelimiter = true;
  bool encryptedInput = false;
  SessionContext *ctx = nullptr;
  MrpcObject *attachmentObj = nullptr;
};







//mutex MRpcServer::m;
//map<u_int, SessionContext> MRpcServer::sessions;


//Objektdefinitionen
class Ping : virtual public mobs::ObjectBase, virtual public MrpcObject
{
public:
  ObjInit(Ping);


  MemVar(int, id, KEYELEMENT1);
  MemVar(int, cnt);

  void execute(mobs::XmlOut &xmlOut, XmlInput &xi) override {
    LOG(LM_INFO, "Send duplicate");
    cnt(cnt()+1);
    this->traverse(xmlOut);
  }
};
ObjRegister(Ping);



MOBS_ENUM_DEF(DocumenType, DocumentUnknown, DocumentPdf, DocumentJpeg, DocumentTiff, DocumentHtml, DocumentText);
MOBS_ENUM_VAL(DocumenType, "unk",           "pdf",       "jpg",        "tif",        "htm",        "txt");

class DocumentTags : virtual public mobs::ObjectBase
{
public:
  ObjInit(DocumentTags);

  MemVar(string, name);
  MemVar(string, content);
};

class Document : virtual public mobs::ObjectBase
{
public:
  ObjInit(Document);

  MemVar(uint64_t, docId);
  MemMobsEnumVar(DocumenType, type);
  MemVar(string, name);
  MemVar(vector<u_char>, content);

};


class DocumentRaw : virtual public mobs::ObjectBase
{
public:
  ObjInit(DocumentRaw);

  MemVar(uint64_t, docId);
  MemMobsEnumVar(DocumenType, type);
  MemVar(string, name);
  MemVar(int64_t, size);
};

class CCBuf : public std::basic_streambuf<char> {
public:
  using Base = std::basic_streambuf<char>;
  using char_type = typename Base::char_type;
  using Traits = std::char_traits<char_type>;
  using int_type = typename Base::int_type;


  explicit CCBuf(std::vector<u_char> &buf) {
    Base::setp((char *)&buf[0], (char *)&buf[buf.size()]);
  }

  /// \private
  int_type overflow(int_type ch) override {
    // nachfassen nicht erlaubt
    LOG(LM_ERROR, "buff overflow");
    return Traits::eof();
  }
};


/// Anforderung Document attached
class GetDocument : virtual public mobs::ObjectBase, virtual public MrpcObject
{
public:
  ObjInit(GetDocument);

  MemVar(uint64_t, docId);
  MemVar(std::string, type);
  MemVar(bool, allowAttach);

  void execute(mobs::XmlOut &xmlOut, XmlInput &xi) override {
    TRACE("");

    auto store = Filestore::instance();
    auto sz = store->docSize(docId());
    DocType dt = store->getType(docId());
    DocumenType docType;
    switch(dt) {
      case DocUnk:
        docType = DocumentUnknown;
        break;
      case DocPdf:
        docType = DocumentPdf;
        break;
      case DocJpeg:
        docType = DocumentJpeg;
        break;
      case DocTiff:
        docType = DocumentTiff;
        break;
      case DocHtml:
        docType = DocumentHtml;
        break;
      case DocText:
        docType = DocumentText;
        break;
    }
    fstream file;
    store->openDocument(docId(), file, false);
//    struct stat stat_buf;
//    if (stat(name().c_str(), &stat_buf))
//      THROW("file not found");
//    std::streamsize sz = stat_buf.st_size;
//    ifstream file(name(), ios::binary);
    if (not file.is_open())
      THROW("file not found");

    if (sz > 8000 and allowAttach()) {
      DocumentRaw doc;

//    sz = 81;
//    doc.name(name());
      doc.docId(docId());
      doc.size(sz);
      doc.type(docType);

      doc.traverse(xmlOut);
      LOG(LM_INFO, "endEncryption;");
      xi.endEncryption();

      LOG(LM_INFO, "Start attachment type=" << doc.type.toStr(mobs::ConvToStrHint(false)));
      vector<u_char> iv;
      iv.resize(mobs::CryptBufAes::iv_size());
      mobs::CryptBufAes::getRand(iv);
      mobs::CryptBufAes cry(xi.ctx->key, iv, "", true);
//    xi.streambufO.getOstream().unsetf(ios::skipws);
      cry.setOstr(xi.streambufO.getOstream());
      ostream ostb(&cry);
      std::streamsize a = xi.streambufO.getOstream().tellp();

      ostb << file.rdbuf();
      ostb.flush();

//    while (sz) {
//      array<wchar_t, 16*1024> b;
//      file.read(&b[0], std::min(std::streamsize(b.size()), sz));
//      if (file.bad())
//        THROW("Lesefehler");
//
//      for (auto i = file.gcount(); i > 0; i--)
//
//      std::streamsize r = ob.sputn(&b[0], file.gcount());
//      LOG(LM_INFO, "Write File " << file.gcount() << " " << r);
//      sz -= file.gcount();
//    }
      file.close();
      cry.finalize();
      std::streamsize b = xi.streambufO.getOstream().tellp();
      if (b - a != AES_BYTES(sz))
        LOG(LM_ERROR, "Error in size: written " << b - a << " <> calculated " << AES_BYTES(sz));
      LOG(LM_INFO, "WRITTEN: " << a << " + " << b - a);

//      auto *tp = dynamic_cast<mobs::TcpStBuf *>(xi.streambufO.getOstream().rdbuf());
//      LOG(LM_INFO, "CHECK STATE " << tp);
//      if (tp and tp->bad())  // TODO iostream-exception
//        throw runtime_error("stream lost");
      LOG(LM_INFO, "CHECK STATE XX");

      xi.streambufO.getOstream().flush();
//    xi.streambufO.getOstream().setf(ios::skipws);
//    xi.streambufO.finalize();
//    xi.xmlResult.writeComment(L"dahjgdhgfga");
    } else {
      Document doc;
      vector<u_char> buf;
      buf.resize(sz);
      CCBuf ccBuf(buf);
      ostream buffer(&ccBuf);
      buffer << file.rdbuf();
      file.close();

//      doc.name(name());
      doc.docId(docId());
      doc.type(docType);
      doc.content(std::move(buf));
      doc.traverse(xmlOut);
    }
  };

};
ObjRegister(GetDocument);

class Dump : virtual public mobs::ObjectBase, virtual public MrpcObject
{
public:
  ObjInit(Dump);


  MemVar(int, id, KEYELEMENT1);

  void execute(mobs::XmlOut &xmlOut, XmlInput &xi) override {
    LOG(LM_INFO, "Dump DB");
    for (uint64_t i = 1; i < 6; i++) {
      GetDocument gd;
      gd.docId(i);
      gd.allowAttach(true);

      xi.needEncryption();
      gd.execute(xmlOut, xi);

    }

    //this->traverse(xmlOut);
  }
};
ObjRegister(Dump);



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

class SearchDocument : virtual public mobs::ObjectBase, virtual public MrpcObject {
public:
  ObjInit(SearchDocument);

  MemVector(DocumentTags, tags);


  void execute(mobs::XmlOut &xmlOut, XmlInput &xi) override {
    TRACE("");
    LOG(LM_INFO, "COMMAND " << to_string());

    auto store = Filestore::instance();
    list<TagInfo> tagInfos;
    map<TagId, TagSearch> tagSearch;

    map<TagId, string> tagNames;
    for (auto i:tags) {
      TagId id = store->findTag(i.name());
      if (id) {
        tagInfos.emplace_back(id, i.content());
        tagNames[id] = i.name();
        tagSearch[id].tagId = id;
        auto c = i.content();
        std::string o = "=";
        if (c.empty())
          o = "";
        else if (c[0] == '*' or c[0] == '<' or c[0] == '>' or c[0] == '=') {
          o = std::string() + c[0];
          c.erase(0, 1);
          if (not c.empty() and (o[0] == '<' and (c[0] == '>' or c[0] == '=') or
                                 o[0] == '>' and c[0] == '=')) {
            o += c[0];
            c.erase(0, 1);
          }
        }
        tagSearch[id].tagOpList.emplace(c, o);
      }
      else
        LOG(LM_ERROR, "Tag " << i.name() << " does not exist");
    }
    list<SearchResult> result;
    store->tagSearch(tagSearch, result);

    SearchDocumentResult sr;
    set<int> skip;
    for (auto it = result.cbegin(); it != result.cend(); it++) {
      if (skip.find(it->docId) != skip.end())
        continue;
      skip.insert(it->docId);
      auto &r = sr.tags[mobs::MemBaseVector::nextpos];
      r.docId(it->docId);
      for (auto it2 = it; it2 != result.cend(); it2++) {
        if (it2->docId == it->docId) {
          auto &inf = r.tags[mobs::MemBaseVector::nextpos];
          auto tn = tagNames.find(it2->tagId);
          if (tn == tagNames.end())
            tn = tagNames.emplace(it2->tagId, store->tagName(it2->tagId)).first;
          inf.name(tagNames[it2->tagId]);
          inf.content(it2->tagContent);
        }
      }
    }

    LOG(LM_INFO, "Result: " << sr.to_string());
    sr.traverse(xmlOut);

  }

};
ObjRegister(SearchDocument);


/// Speichern eines Dokumentes als Attachment
class SaveDocument : virtual public mobs::ObjectBase, virtual public MrpcObject
{
public:
  ObjInit(SaveDocument);

  MemMobsEnumVar(DocumenType, type);
  MemVar(std::string, name);
  MemVar(int64_t, size);
  MemVector(DocumentTags, tags);
  MemVar(uint64_t, supersedeId);
  MemVar(uint64_t, parentId);
  MemVar(std::string, creationInfo);
  MemVar(mobs::MTime, creationTime);


  DocId dId{};

  void execute(mobs::XmlOut &xmlOut, XmlInput &xi) override {
    TRACE("");
    xi.attachmentObj = this;

    auto store = Filestore::instance();
    DocInfo docInfo;
    switch (type()) {
      case DocumentJpeg: docInfo.docType = DocJpeg; break;
      case DocumentTiff: docInfo.docType = DocTiff; break;
      case DocumentText: docInfo.docType = DocText; break;
      case DocumentPdf: docInfo.docType = DocPdf; break;
      case DocumentHtml: docInfo.docType = DocHtml; break;
      case DocumentUnknown: docInfo.docType = DocUnk; break;
    }
    docInfo.creationInfo = "ATTACHED";
//    docInfo.creation();
    std::list<TagInfo> tagInfo;
    store->insertTag(tagInfo, "name", name());
    for (auto &t:tags)
      store->insertTag(tagInfo, t.name(), t.content());
    store->newDocument(docInfo, tagInfo);
    dId = docInfo.id;
  }

  void executeAttachment(mobs::CryptBufAes &crypt, mobs::XmlOut &xmlOut, XmlInput &xi) override {
    TRACE("");
    LOG(LM_INFO, "Do attachment " << name() << " size=" << size() << " " << AES_BYTES(size()));
    crypt.setReadLimit(AES_BYTES(size()));
    istream istr(&crypt);

    auto store = Filestore::instance();

//    ofstream outFile("save.tmp", ios::binary | ios::trunc);
    fstream outFile;
    store->openDocument(dId, outFile, true);

    if (not outFile.is_open())
      THROW("outfile not open");

    outFile << istr.rdbuf();
    outFile.close();


    LOG(LM_INFO, "Attachment saved");
    CommandResult doc;
    if (crypt.bad())
      doc.msg("BAD");
    else
      doc.msg("OK");

    doc.traverse(xmlOut);

  }

};
ObjRegister(SaveDocument);


#define TLOG(l, x) LOG(l, 'T' << id << ' ' << x)

void MRpcServer::worker_thread(int id, MRpcServer *server) {
  for (;;) {
    try {
      TLOG(LM_INFO, "WAITING");
      mobs::tcpstream xstream(server->tcpAccept);
      xstream.exceptions(std::iostream::failbit | std::iostream::badbit);
      LOG(LM_INFO, "Remote: " << xstream.getRemoteHost() << " " << xstream.getRemoteIp());

      if (not xstream.is_open())
        throw runtime_error("connection failed");

      TLOG(LM_INFO, "OK");

      mobs::CryptIstrBuf streambufI(xstream);
      streambufI.getCbb()->setReadDelimiter('\0');
      std::wistream x2in(&streambufI);

      // Output Stream initialisiert
      mobs::CryptOstrBuf streambufO(xstream);
      std::wostream x2out(&streambufO);
      // Writer-Klasse mit File, und Optionen initialisieren
      mobs::XmlWriter xf(x2out, mobs::XmlWriter::CS_utf8, true);
      xf.writeHead();
      xf.writeTagBegin(L"methodResponse");


      cout << "TTT " << std::boolalpha << x2out.fail() << " " << x2out.tellp() << endl;

//    x2out << mobs::CryptBufBase::base64(true);


      // XML-Parser erledigt die eigentliche Arbeit in seinen Callback-Funktionen
      XmlInput xr(x2in, xf, streambufI, streambufO, id, server);
      xr.readTillEof(false);


      do {
        // Input parsen
        xr.parse();
        TLOG(LM_INFO, "XIN bad=" << xstream.bad() << " eof=" << xstream.eof() << " read=" << xstream.tellg() << " eot="
                                 << xr.eot());
        if (xr.attachmentObj) {
          LOG(LM_INFO, "ATTACHMENT");
          vector<u_char> iv;
          iv.resize(mobs::CryptBufAes::iv_size());
          mobs::CryptBufAes::getRand(iv);
          mobs::CryptBufAes cry(xr.ctx->key, iv, "", true);
//          xstream.unsetf(std::ios::skipws);
          auto delim = xstream.get();
          if (delim != 0) {
            LOG(LM_ERROR, "Delimiter is " << int(delim) << " " << (char) delim);
            throw runtime_error("delimiter missing");

//            delim = xstream.get();
          }
          cry.setIstr(xstream);
          mobs::ConvObjToString cth;
          mobs::XmlOut xo(&xf, cth);
          xr.attachmentObj->executeAttachment(cry, xo, xr);
//          xstream.setf(std::ios::skipws);
          delete xr.attachmentObj;
          xr.attachmentObj = nullptr;
          LOG(LM_INFO, "endEncryption; finish=" << xr.finish);
          xr.endEncryption();
          auto *tp = dynamic_cast<mobs::TcpStBuf *>(xstream.rdbuf());
          LOG(LM_INFO, "CHECK STATE " << tp);
          if (tp and tp->bad())  // TODO iostream-exception
            throw runtime_error("stream lost");
        }
      } while (not xr.finish and not xr.eot());
      TLOG(LM_INFO, "parsing done");

      // transmission ends
      xf.writeTagEnd();
      LOG(LM_INFO, "ENDE");
      streambufO.finalize();
      xstream.shutdown();
      TLOG(LM_INFO, "closing good=" << xstream.good());
      xstream.close();
    } catch (mobs::tcpstream::failure &e) {
      TLOG(LM_ERROR, "Worker File-Exception " << e.what());
    } catch (exception &e) {
      TLOG(LM_ERROR, "Worker Exception " << e.what());
    }
  }
}

void MRpcServer::server() {
  if (tcpAccept.initService(service) < 0)
    THROW("Service not started");


//  std::thread t1(worker_thread, 1, this);
//  std::thread t2(worker_thread, 2, this);
  worker_thread(0, this);

//  t1.join();
//  t2.join();





}


int main(int argc, char* argv[]) {
//  logging::Trace::traceOn = true;
  TRACE("");

  try {
    Filestore::instance("DocSrvFiles");
    MRpcServer srv;
//    srv.genKey();
    srv.server();


  }
  catch (std::ios_base::failure &e) {
    LOG(LM_ERROR, "File-Exception " << e.what());
    return 2;
  }
  catch (exception &e)
  {
    LOG(LM_ERROR, "Exception " << e.what());
    return 1;
  }
  return 0;
}
