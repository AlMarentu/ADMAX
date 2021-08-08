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
#include <mobs/converter.h>
#include <set>
#include "mobs/dbifc.h"
#include "mobs/mchrono.h"
#include "mrpc.h"

using DocId = int64_t;
using TagId = int64_t;
using UserId = int;
enum DocType { DocUnk = 0, DocPdf = 1, DocJpeg = 2, DocTiff = 3, DocHtml = 11, DocText = 21, };

class DocInfo {
public:
  DocId id;
  DocType docType;
  std::string fileName; // server internal filename
  int64_t fileSize = 0;
  std::string checkSum;
  DocId parentId = 0;
  DocId supersedeId = 0;
  std::string creationInfo;
  mobs::MTime creation;
  UserId creator{};
  mobs::MTime insertTime;


};

class TagPool {
public:
  enum TagType { T_Enumeration = 0, T_Date = 1, T_String = 2, T_Number = 3,
                 T_Serach7Bit };
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
//  TagSearch(TagId id = 0) : tagId(id) {}
//  TagId tagId;
  std::string tagName;
  bool primary = false;
  std::multimap<std::string, std::string> tagOpList{};
};

class SearchResult {
public:
  TagId tagId{};
  std::string tagContent;
  DocId docId{};
};

class BucketPool {
public:
  class BucketTag {
  public:
    std::string name;
    mobs::StringFormatter formatter{};
    int prio = 0;
    bool displayOnly = false;
  };

  /** \brief
   *
   * @param name
   * @param content
   * @param bucketToken [0] primary, [1..3] Bucket id 1..3
   * @param prioCheck
   * @return true if write in bucket
   */
  bool getToken(const std::string &name, const std::string &content, std::vector<std::string> &bucketToken, std::set<int> &prioCheck);
  bool isBucketTag(const std::string &tagName);
  int getTokenList(const TagSearch &tagSearch, TagSearch &tagResult);
  std::string pool;
  std::map<std::string, BucketTag> elements;
};





/** \brief Ablage der Files im Filesystem, SQLite DB
 *
 */
class Filestore {
public:
  explicit Filestore();
  explicit Filestore(std::string con);
  static void newDbInstance(const std::string &con);

  std::string writeFile(std::istream &source, const DocInfo &info);
  void readFile(const std::string &file, std::ostream &dest);

  void newDocument(DocInfo &doc, const std::list<TagInfo> &tags, int groupId);
  /// schreibt Dateinamen in DB
  void documentCreated(DocInfo &doc);

  void supersedeDocument(DocInfo &doc, DocId supersedeId);

  /// Tag mit name und Inhalt in Liste eintragen
  void insertTag(std::list<TagInfo> &tags, const std::string &pool, const std::string &tagName,
                 const std::string &content, int bucket = 0);
  TagId findTag(const std::string &pool, const std::string &tagName, int bucket = 0);
  std::string tagName(TagId id);

  int findBucket(const std::string &pool, const std::vector<std::string> &bucketToken);
  int findBucket(const std::string &pool, int groupId, const std::string &primToken);

  void bucketSearch(const std::string &pool, const std::map<int, TagSearch> &searchList, std::set<int> &result);

  //  void tagSearch(const std::list<TagSearchInfo> &searchList, std::list<SearchResult> &result);
  /// alle Bedingungen oder-verknüpfen
  std::list<SearchResult> searchTags(const std::string &pool, const std::map<std::string, TagSearch> &searchList,
                                     const std::set<int> &buckets, const std::string &groupName);
  /// tag info zu einem Dokument
  void getTagInfo(DocId id, std::list<SearchResult> &result, DocInfo &doc);
  /// document indo
  void getDocInfo(DocId id, DocInfo &info);

  void allDocs(std::vector<DocId> &result);

  void loadTemplates(std::list<TemplateInfo> &templates);

  void loadBuckets(std::map<std::string, BucketPool> &buckets);

  void loadTemplatesFromFile(const std::string &filename);

  static void setBase(const std::string &basedir);

private:
  static std::string base;
  std::string conName;

};


#endif //MOBS_FILESTORE_H
