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


#ifndef MOBS_FILESTORE_H
#define MOBS_FILESTORE_H

#include <string>
#include <vector>
#include <iostream>
#include <utility>
#include "mobs/dbifc.h"
#include "mobs/mchrono.h"

using DocId = int64_t;
using TagId = int64_t;
using UserId = int;
enum DocType { DocUnk = 0, DocPdf = 1, DocJpeg = 2, DocTiff = 3, DocHtml = 11, DocText = 21, };

class DocInfo {
public:
  DocId id;
  DocType docType;
  DocId parentId = 0;
  DocId supersedeId = 0;
  std::string creationInfo;
  mobs::MTime creation;
  UserId creator{};
  mobs::MTime insertTime;


};

class TagPool {
public:
  enum TagType { T_NoParam = 0, T_Number = 1, T_String = 2, T_RegExp = 3 };
  TagId tagId;
  TagType tagType;
  std::string tagName;
  std::string tagParam;
  int maxSize;  /// maximum number or maximum length of string
};

class TagInfo {
public:
  TagInfo(TagId id = 0, std::string content = "") : tagId(id), tagContent(std::move(content)) {}
  TagId tagId;
  std::string tagContent;
};

class TagSearch {
public:
  TagSearch(TagId id = 0) : tagId(id) {}
  TagId tagId;
  std::multimap<std::string, std::string> tagOpList{};
};

class SearchResult {
public:
  TagId tagId{};
  std::string tagContent;
  DocId docId{};
};

//class TagSearchInfo {
//public:
//  enum Flags { Madd = 0x000, /// add all matches from db to stock
//               Msub = 0x200, /// remove all matches
//               MCaseSensitive = 0,
//               MCaseInsensitive = 0x1000 };
//  enum Mode { Many, Mequal, MnotEqual, MbeginsWith, MnotBeginsWith, Mcontains, MnotContains,
//               MRegExp, MnotRegexp, Mgt, Mge, Mlt, Mle };
//
//  TagSearchInfo(int id = 0, Mode m = Many, std::string content = "", int flags = Madd) :
//          tagId(id), mode(m), tagContent(std::move(content)), flag(flags) {}
//  TagId tagId;
//  Mode mode;
//  std::string tagContent;
//  int flag;
//};



/** \brief Ablage der Files im Filesystem, SQLite DB
 *
 */
class Filestore {
public:
  static Filestore *instance(const std::string &basedir = "");

  void openDocument(DocId id, std::fstream &stream, bool create);

  std::streamsize docSize(DocId id);
  DocType getType(DocId id);


  void newDocument(DocInfo &doc, std::list<TagInfo>);
  void supersedeDocument(DocInfo &doc, DocId supersedeId);

  void insertTag(std::list<TagInfo> &tagList, const std::string &tagName, const std::string &content);
  TagId findTag(const std::string &tagName);
  std::string tagName(TagId id);

//  void tagSearch(const std::list<TagSearchInfo> &searchList, std::list<SearchResult> &result);
  /// alle Bedingungen oder-verknüpfen
  void tagSearch(const std::map<TagId, TagSearch> &searchList, std::list<SearchResult> &result);
  /// tag info zu einem Dokument
  void tagInfo(DocId id, std::list<SearchResult> &result, DocInfo &info);

  void allDocs(std::vector<DocId> &result);

private:
  explicit Filestore(const std::string &basedir);
  std::string base;
  static Filestore *store;
  static mobs::DatabaseManager dbMgr;

};


#endif //MOBS_FILESTORE_H
