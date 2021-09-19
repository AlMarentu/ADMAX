//
// Created by Matthias Lautner on 15.09.21.
//

#ifndef ADMAX_PASSWDDLG_H
#define ADMAX_PASSWDDLG_H


namespace Ui {
class DialogPasswd;
}

#include <QDialog>

class PasswdDlg : public QDialog {
  Q_OBJECT
public:
  PasswdDlg(QWidget *parent, QString &oldPass, QString &newPass);
  PasswdDlg(QWidget *parent, QString &newPass);
  ~PasswdDlg();

public slots:
  void modified(QString text);

private:
  Ui::DialogPasswd *ui;
  QString &oldPw;
  QString &newPw;
};


#endif //ADMAX_PASSWDDLG_H
