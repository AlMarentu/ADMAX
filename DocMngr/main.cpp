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


#include <QApplication>
#include <QPushButton>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);
  QTranslator translatorBase;
  if ( translatorBase.load(QLocale(), QStringLiteral("qtbase"), QLatin1String("_"), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
    a.installTranslator(&translatorBase);
  else
    LOG(LM_ERROR, "no base translator for " << QLocale::languageToString(QLocale().language()).toStdString());
  QTranslator translator;
  if (translator.load(QLocale(), QLatin1String("l10n"), QLatin1String("_"), QLatin1String(":/translators")))
    a.installTranslator(&translator);
  else
    LOG(LM_ERROR, "no translator for " << QLocale::languageToString(QLocale().language()).toStdString());

  LOG(LM_INFO, "LIN QM " << QLibraryInfo::location(QLibraryInfo::TranslationsPath).toStdString());
//  QTranslator qtTranslator;
//  a.installTranslator(&qtTranslator);
  MainWindow main;
  main.show();
//  QPushButton button("Hello world!", nullptr);
//  button.resize(200, 100);
//  button.show();
  return QApplication::exec();
}
