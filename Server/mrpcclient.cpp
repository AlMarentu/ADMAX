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


/** \example erweiterter RPC-XML-Client */


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
#include <fstream>
#include <array>


using namespace std;

//Objektdefinitionen
class Ping : virtual public mobs::ObjectBase
{
public:
  ObjInit(Ping);

  MemVar(int, id, KEYELEMENT1);
  MemVar(int, cnt);
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
ObjRegister(Document);

class DocumentRaw : virtual public mobs::ObjectBase
{
public:
  ObjInit(DocumentRaw);

  MemVar(uint64_t, docId);
  MemMobsEnumVar(DocumenType, type);
  MemVar(string, name);
  MemVar(int64_t, size);
};
ObjRegister(DocumentRaw);



class Dump : virtual public mobs::ObjectBase {
public:
  ObjInit(Dump);


  MemVar(int, id, KEYELEMENT1);
};

u_int sessionId = 0;
vector<u_char> sessionKey;


// Hilfsklasse zum Einlesen von XML-Dateien
class XmlInput : public mobs::XmlReader {
public:
  explicit XmlInput(wistream &str) : XmlReader(str) { readTillEof(true); }

  void StartTag(const std::string &element) override {
    LOG(LM_INFO, "start " << element);
    // Wenn passendes Tag gefunden, dann Objekt einlesen
    auto o = mobs::ObjectBase::createObj(element);
    if (o)
      fill(o);
  }
  void EndTag(const std::string &element) override {
    LOG(LM_INFO, "end " << element << " lev " << level());
    if (level() == 2) // Block ende; hier muss ein Delimiter geparst werden, dann kann ein Attachment kommen
      stop();
  }
  void Encrypt(const std::string &algorithm, const std::string &keyName, const std::string &cipher, mobs::CryptBufBase *&cryptBufp) override {
    LOG(LM_INFO, "Encryption " << algorithm << " keyName " << keyName << " cipher " << cipher);
    encrypted = true;
    if (algorithm == "aes-256-cbc") {
      cryptBufp = new mobs::CryptBufAes(sessionKey);
    }
  }
  void EncryptionFinished() override {
    LOG(LM_INFO, "Encryption finished " << level());
    encrypted = false;
    skipDelimiter = true;
    // weiteres parsen abbrechen
    stop();
  }
  void filled(mobs::ObjectBase *obj, const string &error) override {
    readAttachment = 0;
    LOG(LM_INFO, "filled " << obj->to_string() << (error.empty() ? " OK":" ERROR = ") << error);
    if (auto *sess = dynamic_cast<SessionError *>(obj)) {
      LOG(LM_ERROR, "SESSIONERROR " << sess->error.toStr(mobs::ConvObjToString()));
      // parsen abbrechen
      stop();
    } else if (auto *sess = dynamic_cast<SessionResult *>(obj)) {
      LOG(LM_ERROR, "SESSIORESULT " << sess->to_string());
      sessionId = sess->id();
      // Session-Key mit privatem Schlüssel entschlüsseln
      mobs::decryptPrivateRsa(sess->key(), sessionKey, "keystore/client_priv.pem", "12345");
      // parsen abbrechen
      stop();
    } else if (auto *sess = dynamic_cast<Ping *>(obj)) {
      LOG(LM_ERROR, "PING " << sess->to_string());
    } else if (auto *sess = dynamic_cast<Document *>(obj)) {
      LOG(LM_ERROR, "DOCUMENT " << sess->to_string());
    } else if (auto *sess = dynamic_cast<DocumentRaw *>(obj)) {
      LOG(LM_ERROR, "DOCUMENTRAW " << sess->to_string());
      readAttachment = sess->size();
    }
    delete obj;
//    stop(); // optionaler Zwischenstop
  }

  size_t readAttachment = 0;
  bool encrypted = false;
  bool skipDelimiter = false;

};





void client() {
  try {
//    if (sessionKey.size() != mobs::CryptBufAes::key_size()) {
//      sessionKey.resize(mobs::CryptBufAes::key_size());
//      mobs::CryptBufAes::getRand(sessionKey);
//    }

    mobs::tcpstream con("localhost", 4444);
    if (not con.is_open())
      throw runtime_error("cannot connect");

    LOG(LM_INFO, "OK");
    mobs::CryptOstrBuf streambufO(con);
    std::wostream x2out(&streambufO);
    cout << "TTT " << std::boolalpha << x2out.fail() << " " << x2out.tellp() << endl;

    mobs::CryptIstrBuf streambufI(con);
    streambufI.getCbb()->setReadDelimiter('\0');
//    streambufI.getCbb()->setBase64(true);
    std::wistream x2in(&streambufI);
//  x2in >> mobs::CryptBufBase::base64(true);

    XmlInput xr(x2in);


    mobs::XmlWriter xf(x2out, mobs::XmlWriter::CS_utf8, true);
    // XML-Header
    xf.writeHead();
    xf.writeTagBegin(L"methodCall");


//    x2out << mobs::CryptBufBase::base64(true);

    if (sessionId == 0) {
      SessionLoginData data;
      data.login("client");
      data.software("mrpcclient");

      string buffer = data.to_string(mobs::ConvObjToString().exportJson().noIndent());
      vector<u_char> inhalt;
      copy(buffer.begin(), buffer.end(), back_inserter(inhalt));
      vector<u_char> cipher;
      mobs::encryptPublicRsa(inhalt, cipher, "keystore/server.pem");
      SessionLogin login;
      login.cipher(cipher);

      mobs::ConvObjToString cth;
      mobs::XmlOut xo(&xf, cth);

      login.traverse(xo);
      xf.sync();
      LOG(LM_INFO, "login gesendet");

      xr.parse();


    }
    if (sessionId) {
      LOG(LM_INFO, "Session Id = " << sessionId);

      mobs::ConvObjToString cth;
      mobs::XmlOut xo(&xf, cth);

      Session sess;
      sess.id(sessionId);


      cout << "TT3T " << std::boolalpha << x2out.fail() << " " << x2out.tellp() << endl;
      // Listen-Tag

      // Session aktivieren
      sess.traverse(xo);


      vector<u_char> iv;
      iv.resize(mobs::CryptBufAes::iv_size());
      mobs::CryptBufAes::getRand(iv);

//      xf.startEncrypt(new mobs::CryptBufAes("12345"));
      xf.startEncrypt(new mobs::CryptBufAes(sessionKey, iv, "", true));

      // Objekt schreiben
      if (true) {
        Dump d1;
        d1.traverse(xo);
      } else {
        Ping p;
        p.id(1);
        p.cnt(0);
        p.traverse(xo);
      }

      xf.stopEncrypt();
      // Listen-Tag schließen
      xf.writeTagEnd();

      // file schließen
//    x2out << mobs::CryptBufBase::base64(false);

//    x2out << mobs::CryptBufBase::final();
      streambufO.finalize();
      xf.sync();
      LOG(LM_INFO, "ANSWER");
      LOG(LM_INFO, "XOUT bad=" << con.bad() << " written=" << con.tellp());


      LOG(LM_INFO, "XIN bad=" << con.bad() << " eof=" << con.eof() << " read=" << con.tellg());
      // File parsen
      while (not xr.eof()) {
        xr.parse();
        LOG(LM_INFO, "STOPPED  " <<xr.level());

        if (xr.skipDelimiter) {
          xr.skipDelimiter = false;
          int c = con.get();
          LOG(LM_INFO, "C = " << hex << int(c));
          if (c != '\0')
            throw runtime_error("wrong delimiter");
        }

        if (xr.readAttachment and not xr.encrypted) {
          LOG(LM_INFO, "ATTACHMENT " << xr.readAttachment << " " << xr.level());
//          while (c) {
//            c = con.get();
//            LOG(LM_INFO, "XC = " << hex << int(c));
//          }
          std::vector<u_char> iv;
          iv.resize(mobs::CryptBufAes::iv_size());
          mobs::CryptBufAes::getRand(iv);
          mobs::CryptBufAes crypt(sessionKey, iv, "", true);
          crypt.setIstr(con);
          std::istream attach(&crypt);
          crypt.setReadLimit(mobs::CryptBufAes::iv_size() + (xr.readAttachment + 16) / 16 * 16);
          xr.readAttachment = 0;
          static int fcnt = 1;
          std::string name = "tmp";
          name += std::to_string(fcnt++);
          name += ".dat";
          ofstream tmp(name.c_str());
          for (;;) {
            char c;
            if (attach.get(c).eof()) {
              if (attach.bad()) {
                LOG(LM_ERROR, "Attachment read error");
              } else
                LOG(LM_INFO, "Attachment DONE");
              break;
            }
            tmp << c;
          }
          tmp.close();
          LOG(LM_INFO, "ATTACH END");
        }
      }
    }
    else
      LOG(LM_ERROR, "keine Session Id");

    LOG(LM_INFO, "fertig");


  } catch (exception &e) {
    LOG(LM_ERROR, "Worker Exception " << e.what());
  }
}




int main(int argc, char* argv[]) {
//  logging::Trace::traceOn = true;
  TRACE("");

  try {
//    mobs::generateRsaKey("keystore/client_priv.pem", "keystore/client.pem", "12345");
    client();

#if 0
    Gespann f1, f2;

    f1.id(1);
    f1.typ("Brauereigespann");
    f1.zugmaschiene.typ("Sechsspänner");
    f1.zugmaschiene.achsen(0);
    f1.zugmaschiene.antrieb(true);
    f1.haenger[0].typ("Bräuwagen");
    f1.haenger[0].achsen(2);

    f2.id(2);
    f2.typ("Schlepper mit 2 Anhängern");
    f2.zugmaschiene.typ("Traktor");
    f2.zugmaschiene.achsen(2);
    f2.zugmaschiene.antrieb(true);
    f2.haenger[0].typ("Anhänger");
    f2.haenger[0].achsen(2);
    f2.haenger[1].typ("Anhänger");
    f2.haenger[1].achsen(2);


// Ausgabe XML

    mobs::ConvObjToString cth;
//    wofstream xout("gespann.xml", ios::trunc);
//    ofstream xout("gespann.xml", ios::trunc);
    mobs::tcpstream xout("localhost", 5555);
    if (not xout.is_open())
      throw runtime_error("File not open");

    stringstream ss;
//    MyStrBuf<10, wchar_t> streambuf(xout);

//    mobs::CryptOstrBuf streambuf(xout, new mobs::CryptBufAes( "12345"));
    mobs::CryptOstrBuf streambuf(xout);
    std::wostream x2out(&streambuf);
    cout << "TTT " << std::boolalpha << x2out.fail() << " " << x2out.tellp() << endl;

//    x2out << mobs::CryptBufBase::base64(true);

    // Writer-Klasse mit File, und Optionen initialisieren
    mobs::XmlWriter xf(x2out, mobs::XmlWriter::CS_utf8, true);
    xf.setPrefix(L"m:"); // optionaler XML Namespace
    mobs::XmlOut xo(&xf, cth);
    // XML-Header
    xf.writeHead();
    cout << "TT3T " << std::boolalpha << x2out.fail() << " " << x2out.tellp() << endl;
    // Listen-Tag
    xf.writeTagBegin(L"list");

    // Objekt schreiben
    f1.traverse(xo);
    // optionaler Kommentar
    xf.writeComment(L"und noch einer");

    // Objekt schreiben
    f2.traverse(xo);

    string json = R"({
      id:3,
      typ:"PKW",
      zugmaschiene:{
        typ:"PKW",
        achsen:2,
        antrieb:true}
      })";
    f2.clear();
    mobs::string2Obj(json, f2);

    // Objekt schreiben
    f2.traverse(xo);

    // Listen-Tag schließen
    xf.writeTagEnd();
    // file schließen
//    x2out << mobs::CryptBufBase::base64(false);

//    x2out << mobs::CryptBufBase::final();
    streambuf.finalize();
    LOG(LM_INFO, "CLOSE");
    xout.close();
    LOG(LM_INFO, "XOUT bad=" << xout.bad() << " written=" << xout.tellp());
    cout << ss.str() << " " << ss.str().length() << endl;

    // openssl aes-256-cbc  -d -in cmake-build-debug/gespann.xml   -md sha1 -k 12345
    // openssl aes-256-cbc  -d -in cmake-build-debug/gespann.xml -a -A  -md sha1 -k 12345I
    // openssl aes-256-cbc  -d -in c8  -nosalt  -md sha1 -k 12345

//    return 0;
#endif

#if 0
    // File öffnen
//    wifstream xin("gespann.xml");
//    ifstream xin("gespann.xml");
    mobs::tcpstream xin("localhost", 5555);

    if (not xin.is_open())
      throw runtime_error("in-File not open");

//  MyIStrBuf<10, wchar_t> streambufI(xin);
  mobs::CryptIstrBuf streambufI(xin);
//  mobs::CryptIstrBuf streambufI(xin, new mobs::CryptBufAes("12345"));
//    streambufI.getCbb()->setBase64(true);
    std::wistream x2in(&streambufI);
//  x2in >> mobs::CryptBufBase::base64(true);


  // Import-Klasee mit FIle initialisieren
    XmlInput xr(x2in);
    xr.setPrefix("m:"); // optionaler XML Namespace

    while (not xr.eof()) {
      LOG(LM_INFO, "XIN bad=" << xin.bad() << " eof=" << xin.eof() << " read=" << xin.tellg());
      xin.tellg();
      // File parsen
      xr.parse();
      LOG(LM_INFO, "Zwischenpause");
    }

    // fertig
    LOG(LM_INFO, "XIN bad=" << xin.bad() << " eof=" << xin.eof() << " read=" << xin.tellg());
    xin.close();
    LOG(LM_INFO, "XIN bad=" << xin.bad() << " read=" << xin.tellg());


#endif


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
