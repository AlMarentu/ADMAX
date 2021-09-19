//
// Created by Matthias Lautner on 15.09.21.
//

#include "passwdDlg.h"
#include "ui_passwd.h"
#include "mobs/logging.h"


PasswdDlg::PasswdDlg(QWidget *parent, QString &oldPass, QString &newPass) : QDialog(parent), ui(new Ui::DialogPasswd),
                                                                            oldPw(oldPass), newPw(newPass) {
  ui->setupUi(this);
  ui->labelInfo->setText(tr("enter old and new password"));
  ui->textBrowser->setPlainText("");
}

PasswdDlg::PasswdDlg(QWidget *parent, QString &newPass) : QDialog(parent), ui(new Ui::DialogPasswd),
                                                          oldPw(newPass), newPw(newPass){
  ui->setupUi(this);
  ui->lineEditOld->setEnabled(false);
  ui->labelOld->setEnabled(false);
  ui->labelInfo->setText(tr("enter new password"));
}


PasswdDlg::~PasswdDlg() {

}

void PasswdDlg::modified(QString text) {
  LOG(LM_INFO, "MOD");
  QString info;
  newPw = ui->lineEditNew->text();
  if (&oldPw != &newPw)
    oldPw = ui->lineEditOld->text();
  ui->pushButtonOk->setEnabled(not newPw.isEmpty() and newPw == ui->lineEditRetype->text());
  if (newPw.length() < 3)
    info = tr("to short");
  else if (newPw != ui->lineEditRetype->text())
    info = tr("the passwords are not identical");

  ui->labelInfo->setText(info);
}
