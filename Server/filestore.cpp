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
#include "mobs/dbifc.h"
#include "mobs/logging.h"

#include "mrpc.h"

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
  enum Cntr { CntrDocument = 1, CntrTagPool  = 2, CntrTag  = 3 };
  ObjInit(DMGR_Counter);
  MemVar(int, id, KEYELEMENT1);
  MemVar(int64_t, counter, VERSIONFIELD);
};

class DMGR_TagTransformer : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_TagTransformer);
  MemVar(std::string, regex);
  MemVar(std::string, format);
};

class DMGR_TagPool : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_TagPool);
  MemVar(int, id, KEYELEMENT1);
  MemVar(int64_t, version, VERSIONFIELD);
  MemVar(int, tagType); // TagPool::TagType  T_Enumeration = 0, T_Date = 1, T_String = 2, T_Number = 3
  MemVar(std::string, name);
  MemVector(DMGR_TagTransformer, transformer); // T_String T_Number
  MemVarVector(std::string, enumeration, DBJSON); // T_Enumeration wenn leer, dann nur Tag ohne Inhalt
  MemVar(std::string, searchTag);   // weiteres Tag mit dem Suchinhalt (Methode steht in dessem tagType (> 100)
  MemVar(int, maxSize);
};

class DMGR_TemplatePool : virtual public TemplateInfo {
public:
  ObjInit(DMGR_TemplatePool);
  MemVar(int64_t, version, VERSIONFIELD);

};



class DMGR_TagInfo : virtual public mobs::ObjectBase {
public:
  ObjInit(DMGR_TagInfo);
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




Filestore::Filestore(const std::string &basedir)  : base(basedir) {
  if (store)
    throw std::runtime_error("Filestore already exists");
  // Datenbank Verbindung einrichten

  std::string dbname;
  std::string db = "sqlite://";
  db += basedir;
  db += "/sqlite.db";

  if (basedir.find("mongodb://") == 0) {
    db = basedir;
    dbname = "docsrv";
  }

  // Datenbank-Verbindungen
  dbMgr.addConnection("docsrv", mobs::ConnectionInformation(db, dbname));
  DMGR_Counter c;
  DMGR_Document d;
  DMGR_TagInfo t;
  DMGR_TagPool p;
  DMGR_TemplatePool tp;
  auto dbi = dbMgr.getDbIfc("docsrv");
  dbi.structure(c);
  dbi.structure(d);
  dbi.structure(t);
  dbi.structure(p);
  dbi.structure(tp);


}

Filestore *Filestore::store = nullptr;
mobs::DatabaseManager Filestore::dbMgr;  // singleton, darf nur einmalig eingerichtet werden und muss bis zum letzten Verwenden einer Datenbank bestehen bleiben


void Filestore::newDocument(DocInfo &doc, std::list<TagInfo> tags) {
  LOG(LM_INFO, "newDocument ");
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
  static DMGR_Counter cntr;
  if (cntr.id() != DMGR_Counter::CntrDocument) {
    cntr.id(DMGR_Counter::CntrDocument);
    dbi.load(cntr);
  }
  dbi.save(cntr);

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

  for (auto &t:tags) {
    static DMGR_Counter cntr;
    if (cntr.id() != DMGR_Counter::CntrTag) {
      cntr.id(DMGR_Counter::CntrTag);
      dbi.load(cntr);
    }
    dbi.save(cntr);
    DMGR_TagInfo ti;
    ti.id(cntr.counter());
    ti.active(true);
    ti.tagId(t.tagId);
    ti.docId(doc.id);
    ti.content(t.tagContent);
    ti.creation(doc.creation);
    ti.creator(doc.creator);
    ti.insertTime(doc.insertTime);
    dbi.save(ti);
  }
}

void Filestore::documentCreated(DocInfo &info) {
  LOG(LM_INFO, "documentCreated " << info.id << " " << info.fileName);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");

  DMGR_Document dbd;
  dbd.id(info.id);
  if (not dbi.load(dbd))
    THROW("Document missing");
  dbd.fileName(info.fileName);
  dbi.save(dbd);
}

void Filestore::supersedeDocument(DocInfo &doc, DocId supersedeId) {

}

Filestore *Filestore::instance(const std::string &basedir) {
  if (not store)
    store = new Filestore(basedir);
  return store;
}


std::string Filestore::writeFile(std::istream &source, const DocInfo &info) {
  LOG(LM_INFO, "writeFile ");
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
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
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
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

void Filestore::insertTag(std::list<TagInfo> &tags, const std::string &tagName, const std::string &content) {
  LOG(LM_INFO, "insertTag " << tagName << " " << content);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
  DMGR_TagPool tpool;
  tpool.name(tagName);
  auto cursor = dbi.qbe(tpool);
  if (cursor->eof()) {
    // neuen Tag anlegen
    DMGR_Counter cntr;
    cntr.id(DMGR_Counter::CntrTagPool);
    dbi.load(cntr);
    dbi.save(cntr);
    tpool.id(cntr.counter());
    tpool.tagType(TagPool::T_String);
    dbi.save(tpool);
  }
  else {
    // Tag bereits bekannt
    dbi.retrieve(tpool, cursor);
    for (auto &t:tags) {
      if (t.tagId == tpool.id()) {
        t.tagContent = content;
        return;
      }
    }
  }
  tags.emplace_back(tpool.id(), content);
}


TagId Filestore::findTag(const std::string &tagName) {
  LOG(LM_INFO, "findTag " << tagName);
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
  DMGR_TagPool tpool;
  tpool.name(tagName);
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
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
  DMGR_TagPool tpool;
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


void Filestore::tagSearch(const std::map<TagId, TagSearch> &searchList, std::list<SearchResult> &result) {
  LOG(LM_INFO, "search ");
  result.clear();

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
  DMGR_TagInfo ti;

  using Q = mobs::QueryGenerator;    // Erleichtert die Tipp-Arbeit
  std::set<uint64_t> docIds;
  bool start = true;

  // Hier den besten aus der Filter-Liste aussuchen, keine OR-Verknüpfung
  for (auto &i:searchList) {
    LOG(LM_INFO, "SEARCH: " << i.first);
    std::set<uint64_t> docIdsTmp;
    docIdsTmp.swap(docIds);
    Q query;
    query << Q::AndBegin << ti.active.Qi("=", true) << ti.tagId.Qi("=", i.first);
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
          query << Q::AndBegin << ti.content.Qi(lastOp.c_str(), *lastCont) << ti.content.Qi(s.second.c_str(), s.first) << Q::AndEnd;
          lastOp = "";
          continue;
        }
        query << ti.content.Qi(s.second.c_str(), s.first);
      }
      if (not lastOp.empty()) {
        query << ti.content.Qi(lastOp.c_str(), *lastCont);
      }
      query <<  Q::OrEnd;
    }
    query <<  Q::AndEnd;

    auto cursor = dbi.query(ti, query);
    while (not cursor->eof()) {
      dbi.retrieve(ti, cursor);
      LOG(LM_INFO, "Z " << ti.to_string());
      // Schnittmenge aller sets bilden
      if (start or docIdsTmp.find(ti.docId()) != docIdsTmp.end())
        docIds.emplace(ti.docId());
      cursor->next();
    }
    start = false;
  }


  if (docIds.empty())
    return;

  std::list<uint64_t> l(docIds.begin(), docIds.end());

  Q query2;
  query2 << Q::AndBegin << ti.active.Qi("=", true) << ti.docId.QiIn(l)<< Q::AndEnd;

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


void Filestore::getDocInfo(DocId id, DocInfo &doc) {
  LOG(LM_INFO, "info ");

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");

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

void Filestore::tagInfo(DocId id, std::list<SearchResult> &result, DocInfo &doc) {
  LOG(LM_INFO, "info ");
  result.clear();

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");

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

  DMGR_TagInfo ti;

  using Q = mobs::QueryGenerator;    // Erleichtert die Tipp-Arbeit
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

  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");

  DMGR_Document dbd;
  using Q = mobs::QueryGenerator;    // Erleichtert die Tipp-Arbeit
  Q query;
  for (auto cursor = dbi.query(dbd, query); not cursor->eof(); cursor->next()) {
    dbi.retrieve(dbd, cursor);
    result.emplace_back(dbd.id());
  }
}

void Filestore::loadTemplates(std::list<TemplateInfo> &templates) {
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
  using Q = mobs::QueryGenerator;
  Q query;
  DMGR_TemplatePool tp;
  for (auto cursor = dbi.query(tp, query);not cursor->eof(); cursor->next()) {
    dbi.retrieve(tp, cursor);
    LOG(LM_INFO, tp.to_string());
    templates.emplace_back();
    templates.back().carelessCopy(tp);
  }

  return;
}

void Filestore::loadTemplatesFromFile(const std::string &filename) {
  auto dbi = mobs::DatabaseManager::instance()->getDbIfc("docsrv");
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
      LOG(LM_INFO, "nothing changed");

  }

}



#if 0
void Filestore::tagSearch(const std::list<TagSearchInfo> &searchList, std::list<SearchResult> &result) {

  result.clear();

  DMGR_TagInfo ti;

  using Q = mobs::QueryGenerator;    // Erleichtert die Tipp-Arbeit
  Q query;
//  Q << Q::AndBegin << kunde.Nr.Qi("!=", 7)
//    << Q::OrBegin << Q::Not << kunde.Ort.Qi("LIKE", "%Nor_heim") << kunde.Ort.Qi("=", "Nordheim") << Q::OrEnd
//    << kunde.eintritt.QiBetween("2020-01-01", "2020-06-30") << Q::Not << kunde.status.QiIn({7,8,3}) << Q::AndEnd;
// { "=", "<", "<=", ">", ">=", "<>", " LIKE " };
  TagId currentTag = 0;
  auto it1 = searchList.end();
  auto it2 = searchList.end();
  for (auto it = searchList.begin(); it != searchList.end(); it++) {
    if (not currentTag and it->flag & TagSearchInfo::Msub)
      continue;
    if (not currentTag) {
      currentTag = it->tagId;
      it1 = it;
    } else if (currentTag != it->tagId) {
      it2 = it;
      break;
    }
  }
  // von i1 bis i2 Datenbank-Abfrage, von it2 bis end() filtern
  int cnt = 0;
  std::list<std::string> cl;
  for (auto it = it1; it != it2; ) {
    if ((it->flag & TagSearchInfo::Madd) and it->mode == TagSearchInfo::Mequal) {
      cl.emplace_back(it->tagContent);
      it++;
      if (it != it2 and (it->flag & TagSearchInfo::Madd) and it->mode == TagSearchInfo::Mequal)
        continue;
      if (it != it2 and (it->flag & TagSearchInfo::Madd))
         query << Q::OrBegin;
      else
        query << Q::AndBegin;
      if (cl.size() > 1)
        query << ti.content.QiIn(cl);
      else
        query << ti.content.Qi("=", cl.front());
      continue;
    }

    if ((it->flag & TagSearchInfo::Msub)) {
      auto mode = it->mode;
      std::string content = it->tagContent;
      it++;
//      if (it != it2 and (it->flag & TagSearchInfo::Madd))
//        query << Q::OrBegin;
//      else
//        query << Q::AndBegin;
      std::string op;
      switch (mode) { // { "=", "<", "<=", ">", ">=", "<>", " LIKE " };
        case TagSearchInfo::Mequal: op = "<>"; break;
        case TagSearchInfo::MnotEqual: op = "="; break;
        case TagSearchInfo::Mle: op = ">"; break;
        case TagSearchInfo::Mlt: op = ">="; break;
        case TagSearchInfo::Mge: op = "<"; break;
        case TagSearchInfo::Mgt: op = "<="; break;
        case TagSearchInfo::MnotBeginsWith: query << ti.content.Qi("LIKE", content + "%"); break;
        case TagSearchInfo::MnotContains: query << ti.content.Qi("LIKE", std::string("%") + content + "%"); break;
        case TagSearchInfo::MbeginsWith: query << Q::Not << ti.content.Qi("LIKE", content+ "%"); break;
        case TagSearchInfo::Mcontains: query << Q::Not << ti.content.Qi("LIKE", std::string("%") + content + "%"); break;
        case TagSearchInfo::Many:
        case TagSearchInfo::MnotRegexp:
        case TagSearchInfo::MRegExp:
          THROW("Not implemented");
      }
      if (not op.empty())
        query << ti.content.Qi(op, content);
    }   if ((it->flag & TagSearchInfo::Madd)) {
      auto mode = it->mode;
      std::string content = it->tagContent;
      it++;
      if (it != it2 and (it->flag & TagSearchInfo::Madd))
        query << Q::OrBegin;
      else
        query << Q::AndBegin;
      std::string op;
      switch (mode) { // { "=", "<", "<=", ">", ">=", "<>", " LIKE " };
        case TagSearchInfo::Mequal: op = "="; break;
        case TagSearchInfo::MnotEqual: op = "<>"; break;
        case TagSearchInfo::Mle: op = "<="; break;
        case TagSearchInfo::Mlt: op = "<"; break;
        case TagSearchInfo::Mge: op = ">="; break;
        case TagSearchInfo::Mgt: op = ">"; break;
        case TagSearchInfo::MbeginsWith: query << ti.content.Qi("LIKE", content + "%"); break;
        case TagSearchInfo::Mcontains: query << ti.content.Qi("LIKE", std::string("%") + content + "%"); break;
        case TagSearchInfo::MnotBeginsWith: query << Q::Not << ti.content.Qi("LIKE", content+ "%"); break;
        case TagSearchInfo::MnotContains: query << Q::Not << ti.content.Qi("LIKE", std::string("%") + content + "%"); break;
        case TagSearchInfo::Many:
        case TagSearchInfo::MnotRegexp:
        case TagSearchInfo::MRegExp:
          THROW("Not implemented");
      }
      if (not op.empty())
        query << ti.content.Qi(op, content);
    }



      and it->mode == TagSearchInfo::Mequal) {





      for (auto it0 = std::next(it); ; it0++) {
        if (it0 == it2 or not (it0->flag & TagSearchInfo::Madd) or it0->mode != TagSearchInfo::Mequal) {
          if (std::distance(it, it0) == 1) {
            query << ti.content.Qi("=", it->tagContent);
          } else {}
        }
      }
      tq << ti
    }

  }


}
#endif

