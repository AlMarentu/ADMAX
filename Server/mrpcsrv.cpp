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

#define MRPC_SERVER
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
#include <getopt.h>

ObjRegister(SessionError);
ObjRegister(CommandResult);
ObjRegister(SessionLogin);
ObjRegister(SessionResult);

using namespace std;

#define AES_BYTES(fileSize) (mobs::CryptBufAes::iv_size() + (fileSize + 16) / 16 * 16)

class SessionContext;
class XmlInput;

class MrpcException : public std::runtime_error {
public:
  MrpcException(const char *msg) : std::runtime_error(msg) {};
  MrpcException(const std::string &msg) : std::runtime_error(msg) {};
};

class MRpcServer {
public:
  std::string service = "4444";
  std::string name = "server";
  std::string keystorePath = "keystore";
  std::string passphrase = "12345";

  void server();

  void genKey() const {
    mobs::generateRsaKey(STRSTR(keystorePath << '/' << name << "_priv.pem"),
                         STRSTR(keystorePath << '/' << name << ".pem"), passphrase);
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


class TagInfoCache {
public:
  string name;
  bool isDate = false;
  bool isIdent = false;
  bool isTag = false; // kein Inhalt
  bool doSearch = false; // create extra search tags
  bool infoOnly = false; // no bucket no search
  set<string> token;
  mobs::StringFormatter formatter{};

  void addInfo(const TemplateTagInfo &t);
  bool format(string &value) const;

};

class SessionContext {
public:
  SessionContext(u_int id, std::string  l, std::vector<u_char>  k) : sessionId(id), login(std::move(l)), key(std::move(k)) {}
  u_int sessionId;
  string login;
  vector<u_char> key;

  list<TemplateInfo> tInfo{}; // Liste für diese Session erlaubter Templates
  // TODO System-User ohne Templates für Backup/Restore
  // Caches für das Template cacheTemplate
  map<string, TagInfoCache> tagInfoCache;
  // Caches, für die Buckets
  map<string, BucketPool> bucketCache;
  string poolCache;
  string cacheTemplate;
  string cacheGroupName;

  void enter();
  void release();
  /// return poolname
  string setTemplate(const string &templateName);
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

string SessionContext::setTemplate(const string &templateName) {
  if ( templateName.empty())
    return "";
  for (auto const &i:tInfo) {
    if (i.name() == templateName) {
      if (cacheTemplate != templateName) {
        for (auto const &i:tInfo) {
          if (i.name() == templateName) {
            poolCache = i.pool();
            tagInfoCache.clear();
            for (auto &t:i.tags) {
              tagInfoCache[t.name()].addInfo(t);
              if (t.type() == TagIdent and cacheGroupName.empty())  // TODO Schalter in Config oder eigener Type
                cacheGroupName = t.name();
            }
          }
        }
      }
    }
  }
  return poolCache;
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
    LOG(LM_INFO, "filled " << obj->to_string() << (encryptedInput ? " OK":" UNENCRYPTED"));
    if (not error.empty())
      THROW("error in XML stream: " << error);

    mobs::ConvObjToString cth;
    mobs::XmlOut xo(&xmlResult, cth);

    if (auto *sess = dynamic_cast<SessionLogin *>(obj)) {
      LOG(LM_INFO, "LOGIN " << sess->cipher().size());
      std::vector<u_char> inhalt;
      mobs::decryptPrivateRsa(sess->cipher(), inhalt, STRSTR(server->keystorePath << '/' << server->name << "_priv.pem"), server->passphrase);
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
    } else {
      needEncryption();

      ExecVisitor vi(xo, *this);
      obj->visit(vi);

      if (attachmentInfo.fileSize) {
        delete obj;
        return;
      }
    }


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
  DocInfo attachmentInfo{};
  int64_t attachmentRefId = 0;
  string attachmentError; // if set, don#t save and return this message
};






//mutex MRpcServer::m;
//map<u_int, SessionContext> MRpcServer::sessions;


//Objektdefinitionen


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





void TagInfoCache::addInfo(const TemplateTagInfo &t) {
  name = t.name();
  switch (t.type()) {
    case TagIdent:
      isIdent = true;
    case TagString:
      if (not t.regex().empty())
        formatter.insertPattern(mobs::to_wstring(t.regex()), mobs::to_wstring(t.format()));
      else if (t.type() == TagString)
        doSearch = true;
      break;
    case TagEnumeration:
      for (auto &i:t.enums)
        token.emplace(i());
      isTag = token.empty();
      break;
    case TagDate:
      isDate = true;
      formatter.insertPattern(L"([0-3]\\d).([01]\\d).([12]\\d{3})", L"%3%d-%2%02d-%1%02d");
      formatter.insertPattern(L"([12]\\d{3})-([01]\\d)-([0-3]\\d)", L"%1%d-%2%02d-%3%02d");
      break;
    case TagDisplay:
      infoOnly = true;
      break;
  }
}


bool TagInfoCache::format(string &value) const {
  if (isTag) {
    return value.empty();
  }
  if (not formatter.empty()) {
    wstring result;
    if (formatter.format(mobs::to_wstring(value), result)) {
      LOG(LM_INFO, "FORMAT " << name << " " << value << " -> " << mobs::to_string(result));
      value = mobs::to_string(result);
      return true;
    }
    // alternativ auch Token zulassen
    if (token.find(value) != token.end()) {
      LOG(LM_INFO, "TOKEN " << name << " " << value);
      return true;
    }
    return false;
  }
  if (isDate) {
    mobs::MDate t;
    if (not mobs::string2x(value, t))
      return false;
    LOG(LM_INFO, "FORMAT " << name << " " << value << " -> " << mobs::to_string_iso8601(t, mobs::MDay));
    value = mobs::to_string_iso8601(t, mobs::MDay);
  }
  if (not token.empty()) {
    if (token.find(value) != token.end()) {
      LOG(LM_INFO, "TOKEN " << name << " " << value);
      return true;
    }
  } else
    return true;

  return false;
}


ObjRegister(Ping);
ObjRegister(SaveDocument);
ObjRegister(SearchDocument);
ObjRegister(Dump);
ObjRegister(GetDocument);
ObjRegister(GetConfig);


void ExecVisitor::visit(mobs::ObjectBase &obj) {
  THROW("no MRpc object");
}

void ExecVisitor::visit(GetDocument &obj) {
  TRACE("");
  if (not m_xi.ctx)
    THROW("missing session context");
  auto store = Filestore::instance();
  list<SearchResult> result;
  DocInfo docInfo;
  docInfo.id = obj.docId();
  if (obj.allInfos())
    store->tagInfo(obj.docId(), result, docInfo);
  else
    store->getDocInfo(obj.docId(), docInfo);

  DocumenType docType;
  switch(docInfo.docType) {
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

  if (docInfo.fileSize > 8000 and obj.allowAttach()) {
    DocumentRaw doc;

//    sz = 81;
//    doc.name(name());
    doc.info.docId(obj.docId());

    map<TagId, string> tagNames;
    for (auto i:result) {
      auto &inf = doc.info.tags[mobs::MemBaseVector::nextpos];
      auto tn = tagNames.find(i.tagId);
      if (tn == tagNames.end())
        tn = tagNames.emplace(i.tagId, store->tagName(i.tagId)).first;
      inf.name(tagNames[i.tagId]);
      inf.content(i.tagContent);
    }
    doc.size(docInfo.fileSize);
    doc.type(docType);
    if (obj.allInfos()) {
      doc.info.creationTime(docInfo.creation);
      doc.info.creationInfo(docInfo.creationInfo);
    }

    doc.traverse(m_xmlOut);
    LOG(LM_INFO, "endEncryption;");
    m_xi.endEncryption();

    LOG(LM_INFO, "Start attachment type=" << doc.type.toStr(mobs::ConvToStrHint(false)));
    vector<u_char> iv;
    iv.resize(mobs::CryptBufAes::iv_size());
    mobs::CryptBufAes::getRand(iv);
    mobs::CryptBufAes cry(m_xi.ctx->key, iv, "", true);
//    xi.streambufO.getOstream().unsetf(ios::skipws);
    cry.setOstr(m_xi.streambufO.getOstream());
    ostream ostb(&cry);
    std::streamsize a = m_xi.streambufO.getOstream().tellp();

    store->readFile(docInfo.fileName, ostb);
    cry.finalize();
    std::streamsize b = m_xi.streambufO.getOstream().tellp();
    if (b - a != AES_BYTES(docInfo.fileSize))
      LOG(LM_ERROR, "Error in size: written " << b - a << " <> calculated " << AES_BYTES(docInfo.fileSize));
    LOG(LM_INFO, "WRITTEN: " << a << " + " << b - a);
    m_xi.streambufO.getOstream().flush();
  } else {
    Document doc;
    vector<u_char> buf;
    buf.resize(docInfo.fileSize);
    CCBuf ccBuf(buf);
    ostream buffer(&ccBuf);
    store->readFile(docInfo.fileName, buffer);

//      buffer << file.rdbuf();
//      file.close();

//      doc.name(name());
    doc.info.docId(obj.docId());
    map<TagId, string> tagNames;
    for (auto i:result) {
      auto &inf = doc.info.tags[mobs::MemBaseVector::nextpos];
      auto tn = tagNames.find(i.tagId);
      if (tn == tagNames.end())
        tn = tagNames.emplace(i.tagId, store->tagName(i.tagId)).first;
      inf.name(tagNames[i.tagId]);
      inf.content(i.tagContent);
    }
    doc.type(docType);
    if (obj.allInfos()) {
      doc.info.creationTime(docInfo.creation);
      doc.info.creationInfo(docInfo.creationInfo);
    }
    doc.content(std::move(buf));
    doc.traverse(m_xmlOut);
  }
}

void ExecVisitor::visit(SearchDocument &obj) {
  if (not m_xi.ctx)
    THROW("missing session context");
  TRACE("");
  LOG(LM_INFO, "COMMAND " << obj.to_string());

  SessionContext &context = *m_xi.ctx;
  auto store = Filestore::instance();
  if (context.tInfo.empty())
    store->loadTemplates(context.tInfo);
  if (context.bucketCache.empty())
    store->loadBuckets(context.bucketCache);

  // TODO Template prüfen (=Rechte), fixed Tags hinzu
  string pool = context.setTemplate(obj.templateName());

  if (pool.empty())
    THROW("template " << obj.templateName() << " invalid");
  auto const bucketIt = context.bucketCache.find(pool);

  map<string, TagSearch> tagSearch;
  for (auto i:obj.tags) {
    auto f = context.tagInfoCache.find(i.name());
    bool search = (f != context.tagInfoCache.end() and f->second.doSearch);
    string id = i.name() + string(search ? "$$" : "");

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
    if (search) {
      wstring n = mobs::to_wstring(c);
      std::wstring::const_iterator it = n.begin();
      char cnt = '1';
      while (it != n.end()) {
        string result;
        it = mobs::to7Up(it, n.end(), result);
        LOG(LM_INFO, "NNN " << c << " -> " << result);
        if (o == "=") {
          tagSearch[id + cnt].tagOpList.emplace(result + "%", "LIKE");
          tagSearch[id + cnt].tagName = id;
          cnt++;
        }
        else {
          tagSearch[id].tagOpList.emplace(result, o);
          tagSearch[id].tagName = id;
        }
      }
    } else {
      tagSearch[id].tagName = id;
      tagSearch[id].tagOpList.emplace(c, o);
    }
  }

  LOG(LM_INFO, "TAGSEARCH");
  for (auto &i:tagSearch) {
    for (auto &j:i.second.tagOpList) {
      LOG(LM_INFO, "TAG " << i.second.tagName << " " << j.second << " " << j.first);
    }
  }

  bool primarySearch = false;
  set<int> buckets;
  if (bucketIt != context.bucketCache.end()) {
    map<int, TagSearch> tagSearchBuckets;
    for (auto &i:tagSearch) {
      TagSearch tagResult;
      int prio = bucketIt->second.getTokenList(i.second, tagResult);
      if (prio > 0) {
        tagSearchBuckets.emplace(prio, tagResult);
      } else if (prio == 0) {
        primarySearch = true;
        i.second.primary = true;
      }
    }

    LOG(LM_INFO, "TAGSEARCH BUCKETS");
    for (auto &i:tagSearchBuckets) {
      for (auto &j:i.second.tagOpList) {
        LOG(LM_INFO, "TAG " << i.first << " " << j.second << " " << j.first);
      }
    }
    if (not primarySearch or not tagSearchBuckets.empty())
      store->bucketSearch(pool, tagSearchBuckets, buckets);

    if (primarySearch)
      buckets.insert(0);
  }
  else
    buckets.insert(0);

  list<SearchResult> result = store->tagSearch(pool, tagSearch, buckets, context.cacheGroupName);

  map<TagId, string> tagNames;  // TODO cache in tagSearch mitverwenden
  tagNames[0] = "prim$$";
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
  sr.traverse(m_xmlOut);
}

void ExecVisitor::visit(SaveDocument &obj) {
  TRACE("");
  if (not m_xi.ctx)
    THROW("missing session context");
  DocInfo docInfo;
  docInfo.fileSize = obj.size();
  m_xi.attachmentError.clear();
  m_xi.attachmentRefId = obj.refId();
  try {
    SessionContext &context = *m_xi.ctx;
    auto store = Filestore::instance();
    if (context.tInfo.empty())
      store->loadTemplates(context.tInfo);
    if (context.bucketCache.empty())
      store->loadBuckets(context.bucketCache);
    // TODO Template prüfen (=Rechte), fixed Tags hinzu, nur Systemuser darf ohne Template speichern
    string pool = context.setTemplate(obj.templateName());
    if (pool.empty())
      pool = obj.pool();

    if (pool.empty()) {
      LOG(LM_ERROR, "template " << obj.templateName() << " invalid");
      throw MrpcException("BAD TEMPLATE");
    }

    TagId groupId = 0;
    if (not context.cacheGroupName.empty())
      groupId = store->findTag(pool, context.cacheGroupName);

    auto const bucketIt = context.bucketCache.find(pool);

    docInfo.creation = obj.creationTime();
    docInfo.creationInfo = obj.creationInfo();
    switch (obj.type()) {
      case DocumentJpeg:
        docInfo.docType = DocJpeg;
        break;
      case DocumentTiff:
        docInfo.docType = DocTiff;
        break;
      case DocumentText:
        docInfo.docType = DocText;
        break;
      case DocumentPdf:
        docInfo.docType = DocPdf;
        break;
      case DocumentHtml:
        docInfo.docType = DocHtml;
        break;
      case DocumentUnknown:
        docInfo.docType = DocUnk;
        break;
    }
    class TagTmp {
    public:
      TagTmp(const string &n, const string &c) : name(n), content(c) {};
      string name;
      string content;
      bool inBucket = false;
    };
    if (m_xi.attachmentError.empty()) {
      list<TagTmp> tagTmp;
      for (auto &t:obj.tags) {
        string value = t.content();
        if (not obj.templateName().empty()) {
          auto f = context.tagInfoCache.find(t.name());
          if (f != context.tagInfoCache.end()) {
            if (not f->second.format(value)) {
              LOG(LM_ERROR, "format failed " << t.name());
              throw MrpcException(STRSTR("BAD TAG " << t.name()));
            } else if (f->second.doSearch) {
              wstring n = mobs::to_wstring(value);
              std::wstring::const_iterator it = n.begin();
              while (it != n.end()) {
                string result;
                it = mobs::to7Up(it, n.end(), result);
                LOG(LM_INFO, "NNN " << value << " -> " << result);
                tagTmp.emplace_back(t.name() + "$$", result);
              }
            }
          } else
            LOG(LM_INFO, "tag w/o info " << t.name());
        }
        tagTmp.emplace_back(t.name(), value);
      }
      std::list<TagInfo> tagInfo;

      if (bucketIt != context.bucketCache.cend()) {
        // Bucket auf Vollständigkeit prüfen
        vector<string> buckTok;
        set<int> prioCheck;
        bool primaryOnly = true;
        for (auto const &i:bucketIt->second.elements)
          prioCheck.insert(i.second.prio);
        // extract bucket tags, mark primary tags
        for (auto &i:tagTmp) {
          string token;
          bool inBucket = bucketIt->second.getToken(i.name, i.content, buckTok, prioCheck);
          if (inBucket)
            primaryOnly = false;
          i.inBucket = inBucket;
        }
        if (prioCheck.find(0) != prioCheck.end()) {
          LOG(LM_ERROR, "primary tag missing");
          throw MrpcException("BAD CONTENT");
        }
        if (not primaryOnly and not prioCheck.empty()) {
          LOG(LM_ERROR, "missing bucket tag " << *prioCheck.begin());
          throw MrpcException("BAD CONTENT");
        }

        int bucket = -1;
        for (auto &i:tagTmp) {
          if (i.inBucket) {
            if (bucket == -1)
              bucket = store->findBucket(pool, buckTok);
            store->insertTag(tagInfo, pool, i.name, i.content, bucket);
          } else
            store->insertTag(tagInfo, pool, i.name, i.content);
        }
      }
      else
      {
        for (auto &i:tagTmp)
          store->insertTag(tagInfo, pool, i.name, i.content);
      }

      store->newDocument(docInfo, tagInfo, 0/*groupId*/);
    }
  } catch (MrpcException &e) {
    LOG(LM_ERROR, "Mrpc-Exception " << e.what());
    m_xi.attachmentError = e.what();
  } catch (exception &e) {
    LOG(LM_ERROR, "Exception " << e.what());
    m_xi.attachmentError = "BAD UNKNOWN";
  }
  m_xi.attachmentInfo = docInfo;
}

void ExecVisitor::visit(GetConfig &obj) {
  if (not m_xi.ctx)
    THROW("missing session context");
  SessionContext &context = *m_xi.ctx;
  auto store = Filestore::instance();
  ConfigResult co;
  context.tInfo.clear();
  store->loadTemplates(context.tInfo);
  // TODO Rechte filtern: nur erlaubte Templates liefern
  for (auto t:context.tInfo) {
    co.templates[mobs::MemBaseVector::nextpos].doCopy(t);
  }
  LOG(LM_INFO, "Result: " << co.to_string());
  co.traverse(m_xmlOut);
}

void ExecVisitor::visit(Ping &obj) {
  if (not m_xi.ctx)
    THROW("missing session context");
  LOG(LM_INFO, "Send duplicate");
  obj.cnt(obj.cnt()+1);
  obj.traverse(m_xmlOut);
}

void ExecVisitor::visit(Dump &obj) {
  if (not m_xi.ctx)
    THROW("missing session context");
  LOG(LM_INFO, "Dump DB");
  auto store = Filestore::instance();
  std::vector<DocId> result;
  store->allDocs(result);
  for (auto i:result) {
    GetDocument gd;
    gd.docId(i);
    gd.allowAttach(true);
    gd.allInfos(true);

    m_xi.needEncryption();
    visit(gd);
  }

  //this->traverse(xmlOut);
}


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
        if (xr.attachmentInfo.fileSize) {
          LOG(LM_INFO, "Do attachment " << xr.attachmentInfo.id << " size=" << xr.attachmentInfo.fileSize << " " << AES_BYTES(xr.attachmentInfo.fileSize));
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

          cry.setReadLimit(AES_BYTES(xr.attachmentInfo.fileSize));
          cry.hashAlgorithm("sha1");
          istream istr(&cry);

          auto store = Filestore::instance();

          if (xr.attachmentError.empty()) {
            xr.attachmentInfo.fileName = store->writeFile(istr, xr.attachmentInfo);
            xr.attachmentInfo.checkSum = cry.hashStr();
            LOG(LM_INFO, "HASH " << cry.hashStr());
            store->documentCreated(xr.attachmentInfo);
            if (cry.bad())
              THROW("error while encrypting attachment");
            LOG(LM_INFO, "Attachment saved");
          } else {
            xr.attachmentInfo.id = 0;
            // skip attachment
            size_t c = 0;
            char ch;
            while (not istr.get(ch).eof()) c++;
            LOG(LM_INFO, "HASH " << cry.hashStr() << " " << c);
            if (cry.bad())
              THROW("error while encrypting attachment");
            LOG(LM_INFO, "Attachment skipped");
          }
          xr.attachmentInfo.fileSize = 0;

          CommandResult doc;
          doc.docId(xr.attachmentInfo.id);
          doc.refId(xr.attachmentRefId);
          if (xr.attachmentError.empty())
            doc.msg("OK");
          else
            doc.msg(xr.attachmentError);

          doc.traverse(xo);

//          xstream.setf(std::ios::skipws);
          LOG(LM_INFO, "endEncryption; finish=" << xr.finish);
          xr.endEncryption();
        }
        if (auto *tp = dynamic_cast<mobs::TcpStBuf *>(xstream.rdbuf())) {
          LOG(LM_INFO, "CHECK STATE " << tp->bad());
          if (tp->bad())  // TODO iostream-exception
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

  // TODO zu Debug-Zweckem keine Threads
//  std::thread t1(worker_thread, 1, this);
//  std::thread t2(worker_thread, 2, this);
  worker_thread(0, this);

//  t1.join();
//  t2.join();





}


void usage() {
  cerr << "usage: mrpcsrv [-g] [-p passphrase] [-k keystore] [-b base]\n"
       << " -p passphrase default = '12345'\n"
       << " -k keystore directory for keys, default = 'keystore'\n"
       << " -n keyname name for key, default = 'server'\n"
       << " -P Port default = '4444'\n"
       << " -b base dir default = 'DocSrvFiles'\n"
       << "    mongo uri eg. 'mongodb://localhost:27017'\n"
       << " -c configfile lese Config aus Datei in DB und beende\n"
       << " -g generate key and exit\n";

  exit(1);
}
int main(int argc, char* argv[]) {
//  logging::Trace::traceOn = true;
  TRACE("");

  string base = "DocSrvFiles";
  string passphrase = "12345";
  string keystore = "keystore";
  string keyname = "server";
  string port = "4444";
  string configfile;
  bool genkey = false;

  try {
    char ch;
    while ((ch = getopt(argc, argv, "gp:n:P:b:c:")) != -1) {
      switch (ch) {
        case 'g':
          genkey = true;
          break;
        case 'p':
          passphrase = optarg;
          break;
        case 'k':
          keystore = optarg;
          break;
        case 'n':
          keyname = optarg;
          break;
        case 'P':
          port = optarg;
          break;
        case 'b':
          base = optarg;
          break;
       case 'c':
         configfile = optarg;
          break;
        case '?':
        default:
          usage();
      }
    }

    MRpcServer srv;
    srv.passphrase = passphrase;
    srv.keystorePath = keystore;
    srv.name = keyname;
    srv.service = port;

    if (genkey) {
      srv.genKey();
      exit(0);
    }

    ifstream k(STRSTR(keystore << '/' <<  keyname << "_priv.pem"));
    if (not k.is_open()) {
      cerr << "private key not found - generate with -g" << endl;
      exit(1);
    }
    k.close();

    Filestore::instance(base);
    if (not configfile.empty()) {
      Filestore::instance()->loadTemplatesFromFile(configfile);
      return 0;
    }
#ifndef NDEBUG
    auto store = Filestore::instance();
    ConfigResult co;
    list<TemplateInfo> tinf;
    store->loadTemplates(tinf);
    for (auto t:tinf) {
      LOG(LM_INFO, "Config: " << t.to_string());
    }

//    map<string, BucketPool> buckets;
//    store->loadBuckets(buckets);
#endif
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
