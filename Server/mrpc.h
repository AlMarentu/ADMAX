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


#ifndef MOBS_MRPC_H
#define MOBS_MRPC_H

#include "mobs/objgen.h"
#include "mobs/mchrono.h"
#include <vector>
#include <string>

#define VISITOR(Class) void visit(mobs::ObjVisitor &visitor) override { auto v = dynamic_cast<Class *>(&visitor); if (v) v->visit(*this); }

#ifdef MRPC_SERVER
class XmlInput;
class GetDocument;
class SearchDocument;
class SaveDocument;
class GetConfig;
class Ping;
class GetPub;
class Dump;
class ExecVisitor : virtual public mobs::ObjVisitor {
public:
  ExecVisitor(mobs::XmlOut &xmlOut, XmlInput &xi) : m_xmlOut(xmlOut), m_xi(xi) {}
  void visit(mobs::ObjectBase &obj) override;
  void visit(GetDocument &obj);
  void visit(SearchDocument &obj);
  void visit(SaveDocument &obj);
  void visit(GetConfig &obj);
  void visit(Ping &obj);
  void visit(GetPub &obj);
  void visit(Dump &obj);
  mobs::XmlOut &m_xmlOut;
  XmlInput &m_xi;
};
#endif

class Session : virtual public mobs::ObjectBase
{
public:
  ObjInit(Session);

  MemVar(u_int, id);

};
ObjRegister(Session);


MOBS_ENUM_DEF(SessionErrorIds, SErrUnknown, SErrNeedCredentioal, SErrInvalidCert, SErrNoMoreCon, SErrAccessDenied);
MOBS_ENUM_VAL(SessionErrorIds, "UNK",       "NEED_CREDENTIAL",   "NOC_ERT",       "NO_CON",      "NO_ACC");

class SessionError : virtual public mobs::ObjectBase
{
public:
  ObjInit(SessionError);

  MemMobsEnumVar(SessionErrorIds, error);
  MemVar(std::string, msg);

};

class CommandResult : virtual public mobs::ObjectBase
{
public:
  ObjInit(CommandResult);

  MemVar(int64_t, refId);
  MemVar(uint64_t, docId);
  MemVar(std::string, msg);

};


class SessionLogin : virtual public mobs::ObjectBase
{
public:
  ObjInit(SessionLogin);

  MemVar(std::vector<u_char>, cipher);
};

class SessionLoginData : virtual public mobs::ObjectBase
{
public:
  ObjInit(SessionLoginData);

  MemVar(std::string, login);
  MemVar(std::string, software);
  MemVar(std::string, hostname);
};

class SessionResult : virtual public mobs::ObjectBase
{
public:
  ObjInit(SessionResult);

  MemVar(std::vector<u_char>, key);
  MemVar(u_int, id);
  MemVar(std::string, info);
};

class PublicKey : virtual public mobs::ObjectBase
{
public:
  ObjInit(PublicKey);

  MemVar(u_int, id);
  MemVar(std::string, key);
};



class GetPub : virtual public mobs::ObjectBase
{
public:
  ObjInit(GetPub);

  MemVar(int, id, KEYELEMENT1);
  MemVar(int, cnt);
#ifdef MRPC_SERVER
  VISITOR(ExecVisitor);
#endif
};

class Ping : virtual public mobs::ObjectBase
{
public:
  ObjInit(Ping);

  MemVar(int, id, KEYELEMENT1);
  MemVar(int, cnt);
#ifdef MRPC_SERVER
  VISITOR(ExecVisitor);
#endif
};

class Progress : virtual public mobs::ObjectBase
{
public:
  ObjInit(Progress);

  MemVar(int, percent);
  MemVar(std::string, comment);
};


MOBS_ENUM_DEF(DocumenType, DocumentUnknown, DocumentPdf, DocumentJpeg, DocumentTiff, DocumentHtml, DocumentText);
MOBS_ENUM_VAL(DocumenType, "unk",           "pdf",       "jpg",        "tif",        "htm",        "txt");

class DocumentTags : virtual public mobs::ObjectBase
{
public:
  ObjInit(DocumentTags);

  MemVar(std::string, name);
  MemVar(std::string, content);
};


class DocumentInfo : virtual public mobs::ObjectBase {
public:
  ObjInit(DocumentInfo);

  MemVar(uint64_t, docId);
  MemVector(DocumentTags, tags, USEVECNULL);
  MemVar(std::string, creationInfo, USENULL); /// Art der Erzeugung/Ableitung/Ersetzung
  MemVar(mobs::MTime, creationTime, USENULL); /// Zeitpunkt der Erzeugung, wenn ungleich Eintragezeitpunkt
};

class SearchDocumentResult : virtual public mobs::ObjectBase {
public:
  ObjInit(SearchDocumentResult);

  MemVector(DocumentInfo, tags); // ohne creation-Infos

};

class SearchDocument : virtual public mobs::ObjectBase {
public:
  ObjInit(SearchDocument);

  MemVar(std::string, pool);
  MemVar(std::string, templateName); // für fixedTags und Berechtigung; ist TemplateName gesetzt, wird pool ignoriert
  MemVector(DocumentTags, tags);

#ifdef MRPC_SERVER
  void visit(mobs::ObjVisitor &visitor) override { auto v = dynamic_cast<ExecVisitor *>(&visitor); if (v) v->visit(*this); };
#endif

};

/// Anforderung Document attached
class GetDocument : virtual public mobs::ObjectBase
{
public:
  ObjInit(GetDocument);

  MemVar(uint64_t, docId);
  MemVar(std::string, type);  // TODO sinvoll? evtl. Typ-Konvertierung oder einzelne Seiten
  MemVar(bool, allowAttach);  // große Dokumente dürfen als Attachment gesendet werden
  MemVar(bool, allInfos);     // alle vorhandenen Infos senden
#ifdef MRPC_SERVER
  VISITOR(ExecVisitor);
#endif
};

class Dump : virtual public mobs::ObjectBase
{
public:
  ObjInit(Dump);

  MemVar(int, id, KEYELEMENT1);
#ifdef MRPC_SERVER
  VISITOR(ExecVisitor);
#endif
};

class Document : virtual public mobs::ObjectBase
{
public:
  ObjInit(Document);

  //MemVar(uint64_t, docId);
  MemObj(DocumentInfo, info);
  MemMobsEnumVar(DocumenType, type);
  MemVar(std::string, name);
  MemVar(std::string, pool, USENULL);
  MemVar(std::vector<u_char>, content);

};


class DocumentRaw : virtual public mobs::ObjectBase
{
public:
  ObjInit(DocumentRaw);

//  MemVar(uint64_t, docId);
  MemObj(DocumentInfo, info);
  MemMobsEnumVar(DocumenType, type);
  MemVar(std::string, name);
  MemVar(std::string, pool, USENULL);
  MemVar(int64_t, size);
};


/// Speichern eines Dokumentes als Attachment
class SaveDocument : virtual public mobs::ObjectBase
{
public:
  ObjInit(SaveDocument);

  MemMobsEnumVar(DocumenType, type);
  MemVar(int64_t, refId);
  MemVar(std::string, name);
  MemVar(std::string, pool);
  MemVar(std::string, templateName); // für fixedTags und Berechtigung; entweder Pool oder TemplateName muss gesetzt sein
  MemVar(int64_t, size);
  MemVector(DocumentTags, tags);
  MemVar(uint64_t, supersedeId);
  MemVar(uint64_t, parentId);
  MemVar(std::string, creationInfo);
  MemVar(mobs::MTime, creationTime);
#ifdef MRPC_SERVER
  VISITOR(ExecVisitor);
#endif

};


MOBS_ENUM_DEF(TagType, TagEnumeration, TagDate, TagString, TagIdent, TagDisplay);
MOBS_ENUM_VAL(TagType, "enum",         "date",  "string",  "ident",  "display");

class TemplateTagInfo : virtual public mobs::ObjectBase
{
public:
  ObjInit(TemplateTagInfo);

  MemMobsEnumVar(TagType, type, LENGTH(8));
  MemVar(std::string, name);
  MemVar(std::string, maskText);
  MemVar(std::string, regex);
  MemVar(std::string, format);
  MemVarVector(std::string, enums);
  // TODO MemVarVector(std::string, maskEnums);
  MemVar(bool, hide);
  MemVar(int, maxSize);
  MemVar(bool, grpRepeat); // repeat fpr whole group
  MemVar(int, prio, USENULL); // Prio für BucketInfo 0, 1, 2, 3
};

MOBS_ENUM_DEF(TemplateType, TemplateSearch, TemplateCreate, TemplateEdit, TemplateBucket);
MOBS_ENUM_VAL(TemplateType, "R",            "C",            "U",          "B");

class TemplateInfo : virtual public mobs::ObjectBase
{
public:
  ObjInit(TemplateInfo);

  MemMobsEnumVar(TemplateType, type, LENGTH(1));
  MemVar(std::string, pool, KEYELEMENT1);
  MemVar(std::string, name, KEYELEMENT2, ALTNAME(templNam));
  MemVar(std::string, maskText);

  MemVector(TemplateTagInfo, tags);
  MemVector(DocumentTags, fixTags, USEVECNULL); // Tags, die serverseitig ergänzt werden

};

class GetConfig : virtual public mobs::ObjectBase {
public:
  ObjInit(GetConfig);
  MemVar(bool, start);
#ifdef MRPC_SERVER
  VISITOR(ExecVisitor);
#endif
};

class ConfigResult : virtual public mobs::ObjectBase {
public:
  ObjInit(ConfigResult);
  MemVector(TemplateInfo, templates);

};



#endif //MOBS_MRPC_H
