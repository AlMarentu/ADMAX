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


#include "filestore.h"
#include <exception>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "mobs/querygenerator.h"
#include <sys/stat.h>
#include <set>
#include <utility>
#include <mobs/rsa.h>
//#include <unistd.h>
#include "mobs/dbifc.h"
#include "mobs/logging.h"

#include "mrpc.h"

/*
 * use docsrv
 * db.DMGR_Tag.createIndex({ tagId:1, active:1, content:1 })
 * db.DMGR_Tag.createIndex({ docId:1, tagId:1, active:1 })
 * db.DMGR_TagInfo.createIndex({ name:1, pool:1, bucket:1 }, { unique: true })
 * db.DMGR_BucketInfo.createIndex({ pool:1, tok1:1, tok2:1, tok3:1 })
 *
 * db.DMGR_Tag.getIndexes()
 */

class DMGR_Document : virtual public mobs::ObjectBase
{
public:
  ObjInit(DMGR_Document);

  MemVar(uint64_t, id, KEYELEMENT1);
  MemVar(int, docType);
  MemVar(std::string, fileName); // internal name
  MemVar(int64_t, fileSize);
  MemVar(std::string, checksum);
  MemVar(uint64_t, supersedeId);
  MemVar(uint64_t, parentId);
  MemVar(std::string, creationInfo);
  MemVar(mobs::MTime, creation);
  MemVar(int, creator);
  MemVar(mobs::MTime, insertTime);
  MemVar(mobs::MTime, storageTime, USENULL);
};

/** \brief Datenbankobjekt für Counter
 *
 * 1 DMGR_Document
 * 2 DMGR_TagPool
 */
class DMGR_Counter : virtual public mobs::ObjectBase {
public:
  enum Cntr { CntrDocument = 1, CntrTagPool  = 2, CntrTag  = 3, CntrBucketInfo = 4 };
  ObjInit(DMGR_Counter);
  MemVar(int, id, KEYELEMENT1);
  MemVar(int64_t, counter, VERSIONFIELD);
};


class DMGR_TagInfo : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_TagInfo);
  MemVar(int, id, KEYELEMENT1);
  MemVar(int64_t, version, VERSIONFIELD);
  MemVar(std::string, name);
  MemVar(std::string, pool);  // unique index (name, pool, bucket)
  MemVar(int, bucket); // ein Tag-Pool kann in mehrere Buckets aufgeteilt sein
  MemVar(int, maxSize);
};

class DMGR_TemplatePool : virtual public TemplateInfo {
public:
  ObjInit(DMGR_TemplatePool);
  MemVar(int64_t, version, VERSIONFIELD);

};

class DMGR_BucketPool : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_BucketPool);
  MemVar(std::string, pool, KEYELEMENT1);
  MemVar(std::string, name, KEYELEMENT2);
  MemVar(int, prio, KEYELEMENT3);  // 0 = uniq->alles in einen Bucket, 1,2,3 = BucketElement
  MemVar(int64_t, version, VERSIONFIELD);
  MemVar(bool, displayOnly);
  MemVar(std::string, regex);
  MemVar(std::string, format);
};

class DMGR_BucketInfo : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_BucketInfo);  // index (pool, tok1, tok2, tok3)
  MemVar(int, id, KEYELEMENT1);
//  MemVar(int64_t, version, VERSIONFIELD);
  MemVar(std::string, pool);
  MemVar(std::string, tok1); // BucketToken prio=1
  MemVar(std::string, tok2); // BucketToken prio=2
  MemVar(std::string, tok3); // BucketToken prio=3
};

class DMGR_KeyInfo : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_KeyInfo);
  MemVar(std::string, fingerprint, KEYELEMENT1);
  MemVar(std::string, user);
  MemVar(std::string, pubkey);
  MemVar(int64_t, version, VERSIONFIELD);

};


class DMGR_Tag : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_Tag); // index (docId), index(tagId, active, content)
  MemVar(int64_t, id, KEYELEMENT1);
  //MemVar(int64_t, version, VERSIONFIELD);
  MemVar(uint64_t, docId);
  MemVar(int, tagId);
  MemVar(std::string, content);
  MemVar(bool, active);
  MemVar(mobs::MTime, insertTime);
  MemVar(mobs::MTime, creation);
  MemVar(int, creator);
  MemVar(mobs::MTime, deactivation);
  MemVar(int, deactivator);

};

class DMGR_ServerKey : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_ServerKey);
  MemVar(bool, dummy, KEYELEMENT1);
  MemVar(std::string, privkey);
  MemVar(std::string, pubkey);

};



void Filestore::setBase(const std::string &basedir, bool genkey) {
  base = basedir;
  // Datenbank Verbindung einrichten
  newDbInstance("docsrv");

  DMGR_ServerKey sk;
  DMGR_Counter c;
  DMGR_KeyInfo k;
  DMGR_Document d;
  DMGR_Tag t;
  DMGR_TagInfo p;
  DMGR_TemplatePool tp;
  DMGR_BucketPool bp;
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
  dbi.structure(sk);
  dbi.structure(c);
  dbi.structure(k);
  dbi.structure(d);
  dbi.structure(t);
  dbi.structure(p);
  dbi.structure(tp);
  dbi.structure(bp);

  if (not genkey and dbi.load(sk)) {
    pub = sk.pubkey();
    priv = sk.privkey();
//    LOG(LM_INFO, "PUB " << pub);
  } else {
    mobs::generateRsaKeyMem(priv, pub, "Nd7d/lk");
//    LOG(LM_INFO, "PUB NEW " << pub);
    sk.pubkey(pub);
    sk.privkey(priv);
    dbi.save(sk);
  }
}

void Filestore::newDbInstance(const std::string &con) {
  std::string dbname;
  std::string db = "sqlite://";
  db += base;
  db += "/sqlite.db";

  if (base.find("mongodb://") == 0) {
    db = base;
    dbname = "docsrv";
  }

  // Datenbank-Verbindungen
  mobs::DatabaseManager::instance()->addConnection(con, mobs::ConnectionInformation(db, dbname));
}

Filestore::Filestore() : conName("docsrv") {
}


Filestore::Filestore(std::string con) : conName(std::move(con)) {

}

std::string Filestore::base;
std::string Filestore::pub;
std::string Filestore::priv;


void Filestore::newDocument(DocInfo &doc, const std::list<TagInfo> &tags, int groupId) {
  LOG(LM_INFO, "newDocument ");
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  static DMGR_Counter cntr;
  static std::mutex mutex;
  {
    std::lock_guard<std::mutex> guard(mutex);
    if (cntr.id() != DMGR_Counter::CntrDocument) {
      cntr.id(DMGR_Counter::CntrDocument);
      dbi.load(cntr);
    }
    dbi.save(cntr);
  }

  doc.id = cntr.counter();
  doc.supersedeId = 0;
  doc.insertTime = mobs::MTimeNow();
  if (doc.creation == mobs::MTime{})
    doc.creation = doc.insertTime;

  DMGR_Document dbd;
  dbd.id(doc.id);
  dbd.docType(doc.docType);
  dbd.fileSize(doc.fileSize);
  dbd.parentId(doc.parentId);
  dbd.creation(doc.creation);
  dbd.insertTime(doc.insertTime);
  dbd.creator(doc.creator);
  dbd.creationInfo(doc.creationInfo);

  dbi.save(dbd);

  std::list<uint64_t> docs;
  if (groupId) { // Bei groupId Tags die auf selbe groupId verweisen weglassen, außer group-Tag selbst
    std::string group;
    for (auto &t:tags) {
      if (t.tagId == groupId)
        group = t.tagContent;
    }
    DMGR_Tag tg;
    tg.content(group);
    tg.tagId(groupId);
    tg.active(true);
    for (auto cursor = dbi.qbe(tg); not cursor->eof(); cursor->next()) {
      dbi.retrieve(tg, cursor);
      docs.emplace_back(tg.docId());
    }
    LOG(LM_INFO, "group " << group << " found " << docs.size() << " documents");
    if (docs.empty())
      groupId = 0;
  }

  for (auto &t:tags) {
    if (groupId and t.tagId != groupId) {
      DMGR_Tag ts;
      using Q = mobs::QueryGenerator;
      Q query;
      query << Q::AndBegin << ts.active.QiEq(true) << ts.tagId.QiEq(t.tagId) << ts.content.QiEq(t.tagContent)
            << ts.docId.QiIn(docs) << Q::AndEnd;
      if (not dbi.query(ts, query)->eof()) {
        LOG(LM_INFO, "tag " << t.tagId << " " << t.tagContent << " already exists - skip");
        continue;
      }
    }
    static DMGR_Counter cntr;
    static std::mutex mutex;
    {
      std::lock_guard<std::mutex> guard(mutex);
      if (cntr.id() != DMGR_Counter::CntrTag) {
        cntr.id(DMGR_Counter::CntrTag);
        dbi.load(cntr);
      }
      dbi.save(cntr);
    }
    DMGR_Tag ti;
    ti.id(cntr.counter());
    ti.active(true);
    ti.tagId(t.tagId);
    ti.docId(doc.id);
    ti.content(t.tagContent);
    ti.creation(doc.creation);
    ti.creator(doc.creator);
    ti.insertTime(doc.insertTime);
    LOG(LM_INFO, "SAVE " << ti.to_string());
    dbi.save(ti);
  }
}

void Filestore::documentCreated(DocInfo &info) {
  LOG(LM_INFO, "documentCreated " << info.id << " " << info.fileName);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);

  DMGR_Document dbd;
  dbd.id(info.id);
  if (not dbi.load(dbd))
    THROW("Document missing");
  dbd.fileName(info.fileName);
  dbi.save(dbd);
}

void Filestore::supersedeDocument(DocInfo &doc, DocId supersedeId) {

}

std::string Filestore::writeFile(std::istream &source, const DocInfo &info) {
  LOG(LM_INFO, "writeFile ");
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  if (dbi.getConnection()->connectionType() == u8"Mongo") {
    return dbi.getConnection()->uploadFile(dbi, source);
  } else {
    std::string name = STRSTR(std::hex << std::setfill('0') << std::setw(8) << info.id);
    std::stringstream str;
    str << base << '/' << name;
    std::ofstream of(str.str(), std::ios::binary | std::fstream::trunc);
    if (not of.is_open())
      THROW("file open failed " << str.str());
    of << source.rdbuf();
    of.close();
    if (of.bad())
      THROW("file write failed " << str.str());

    return name;
  }
}

void Filestore::readFile(const std::string &name, std::ostream &dest) {
  LOG(LM_INFO, "readFile " << name);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  if (dbi.getConnection()->connectionType() == u8"Mongo") {
    return dbi.getConnection()->downloadFile(dbi, name, dest);
  } else {
    std::stringstream str;
    str << base << '/' << name;
    std::ifstream inf(str.str(), std::ios::binary);
    if (not inf.is_open())
      THROW("file open failed " << str.str());
    dest << inf.rdbuf();
    inf.close();
    if (inf.bad())
      THROW("file read failed " << str.str());
  }
}


int Filestore::findBucket(const std::string &pool, const std::vector<std::string> &bucketToken) {
  std::string s;
  std::for_each(bucketToken.begin(), bucketToken.end(), [&s](const std::string &i){ s += i; s += " "; });
  LOG(LM_INFO, "findBucket " << s );
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_BucketInfo binfo;
  binfo.pool(pool);
  switch (bucketToken.size()) {
    case 0:
      return 0;
    case 3:
      binfo.tok3(bucketToken[2]);
    case 2:
      binfo.tok2(bucketToken[1]);
    case 1:
      binfo.tok1(bucketToken[0]);
      break;
    default:
      THROW("findBucket: to many tokens");
  }
  auto cursor = dbi.qbe(binfo);
  if (cursor->eof()) {
    // neuen Tag anlegen
    DMGR_Counter cntr;
    cntr.id(DMGR_Counter::CntrBucketInfo);
    dbi.load(cntr);
    dbi.save(cntr);
    binfo.id(int(cntr.counter()));
    if (binfo.id() == 0)
      THROW("BucketId should not be 0");
    dbi.save(binfo);
  }
  else {
    // Tag bereits bekannt
    dbi.retrieve(binfo, cursor);
  }
  return binfo.id();
}


void Filestore::insertTag(std::list<TagInfo> &tags, const std::string &pool, const std::string &tagName,
                          const std::string &content, int bucket) {
  LOG(LM_INFO, "insertTag " << tagName << "[" << bucket << "] " << content);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_TagInfo tpool;
  tpool.pool(pool);
  tpool.name(tagName);
  tpool.bucket(bucket);
  auto cursor = dbi.qbe(tpool);
  if (cursor->eof()) {
    // neuen Tag anlegen
    DMGR_Counter cntr;
    cntr.id(DMGR_Counter::CntrTagPool);
    dbi.load(cntr);
    dbi.save(cntr);
    tpool.id(cntr.counter());
    dbi.save(tpool);
  }
  else {
    // Tag bereits bekannt
    dbi.retrieve(tpool, cursor);
#ifdef KEINE_MEHRFACHEN_TAGS
    for (auto &t:tags) {
      if (t.tagId == tpool.id()) {
        t.tagContent = content;
        return;
      }
    }
#endif
  }
  tags.emplace_back(tpool.id(), content);
}


TagId Filestore::findTag(const std::string &pool, const std::string &tagName, int bucket) {
  LOG(LM_INFO, "findTag " << tagName);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_TagInfo tpool;
  tpool.pool(pool);
  tpool.name(tagName);
  tpool.bucket(bucket);
  auto cursor = dbi.qbe(tpool);
  if (cursor->eof()) {
    return 0;
  }
  else {
    // Tag bereits bekannt
    dbi.retrieve(tpool, cursor);
    return tpool.id();
  }
  return 0;
}

std::string Filestore::tagName(TagId id) {
  LOG(LM_INFO, "tagName " << id);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_TagInfo tpool;
  tpool.id(id);
  auto cursor = dbi.qbe(tpool);
  if (cursor->eof()) {
    return 0;
  }
  else {
    // Tag bereits bekannt
    dbi.retrieve(tpool, cursor);
    return tpool.name();
  }
  return "";
}


std::list<SearchResult>
Filestore::searchTags(const std::string &pool, const std::map<std::string, TagSearch> &searchList,
                      const std::set<int> &buckets, const std::string &groupName, std::function<void (int)> ckFun) {
  LOG(LM_INFO, "search ");
  std::chrono::system_clock::time_point begin = std::chrono::system_clock::now();
  std::chrono::system_clock::time_point now;
  std::list<SearchResult> result;
  TagId groupId = 0;
  if (not groupName.empty())
    groupId = findTag(pool, groupName);

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_Tag ti;

  using Q = mobs::QueryGenerator;    // Erleichtert die Tipp-Arbeit

  std::list<uint64_t> docList;
  std::set<uint64_t> docIdsPrim; // Ids der Primary muss mit allen anderen Buckets eine Schnittmenge bilden
  int cnt = 0;
  int maxCnt = buckets.size();
  for (auto bucket:buckets) {
    LOG(LM_INFO, "SEARCH BUCKET " << bucket);
    // pro SearchList-Eintrag (tag) eine Query und Schnittmenge aus Ergebnissen bilden
    std::set<uint64_t> docIds;
    bool start = true;
    bool startPrim = true;
    for (auto &i:searchList) {
      if (i.second.primary and bucket != 0) // only 0 allowed
        continue;
      TagId id = findTag(pool, i.second.tagName, bucket);
      if (id <= 0) {
        LOG(LM_INFO, "no data for " << i.second.tagName << "[" << bucket << "] ");
        if (bucket == 0 and not i.second.primary and buckets.size() > 1)
          continue;
        break;
      }
//      if (i.first.length() > 3 and i.first.[i.first.length()-1] == '$')
//        dontReturn.insert(id);
      LOG(LM_INFO, "SEARCH: " << i.second.tagName << "[" << bucket << "] " << id << " prim=" << i.second.primary);
      std::set<uint64_t> docIdsTmp;
      std::set<uint64_t> docIdsPrimTmp;
      if (start and bucket and not docIdsPrim.empty())  // bei buckets > 0 mit Primary vorinitialisieren
        docIdsTmp = docIdsPrim;
      else
        docIdsTmp.swap(docIds);
      if (i.second.primary)
        docIdsPrimTmp.swap(docIdsPrim);
      Q query;
      query << Q::AndBegin << ti.active.Qi("=", true) << ti.tagId.Qi("=", id);
      if (not i.second.tagOpList.empty()) {
        query << Q::OrBegin;
        // Ranges erkennen  >a <b
        std::string lastOp; // nur > oder >=
        const std::string *lastCont = nullptr;
        for (auto &s:i.second.tagOpList) {
          if (s.second == ">" or s.second == ">=") {
            if (lastOp.empty()) {
              lastCont = &s.first;
              lastOp = s.second;
              continue;
            } else if (s.second == ">=" and *lastCont == s.first) {
              lastOp = s.second;
              continue;
            } // else ignore, makes no sense
          } else if (not lastOp.empty() and (s.second == "<" or s.second == "<=")) {
            query << Q::AndBegin << ti.content.Qi(lastOp.c_str(), *lastCont) << ti.content.Qi(s.second.c_str(), s.first)
                  << Q::AndEnd;
            lastOp = "";
            continue;
          }
          query << ti.content.Qi(s.second.c_str(), s.first);
        }
        if (not lastOp.empty()) {
          query << ti.content.Qi(lastOp.c_str(), *lastCont);
        }
        query << Q::OrEnd;
      }
      // Query auf bereits bekannte reduzieren
      if (not start and not i.second.primary and docIdsTmp.size() < 100) {
        std::list<uint64_t> l(docIdsTmp.begin(), docIdsTmp.end());
        query << ti.docId.QiIn(l);
      }
      query << Q::AndEnd;

      auto cursor = dbi.query(ti, query);
      now = std::chrono::system_clock::now();
      LOG(LM_INFO, "TIME " << std::chrono::duration_cast<std::chrono::milliseconds>(now - begin).count());
      while (not cursor->eof()) {
        ckFun(20 * cnt / maxCnt);
//        usleep(50000);
        dbi.retrieve(ti, cursor);
        LOG(LM_INFO, "Z " << ti.to_string());
        // Schnittmenge aller sets bilden
        if (start or docIdsTmp.find(ti.docId()) != docIdsTmp.end())
          docIds.emplace(ti.docId());
        if (i.second.primary) { // only bucket 0
          if (startPrim or docIdsPrimTmp.find(ti.docId()) != docIdsPrimTmp.end())
            docIdsPrim.emplace(ti.docId());
        }
        cursor->next();
      }
      now = std::chrono::system_clock::now();
      LOG(LM_INFO, "TIME " << std::chrono::duration_cast<std::chrono::milliseconds>(now - begin).count());
      start = false;
      LOG(LM_INFO, "QSIZE " << docIds.size() << " " << cursor->pos() << " " << docIdsPrim.size());
      if (i.second.primary) {
        startPrim = false;
        if (docIdsPrim.empty()) {
          LOG(LM_INFO, "empty primary search " << i.first);
          return result;
        }
      }
      if (docIds.empty()) {
        LOG(LM_INFO, "empty bucket result at " << i.first);
        break;
      }
    }
    docIdsPrim = docIds;
    if (groupId and not docIds.empty()) { // collect all docs with same groupId
      DMGR_Tag tig;
      std::list<uint64_t> l(docIds.begin(), docIds.end());
      std::list<std::string> groupIds;
      LOG(LM_INFO, "collect via groupId " << groupName << " from " << l.size() << " documents");
      Q queryG1;
      queryG1 << Q::AndBegin << tig.active.QiEq( true) << tig.tagId.QiEq(groupId) << tig.docId.QiIn(l) << Q::AndEnd;
      for (auto cursor = dbi.query(tig, queryG1); not cursor->eof(); cursor->next()) {
        dbi.retrieve(tig, cursor);
        groupIds.emplace_back(tig.content());
      }
      LOG(LM_INFO, "expand via groupId " << groupName << " from " << groupIds.size() << " groupIds");
      Q queryG2;
      queryG2 << Q::AndBegin << tig.active.QiEq( true) << tig.tagId.QiEq(groupId) << tig.content.QiIn(groupIds) << Q::AndEnd;
      for (auto cursor = dbi.query(tig, queryG2); not cursor->eof(); cursor->next()) {
        dbi.retrieve(tig, cursor);
        docIds.insert(tig.docId());
      }
      now = std::chrono::system_clock::now();
      LOG(LM_INFO, "TIME " << std::chrono::duration_cast<std::chrono::milliseconds>(now - begin).count());
    }
    if (bucket != 0 or buckets.size() == 1)
      docList.insert(docList.end(), docIds.begin(), docIds.end());
    cnt++;
  }

  if (docList.empty())
    return result;

  LOG(LM_INFO, "found " << docList.size() << " documents " << docIdsPrim.size() << " primaryId");
  now = std::chrono::system_clock::now();
  LOG(LM_INFO, "TIME " << std::chrono::duration_cast<std::chrono::milliseconds>(now - begin).count());

  Q query2;
  query2 << Q::AndBegin << ti.active.QiEq(true) << ti.docId.QiIn(docList) << Q::AndEnd;

  std::set<uint64_t> docRead;
  maxCnt = docList.size();
  for (auto cursor = dbi.query(ti, query2); not cursor->eof(); cursor->next()) {
    ckFun(20 + 80 * docRead.size() / maxCnt);
//    usleep(50000);

    dbi.retrieve(ti, cursor);
    SearchResult r;
    r.tagId = ti.tagId();
    r.tagContent = ti.content();
    r.docId = ti.docId();
    result.emplace_back(r);
    if (groupId and r.tagId == groupId and docIdsPrim.find(r.docId) != docIdsPrim.end()) {
      r.tagContent.clear();
      r.tagId = 0;
      r.docId = ti.docId();
      result.emplace_front(r);  // thus primary is sorted first
    }
    docRead.insert(r.docId);
  }
  now = std::chrono::system_clock::now();
  LOG(LM_INFO, "TIME " << std::chrono::duration_cast<std::chrono::milliseconds>(now - begin).count());
  return result;
}

void
Filestore::bucketSearch(const std::string &pool, const std::map<int, TagSearch> &searchList, std::set<int> &result) {
  LOG(LM_INFO, "search ");
  result.clear();

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_Tag ti;

  using Q = mobs::QueryGenerator;    // Erleichtert die Tipp-Arbeit
  DMGR_BucketInfo binfo;
  Q query;
  query << Q::AndBegin << binfo.pool.QiEq(pool);
  // Für die einzelnen Zeilen Queries bilden und gemeinsam ver-und-en
  for (auto &i:searchList) {
    LOG(LM_INFO, "SEARCH: " << i.first);
    MemVarType(std::string) *tokVar = nullptr;
    switch (i.first) {
      case 1: tokVar = &binfo.tok1; break;
      case 2: tokVar = &binfo.tok2; break;
      case 3: tokVar = &binfo.tok3; break;
      default:
        THROW("invalid bucket prio");
    }
    if (not i.second.tagOpList.empty()) {
      query <<  Q::OrBegin;
      // Ranges erkennen  >a <b
      std::string lastOp; // nur > oder >=
      const std::string *lastCont = nullptr;
      for (auto &s:i.second.tagOpList) {
        if (s.second == ">" or s.second == ">=") {
          if (lastOp.empty()) {
            lastCont = &s.first;
            lastOp = s.second;
            continue;
          } else if (s.second == ">=" and *lastCont == s.first) {
            lastOp = s.second;
            continue;
          } // else ignore, makes no sense
        }
        else if (not lastOp.empty() and (s.second == "<" or s.second == "<=")) {
          query << Q::AndBegin << tokVar->Qi(lastOp.c_str(), *lastCont) << tokVar->Qi(s.second.c_str(), s.first) << Q::AndEnd;
          lastOp = "";
          continue;
        }
        query << tokVar->Qi(s.second.c_str(), s.first);
      }
      if (not lastOp.empty()) {
        query << tokVar->Qi(lastOp.c_str(), *lastCont);
      }
      query <<  Q::OrEnd;
    }
  }
  query <<  Q::AndEnd;

  auto cursor = dbi.query(binfo, query);
  while (not cursor->eof()) {
    dbi.retrieve(binfo, cursor);
    LOG(LM_INFO, "Z " << binfo.to_string());
    result.insert(binfo.id());
    cursor->next();
  }


}


void Filestore::getDocInfo(DocId id, DocInfo &doc) {
  LOG(LM_INFO, "info ");

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);

  DMGR_Document dbd;
  dbd.id(id);
  if (not dbi.load(dbd))
    THROW("document not found");

  doc.docType = DocType(dbd.docType());
  doc.fileName = dbd.fileName();
  doc.fileSize = dbd.fileSize();
  doc.checkSum = dbd.checksum();
  doc.parentId = dbd.parentId();
  doc.creation = dbd.creation();
  doc.insertTime = dbd.insertTime();
  doc.creator = dbd.creator();
  doc.creationInfo = dbd.creationInfo();
}

void Filestore::getTagInfo(DocId id, std::list<SearchResult> &result, DocInfo &doc) {
  LOG(LM_INFO, "info ");
  result.clear();

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);

  DMGR_Document dbd;
  dbd.id(id);
  if (not dbi.load(dbd))
    THROW("document not found");

  doc.docType = DocType(dbd.docType());
  doc.fileName = dbd.fileName();
  doc.fileSize = dbd.fileSize();
  doc.checkSum = dbd.checksum();
  doc.parentId = dbd.parentId();
  doc.creation = dbd.creation();
  doc.insertTime = dbd.insertTime();
  doc.creator = dbd.creator();
  doc.creationInfo = dbd.creationInfo();

  DMGR_Tag ti;

  using Q = mobs::QueryGenerator;
  Q query2;
  query2 << Q::AndBegin << ti.active.QiEq(true) << ti.docId.QiEq(id) << Q::AndEnd;

  auto cursor = dbi.query(ti, query2);
  while (not cursor->eof()) {
    dbi.retrieve(ti, cursor);
    SearchResult r;
    r.tagId = ti.tagId();
    r.tagContent = ti.content();
    r.docId = ti.docId();
    result.emplace_back(r);
    cursor->next();
  }

}

void Filestore::allDocs(std::vector<DocId> &result) {
  LOG(LM_INFO, "all ");
  result.clear();

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);

  DMGR_Document dbd;
  using Q = mobs::QueryGenerator;
  Q query;
  for (auto cursor = dbi.query(dbd, query); not cursor->eof(); cursor->next()) {
    dbi.retrieve(dbd, cursor);
    result.emplace_back(dbd.id());
  }
}

void Filestore::loadTemplates(std::list<TemplateInfo> &templates) {
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  using Q = mobs::QueryGenerator;
  Q query;
  DMGR_TemplatePool tp;
  for (auto cursor = dbi.query(tp, query);not cursor->eof(); cursor->next()) {
    dbi.retrieve(tp, cursor);
    LOG(LM_INFO, tp.to_string());
    templates.emplace_back();
    templates.back().carelessCopy(tp);
  }
}

void Filestore::loadBuckets(std::map<std::string, BucketPool> &buckets) {
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  using Q = mobs::QueryGenerator;
  Q query;
  DMGR_BucketPool bp;
  std::string primaryName;
  for (auto cursor = dbi.query(bp, query);not cursor->eof(); cursor->next()) {
    dbi.retrieve(bp, cursor);
    LOG(LM_INFO, bp.to_string());
    auto &b = buckets[bp.pool()];
    b.pool = bp.pool();
    std::string pf;
    if (bp.prio() == 0 and primaryName.empty())
      primaryName = bp.name();
    else if (bp.displayOnly() or (bp.prio() > 0 and bp.name() == primaryName))
      pf = std::to_string(b.elements.size()) + "$";
    auto &e = b.elements[bp.name() + pf];
    e.name = bp.name();
    if (bp.displayOnly())
      e.displayOnly = true;
    else
      e.prio = bp.prio();
    if (e.prio and not bp.regex().empty() and not bp.format().empty())
      e.formatter.insertPattern(mobs::to_wstring(bp.regex()), mobs::to_wstring(bp.format()));
  }
}




void Filestore::loadTemplatesFromFile(const std::string &filename) {
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  std::ifstream input(filename);
  std::string buf;
  if (not input)
    THROW("cann't read '" << filename);

  char c;
  while (not input.get(c).eof())
    buf += c;
  input.close();
  ConfigResult cr;
  mobs::string2Obj(buf, cr); //, mobs::ConvObjFromStr().useExceptUnknown());
  LOG(LM_INFO, cr.to_string());

  for (auto &t:cr.templates) {
    if (t.type() == TemplateBucket) {
      for (auto &i:t.tags) {
        DMGR_BucketPool bp;
        bp.pool(t.pool());
        bp.name(i.name());
        bp.prio(i.prio());
        if (dbi.load(bp)) {
          bp.clearModified();
        }
        bp.displayOnly(i.type() == TagDisplay);
        bp.regex(i.regex());
        bp.format(i.format());
        if (bp.isModified())
          dbi.save(bp);
        else
          LOG(LM_INFO, "bucket nothing changed");
      }
    } else {
      DMGR_TemplatePool tp;
      tp.name(t.name());
      tp.pool(t.pool());
      if (dbi.load(tp)) {
        tp.clearModified();
      }
      tp.carelessCopy(t);
      if (tp.isModified())
        dbi.save(tp);
      else
        LOG(LM_INFO, "template nothing changed");
    }
  }

}

void Filestore::addUser(const std::string &fingerprint, const std::string &user, const std::string &pubKey) {
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_KeyInfo k;
  k.fingerprint(fingerprint);
  if (dbi.load(k))
    k.clearModified();
  k.user(user);
  k.pubkey(pubKey);
  if (k.isModified())
    dbi.save(k);
  else
    LOG(LM_INFO, "key info nothing changed");
}

bool Filestore::findUser(const std::string &fingerprint, std::string &user, std::string &pubKey) {
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc(conName);
  DMGR_KeyInfo k;
  k.fingerprint(fingerprint);
  if (not dbi.load(k))
    return false;
  user = k.user();
  pubKey = k.pubkey();
  return true;
}


int BucketPool::getTokenList(const TagSearch &tagSearch, TagSearch &tagResult) {
  auto it = elements.find(tagSearch.tagName);
  if (it == elements.end())
    return -1;
  tagResult.tagOpList.clear();
  if (it->second.prio) {
    for (auto &i:tagSearch.tagOpList) {
      std::wstring result;
      if (it->second.formatter.empty())
        tagResult.tagOpList.emplace(i.first, i.second);
      else if (it->second.formatter.format(mobs::to_wstring(i.first), result))
        tagResult.tagOpList.emplace(mobs::to_string(result), i.second);
    }
  }
  return it->second.prio;
}

bool BucketPool::isBucketTag(const std::string &tagName) {
  auto it = elements.find(tagName);
  if (it == elements.end())
    return false;
  return it->second.prio > 0;
}

// return: store in bucket
bool BucketPool::getToken(const std::string &name, const std::string &content,
                          std::vector<std::string> &bucketToken, std::set<int> &prioCheck) {
  bool bucketVar = true;
  LOG(LM_INFO, "GetInfo " << name);
  for (auto const &i:elements) {
    if (i.second.name == name) {
      LOG(LM_INFO, "GetInfo " << name << " found  " << i.second.prio);
      if (i.second.displayOnly)
        return false;
      if (i.second.prio) {
        if (i.second.prio >= bucketToken.size())
          bucketToken.resize(i.second.prio);
        std::wstring result;
        std::string var;
        if (i.second.formatter.empty())
          var = content;
        else if (i.second.formatter.format(mobs::to_wstring(content), result))
          var = mobs::to_string(result);
        if (prioCheck.find(i.second.prio) == prioCheck.end()) { // already filled
          if (bucketToken[i.second.prio - 1] != var)
            THROW("token mismatch " << bucketToken[i.second.prio - 1] << " <-> " << var);
        } else {
          prioCheck.erase(i.second.prio);
          bucketToken[i.second.prio - 1] = var;
        }
      } else {
        prioCheck.erase(0);
        bucketVar = false;
      }
    }
  }

  return bucketVar;
}


