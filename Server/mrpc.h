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




#endif //MOBS_MRPC_H
