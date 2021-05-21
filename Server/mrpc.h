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

class Session : virtual public mobs::ObjectBase
{
public:
  ObjInit(Session);

  MemVar(u_int, id);

};
ObjRegister(Session);


MOBS_ENUM_DEF(SessionErrorIds, SErrUnknown, SErrNeedCredentioal);
MOBS_ENUM_VAL(SessionErrorIds, "UNK",       "NEED_CREDENTIAL");

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

  MemVar(int64_t, msgId);
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



class Ping : virtual public mobs::ObjectBase
{
public:
  ObjInit(Ping);

  MemVar(int, id, KEYELEMENT1);
  MemVar(int, cnt);
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

  MemVector(DocumentTags, tags);

};

/// Anforderung Document attached
class GetDocument : virtual public mobs::ObjectBase
{
public:
  ObjInit(GetDocument);

  MemVar(uint64_t, docId);
  MemVar(std::string, type);
  MemVar(bool, allowAttach);
  MemVar(bool, allInfos);
};

class Dump : virtual public mobs::ObjectBase
{
public:
  ObjInit(Dump);

  MemVar(int, id, KEYELEMENT1);
};

class Document : virtual public mobs::ObjectBase
{
public:
  ObjInit(Document);

  //MemVar(uint64_t, docId);
  MemObj(DocumentInfo, info);
  MemMobsEnumVar(DocumenType, type);
  MemVar(std::string, name);
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
  MemVar(int64_t, size);
};


/// Speichern eines Dokumentes als Attachment
class SaveDocument : virtual public mobs::ObjectBase
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


};


MOBS_ENUM_DEF(TagType, TagEnumeration, TagDate, TagString, TagIdent);
MOBS_ENUM_VAL(TagType, "enum",         "date",  "string",  "ident");

class TemplateTagInfo : virtual public mobs::ObjectBase
{
public:
  ObjInit(TemplateTagInfo);

  MemMobsEnumVar(TagType, type);
  MemVar(std::string, name);
  MemVar(std::string, maskText);
  MemVar(std::string, regex);
  MemVar(std::string, format);
  MemVarVector(std::string, enums); // TODO maskEnums
  MemVar(bool, hide);
  MemVar(int, maxSize);
};

MOBS_ENUM_DEF(TemplateType, TemplateSearch, TemplateCreate, TemplateEdit);
MOBS_ENUM_VAL(TemplateType, "R",            "C",            "U");

class TemplateInfo : virtual public mobs::ObjectBase
{
public:
  ObjInit(TemplateInfo);

  MemMobsEnumVar(TemplateType, type);
  MemVar(std::string, pool, KEYELEMENT1);
  MemVar(std::string, name, KEYELEMENT2);
  MemVar(std::string, maskText);

  MemVector(TemplateTagInfo, tags);
  MemVector(DocumentTags, fixTags, USEVECNULL); // Tags, die serverseitig ergänzt werden

};

class GetConfig : virtual public mobs::ObjectBase {
public:
  ObjInit(GetConfig);
  MemVar(bool, start);

};

class ConfigResult : virtual public mobs::ObjectBase {
public:
  ObjInit(ConfigResult);
  MemVector(TemplateInfo, templates);

};



#endif //MOBS_MRPC_H
