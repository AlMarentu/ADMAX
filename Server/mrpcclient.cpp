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
#include "mobs/mchrono.h"
#include "mrpc.h"
#include <fstream>
#include <sstream>
#include <array>
#include <getopt.h>


using namespace std;

//Objektdefinitionen

ObjRegister(Ping);

ObjRegister(SessionError);
ObjRegister(CommandResult);
ObjRegister(SessionLogin);
ObjRegister(SessionResult);

ObjRegister(Document);
ObjRegister(DocumentRaw);






u_int sessionId = 0;
vector<u_char> sessionKey;


// Hilfsklasse zum Einlesen von XML-Dateien
class XmlInput : public mobs::XmlReader {
public:
  explicit XmlInput(wistream &str, mobs::tcpstream &con, const string &priv, const string &pass) :
          XmlReader(str), connection(con), privkey(priv), passwd(pass) { readTillEof(true); }

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
    int c = connection.get();
    LOG(LM_INFO, "C = " << hex << int(c));
    if (c != '\0')
      throw runtime_error("wrong delimiter");
    // weiteres parsen abbrechen
    stop();
  }
  void filled(mobs::ObjectBase *obj, const string &error) override {
    readAttachment = 0;
    LOG(LM_INFO, "filled " << obj->to_string() << (encrypted? " OK":" UNENCRYPTED"));
    if (not error.empty())
      THROW("error in XML stream: " << error);
    if (auto *sess = dynamic_cast<SessionError *>(obj)) {
      LOG(LM_ERROR, "SESSIONERROR " << sess->error.toStr(mobs::ConvObjToString()));
      // parsen abbrechen
      stop();
    } else if (auto *sess = dynamic_cast<CommandResult *>(obj)) {
      lastRefId = sess->refId();
      if (sess->msg() != "OK") {
        if (sess->refId() > 0)
          LOG(LM_ERROR, "ERROR in RefId " << sess->refId() << ": " << sess->msg());
        else
          THROW("MsgResult = " << sess->msg());
      }
    } else if (auto *sess = dynamic_cast<SessionResult *>(obj)) {
      LOG(LM_ERROR, "SESSIORESULT " << sess->to_string());
      sessionId = sess->id();
      // Session-Key mit privatem Schlüssel entschlüsseln
      mobs::decryptPrivateRsa(sess->key(), sessionKey, privkey, passwd);
      // parsen abbrechen
      stop();
    } else if (auto *sess = dynamic_cast<Ping *>(obj)) {
      LOG(LM_ERROR, "PING " << sess->to_string());
    } else if (auto *sess = dynamic_cast<Document *>(obj)) {
      LOG(LM_ERROR, "DOCUMENT " << sess->to_string());
      if (dumpStr.is_open()) {
          mobs::XmlOut xo(xout, mobs::ConvObjToString().exportXml());
          sess->traverse(xo);
          xo.sync();
        }
    } else if (auto *sess = dynamic_cast<DocumentRaw *>(obj)) {
      LOG(LM_ERROR, "DOCUMENTRAW " << sess->to_string());
      readAttachment = sess->size();
      if (dumpStr.is_open()) {
        mobs::XmlOut xo(xout, mobs::ConvObjToString().exportXml());
        sess->traverse(xo);
        xo.sync();
      }
    }
    delete obj;
//    stop(); // optionaler Zwischenstop
  }

  mobs::tcpstream &connection;
  string privkey;
  string passwd;
  size_t readAttachment = 0;
  bool encrypted = false;
  fstream dumpStr;
  mobs::XmlWriter *xout = nullptr;
  int64_t lastRefId = 0;

};

class XmlDump : public mobs::XmlReader {
public:
  explicit XmlDump(wistream &str) : XmlReader(str) { readTillEof(false); }

  void StartTag(const std::string &element) override {
    LOG(LM_INFO, "start " << element);
    // Wenn passendes Tag gefunden, dann Objekt einlesen
    auto o = mobs::ObjectBase::createObj(element);
    if (o)
      fill(o);
  }
  void EndTag(const std::string &element) override {
    LOG(LM_INFO, "end " << element << " lev " << level());
//    if (level() == 2) // Block ende; hier muss ein Delimiter geparst werden, dann kann ein Attachment kommen
//      stop();
  }
  void filled(mobs::ObjectBase *obj, const string &error) override {
    lastObj = obj;
    if (not error.empty())
      THROW("XML-Parse error " << error);
    LOG(LM_INFO, "filled " << obj->to_string());
    // parsen abbrechen
    stop();
  }

  mobs::ObjectBase *lastObj = nullptr;
  size_t parseAttachment = 0;
};


void doRestore(mobs::tcpstream &con, XmlInput &xr, mobs::XmlWriter &xf, mobs::XmlOut &xo) {
  if (not xr.dumpStr.is_open())
    THROW("cannot open dump file");
  LOG(LM_INFO, "READ DUMP");
  mobs::CryptIstrBuf dumpStrbufI(xr.dumpStr);
  dumpStrbufI.getCbb()->setReadDelimiter('\0');
  std::wistream xin(&dumpStrbufI);
  XmlDump xd(xin);
  int count = 0;
  while (xr.dumpStr.good()) {
    xd.lastObj = nullptr;
    xd.parse();
    LOG(LM_INFO, "LEV " << xd.level());
    if (xd.lastObj and count) {
//            LOG(LM_INFO, "PARSE");
//            xr.parse();
//            LOG(LM_INFO, "PARSE DONE");
//


      vector<u_char> iv;
      iv.resize(mobs::CryptBufAes::iv_size());
      mobs::CryptBufAes::getRand(iv);
      xf.startEncrypt(new mobs::CryptBufAes(sessionKey, iv, "", true));
    }
    if (xd.lastObj) {
      count++;
      LOG(LM_INFO, "OBJ " << xd.lastObj->to_string());
      if (auto obj = dynamic_cast<DocumentRaw *>(xd.lastObj)) {
        dumpStrbufI.getCbb()->setReadDelimiter();
        dumpStrbufI.getCbb()->setReadLimit(obj->size() + 1);
        int c = dumpStrbufI.getCbb()->sbumpc();
        LOG(LM_INFO, "C = " << c);
        if (c)
          THROW("invalid delimiter");
        std::vector<char> buf;
        buf.resize(obj->size());
        streamsize sz = dumpStrbufI.getCbb()->sgetn(&buf[0], buf.size());
        if (sz != obj->size())
          THROW("eof reached");
        dumpStrbufI.getCbb()->setReadDelimiter('\0');
        dumpStrbufI.getCbb()->setReadLimit();

        SaveDocument sd;
        sd.name(obj->name());
        sd.tags(obj->info.tags);
        sd.type(obj->type());
        sd.size(obj->size());
        sd.creationInfo(obj->info.creationInfo());
        sd.creationTime(obj->info.creationTime());

        LOG(LM_INFO, "GENERATE " << sd.to_string());
        sd.traverse(xo);
        xf.stopEncrypt();
        xf.putc(L'\0');
        xf.sync();

        LOG(LM_INFO, "Start attachment size=" << sz);
        vector<u_char> iv;
        iv.resize(mobs::CryptBufAes::iv_size());
        mobs::CryptBufAes::getRand(iv);
        mobs::CryptBufAes cry(sessionKey, iv, "", true);
        cry.setOstr(con);
        ostream ostb(&cry);
        ostb.write(&buf[0], buf.size());
        cry.finalize();


        // debug-File
        static int fcnt = 1;
        std::string name = "tmpd";
        name += std::to_string(fcnt++);
        name += ".dat";
        ofstream tmp(name.c_str());
        tmp.write(&buf[0], buf.size());
        tmp.close();
      } else if (auto obj = dynamic_cast<Document *>(xd.lastObj)) {
        SaveDocument sd;
        sd.name(obj->name());
        sd.tags(obj->info.tags);
        sd.type(obj->type());
        sd.creationInfo(obj->info.creationInfo());
        sd.creationTime(obj->info.creationTime());
        sd.size(obj->content().size());

        LOG(LM_INFO, "GENERATE " << sd.to_string());
        sd.traverse(xo);
        xf.stopEncrypt();
        xf.putc(L'\0');
        xf.sync();

        LOG(LM_INFO, "Start attachment size=" << obj->content().size());
        vector<u_char> iv;
        iv.resize(mobs::CryptBufAes::iv_size());
        mobs::CryptBufAes::getRand(iv);
        mobs::CryptBufAes cry(sessionKey, iv, "", true);
        cry.setOstr(con);
        ostream ostb(&cry);
        ostb.write((char *)&obj->content()[0], obj->content().size());
        cry.finalize();


      }

      if (count > 2) {
        // Verzögert die Results auswerten, damit keine unnütze Wartezeit entsteht
        LOG(LM_INFO, "PARSE " << count);
        xr.parse();
        LOG(LM_INFO, "STOPPED  " << xr.level());
      }
    } else {
      LOG(LM_INFO, "NO OBJ");
//            break;
    }
    delete xd.lastObj;
    xd.lastObj = nullptr;
  }
};

void doImport(mobs::tcpstream &con, XmlInput &xr, mobs::XmlWriter &xf, mobs::XmlOut &xo, const string &path, size_t skip) {
  if (not xr.dumpStr.is_open())
    THROW("cannot open import file");
  LOG(LM_INFO, "READ INPUT");
  xr.dumpStr.exceptions(ios_base::badbit);
  vector<string> head;
  string templateName;
  char buf[1024];
  char delim = '\t';
  xr.dumpStr.getline(buf, sizeof(buf));
  string tmp = buf;
  size_t pos = tmp.find(u8"$template");
  if (pos == string::npos)
    THROW("no '$template' in header");
  if (pos > 0)
    delim = tmp[pos-1];
  if (tmp[tmp.length() -1] == '\r')
    tmp.resize(tmp.length() -1);
  stringstream ss(tmp);
  while (not ss.eof()) {
    ss.getline(buf, sizeof(buf), delim);
    if (not buf[0]) {
      if (ss.eof())
        break;
      strcpy(buf, "$ignore");
    }
    head.emplace_back(buf);
    LOG(LM_INFO, "HEAD " << buf);
  }

  int count = 0;
  while (not xr.dumpStr.eof()) {
    xr.dumpStr.getline(buf, sizeof(buf));
    string tmp = buf;
    SaveDocument sd;
    sd.refId(count +1); // line of input
    if (tmp[tmp.length() -1] == '\r')
      tmp.resize(tmp.length() -1);
    string filename;
    stringstream ss(tmp);
    auto iter = head.cbegin();
    while (not ss.eof()) {
      if (iter == head.end())
        break;
      ss.getline(buf, sizeof(buf), delim);
//      LOG(LM_INFO, "TOK " << *iter << " CONT " << buf);
      if (*iter == "$creation") {
        mobs::MTime t;
        if (mobs::string2x(buf, t))
          sd.creationTime(t);
        else
          LOG(LM_ERROR, "invalide DateTime " << buf);
      } else if (*iter == "$template") {
        if (buf[0])
          templateName = buf;
      } else if (*iter == "$filename") {
        filename = buf;
      } else if (not iter->empty() and buf[0] and (*iter)[0] != '$') {
        auto &t = sd.tags[mobs::MemBaseVector::nextpos];
        t.name(*iter);
        t.content(buf);
      }
      iter++;
    }
    if (skip > 0) {
      skip--;
      continue;
    }

    if (filename.empty()) {
      LOG(LM_ERROR, "missing file name " << filename);
      continue;
    }
    if (filename[0] != '/' and not path.empty())
      filename = STRSTR(path << '/' << filename);

    size_t pos = filename.rfind('.');
    string ext;
    if (pos != string::npos)
      ext = mobs::toUpper(filename.substr(pos+1));
    if (ext == "PDF")
      sd.type(DocumentPdf);
    else if (ext == "TIF" or ext == "TIFF")
      sd.type(DocumentTiff);
    else if (ext == "JPG" or ext == "JPEG")
      sd.type(DocumentJpeg);
    else {
      LOG(LM_ERROR, "invalid file type " << filename);
      continue;
    }
    pos = filename.rfind('/');
    if (pos == string::npos)
      sd.name(filename);
    else
      sd.name(filename.substr(pos+1));

    if (templateName.empty()) {
      LOG(LM_ERROR, "template name missing");
      continue;
    }
    sd.templateName(templateName);

    ifstream data(filename, ios::binary);
    if (not data.is_open()) {
      LOG(LM_INFO, "file " << filename << " not found");
      continue;
    }

    data.seekg(0, ios_base::end);
    sd.size(data.tellg());
    data.seekg(0, ios_base::beg);


    LOG(LM_INFO, "DOC " << sd.to_string());
    count++;

#if 1
    vector<u_char> iv;
    if (xf.cryptingLevel() == 0)
    {
      iv.resize(mobs::CryptBufAes::iv_size());
      mobs::CryptBufAes::getRand(iv);
      xf.startEncrypt(new mobs::CryptBufAes(sessionKey, iv, "", true));
    }
    sd.traverse(xo);
    xf.stopEncrypt();
    xf.putc(L'\0');
    xf.sync();

    LOG(LM_INFO, "Start attachment size=");
    mobs::CryptBufAes::getRand(iv);
    mobs::CryptBufAes cry(sessionKey, iv, "", true);
    cry.setOstr(con);
    ostream ostb(&cry);
    ostb << data.rdbuf();
    cry.finalize();
    data.close();


    while (count > xr.lastRefId + 4 ) {
      // Verzögert die Results auswerten, damit keine unnütze Wartezeit entsteht
      LOG(LM_INFO, "PARSE " << count);
      xr.parse();
      LOG(LM_INFO, "STOPPED  " << xr.level());
    }

#endif
  }
}


void client(const string &mode, const string& server, int port, const string &keystore, const string &keyname, const string &pass,
            const string &file, size_t skip) {
  try {
//    if (sessionKey.size() != mobs::CryptBufAes::key_size()) {
//      sessionKey.resize(mobs::CryptBufAes::key_size());
//      mobs::CryptBufAes::getRand(sessionKey);
//    }

    string serverkey = keystore + "server.pem";
    string privkey = keystore + keyname + "_priv.pem";

    string service = to_string(port);
    mobs::tcpstream con(server, service);
    if (not con.is_open())
      throw runtime_error("can't connect");

    LOG(LM_INFO, "OK");
    mobs::CryptOstrBuf streambufO(con);
    std::wostream x2out(&streambufO);
    cout << "TTT " << std::boolalpha << x2out.fail() << " " << x2out.tellp() << endl;

    mobs::CryptIstrBuf streambufI(con);
    streambufI.getCbb()->setReadDelimiter('\0');
//    streambufI.getCbb()->setBase64(true);
    std::wistream x2in(&streambufI);
//  x2in >> mobs::CryptBufBase::base64(true);

    XmlInput xr(x2in, con, privkey, pass);


    mobs::XmlWriter xf(x2out, mobs::XmlWriter::CS_utf8, true);
    // XML-Header
    xf.writeHead();
    xf.writeTagBegin(L"methodCall");


//    x2out << mobs::CryptBufBase::base64(true);

    if (sessionId == 0) {
      SessionLoginData data;
      data.login(keyname);
      data.software("mrpcclient");

      string buffer = data.to_string(mobs::ConvObjToString().exportJson().noIndent());
      vector<u_char> inhalt;
      copy(buffer.begin(), buffer.end(), back_inserter(inhalt));
      vector<u_char> cipher;
      mobs::encryptPublicRsa(inhalt, cipher, serverkey);
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

      xf.startEncrypt(new mobs::CryptBufAes(sessionKey, iv, "", true));

      // Objekt schreiben
      if (mode == "dump") {
        xr.dumpStr.open(file, ios::trunc | ios::binary | ios::out);
        if (not xr.dumpStr.is_open())
          THROW("cannot open dump file");
        static mobs::CryptOstrBuf dumpStrbuf(xr.dumpStr);
        static std::wostream xostr(&dumpStrbuf);
        xr.xout = new mobs::XmlWriter(xostr, mobs::XmlWriter::CS_utf8, true);
        xr.xout->writeHead();
        xr.xout->writeTagBegin(L"dump");

        Dump d1;
        d1.traverse(xo);
      } else if (mode == "restore") {
        xr.dumpStr.open(file, ios::binary | ios::in);
        doRestore(con, xr, xf, xo);
      } else if (mode == "import") {
        xr.dumpStr.open(file, ios::in);
        size_t pos = file.rfind('/');
        string path = file;
        if (pos == string::npos)
          pos = 0;
        path.resize(pos);
        doImport(con, xr, xf, xo, path, skip);
      } else {
        Ping p;
        p.id(1);
        p.cnt(0);
        p.traverse(xo);
      }

      if (mode != "restore")
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
      // File abschließend  parsen
      while (not xr.eof()) {
        LOG(LM_INFO, "PARSE");
        xr.parse();
        LOG(LM_INFO, "STOPPED  " <<xr.level());

        if (xr.readAttachment and not xr.encrypted) {
          LOG(LM_INFO, "ATTACHMENT " << xr.readAttachment << " " << xr.level());
          mobs::CryptBufAes crypt(sessionKey);
          crypt.setIstr(con);
          std::istream attach(&crypt);
          crypt.setReadLimit(mobs::CryptBufAes::iv_size() + (xr.readAttachment + 16) / 16 * 16);
          xr.readAttachment = 0;
          xr.dumpStr << '\0';
#ifdef DUMP_DEBUG
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
            xr.dumpStr << c;
          }
          tmp.close();
#else
          xr.dumpStr << attach.rdbuf();
#endif
          LOG(LM_INFO, "ATTACH END");
        }
      }
    }
    else
      LOG(LM_ERROR, "keine Session Id");

    if (mode == "dump")
    {
      if (not xr.dumpStr.is_open())
        THROW("dump file not open");
      xr.xout->writeTagEnd();
      xr.dumpStr.close();
    }

    LOG(LM_INFO, "fertig");


  } catch (exception &e) {
    LOG(LM_ERROR, "Worker Exception " << e.what());
  }
}


void usage() {
  cerr << "usage: mrpcclient -c command [-p passphrase] [-k keystore]\n"
       << " -p passphrase default = '12345'\n"
       << " -k keystore directory for keys, default = 'keystore'\n"
       << " -n keyname name for keys, default = 'client'\n"
       << " -S server default = 'localhost'\n"
       << " -P Port default = '4444'\n"
       << " -f filename default = 'admax.dump'\n"
       << " commands:\n"
       << "  genkey ... generate key pair\n"
       << "  dump ... dump database\n"
       << "  restore ... restore database\n"
       << "  import ... import from file\n"
       << "  ping ... ping server\n";
  exit(1);
}

int main(int argc, char* argv[]) {
//  logging::Trace::traceOn = true;
  TRACE("");
  logging::currentLevel = LM_INFO;
  string mode;
  string passphrase = "12345";
  string keystore = "keystore";
  string keyname = "client";
  string server = "localhost";
  string filename = "admax.dump";
  int port = 4444;
  size_t skip = 0;

  try {
    char ch;
    while ((ch = getopt(argc, argv, "c:p:n:S:P:f:s:")) != -1) {
      switch (ch) {
        case 'c':
          mode = optarg;
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
        case 'S':
          server = optarg;
          break;
        case 's':
          skip = stoul(optarg);
          break;
        case 'f':
          filename = optarg;
          break;
        case 'P':
          port = stoi(string(optarg));
          break;
        case '?':
        default:
          usage();
      }
    }
    if (keystore.rfind('/') != keystore.length() - 1)
      keystore += '/';

    if (mode.empty())
      usage();
    if (mode == "genkey") {
      mobs::generateRsaKey(keystore + keyname + "_priv.pem", keystore + keyname + ".pem", passphrase);
      exit(0);
    }

    ifstream k(keystore + keyname + "_priv.pem");
    if (not k.is_open()) {
      cerr << "private key not found - generate with -c genkey" << endl;
      exit(1);
    }
    k.close();


    client(mode, server, port, keystore, keyname, passphrase, filename, skip);


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
