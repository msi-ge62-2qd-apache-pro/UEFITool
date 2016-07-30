/* uefitool.h

  Copyright (c) 2014, Nikolaj Schlej. All rights reserved.
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

  */

#ifndef UEFITOOL_H
#define UEFITOOL_H

#include <QMainWindow>
#include <QByteArray>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QString>
#include <QTreeView>
#include <QUrl>

#include "../common/basetypes.h"
#include "../common/utility.h"
#include "../common/ffs.h"
#include "../common/ffsparser.h"
#include "../common/ffsops.h"
#include "../common/ffsbuilder.h"

#include "searchdialog.h"
#include "hexviewdialog.h"
#include "messagelistitem.h"
#include "ffsfinder.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class UEFITool;
}
QT_END_NAMESPACE

class UEFITool : public QMainWindow
{
    Q_OBJECT

public:
    explicit UEFITool(QWidget *parent = 0);
    ~UEFITool();

    void openImageFile(QString path);
    void setProgramPath(QString path) { currentProgramPath = path; }

private slots:
    void init();
    void populateUi(const QModelIndex &current);
    void scrollTreeView(QListWidgetItem* item);

    void openImageFile();
    void openImageFileInNewWindow();
    void saveImageFile();
    void search();

    void hexView();

    void goToData();
    
    void extract(const UINT8 mode);
    void extractAsIs();
    void extractBody();
    void extractBodyUncompressed();

    void insert(const UINT8 mode);
    void insertInto();
    void insertBefore();
    void insertAfter();

    void replace(const UINT8 mode);
    void replaceAsIs();
    void replaceBody();

    void rebuild();

    void remove();

    void copyMessage();
    void copyAllMessages();
    void enableMessagesCopyActions(QListWidgetItem* item);
    void clearMessages();

    void about();
    void aboutQt();

    void exit();
    void writeSettings();

private:
    Ui::UEFITool* ui;
    TreeModel* model;
    FfsParser* ffsParser;
    FfsFinder* ffsFinder;
    FfsOperations* ffsOps;
    FfsBuilder* ffsBuilder;
    SearchDialog* searchDialog;
    HexViewDialog* hexViewDialog;
    QClipboard* clipboard;
    QString currentDir;
    QString currentProgramPath;
    const QString version;

    bool enableExtractBodyUncompressed(const QModelIndex &current);

    void dragEnterEvent(QDragEnterEvent* event);
    void dropEvent(QDropEvent* event);
    void contextMenuEvent(QContextMenuEvent* event);
    void readSettings();
    void showParserMessages();
    void showFinderMessages();
    void showFitTable();
    void showBuilderMessages();
};

#endif // UEFITOOL_H
