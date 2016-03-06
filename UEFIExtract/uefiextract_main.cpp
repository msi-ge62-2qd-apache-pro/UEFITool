/* uefiextract_main.cpp

Copyright (c) 2016, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/
#include <QCoreApplication>
#include <QString>
#include <QFileInfo>

#include <iostream>

#include "../common/ffsparser.h"
#include "uefiextract.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setOrganizationName("CodeRush");
    a.setOrganizationDomain("coderush.me");
    a.setApplicationName("UEFIExtract");

    if (a.arguments().length() > 32) {
        std::cout << "Too many arguments" << std::endl;
        return 1;
    }

    if (a.arguments().length() > 1) {
        QString path = a.arguments().at(1);
        QFileInfo fileInfo(path);
        if (!fileInfo.exists())
            return ERR_FILE_OPEN;

        QFile inputFile;
        inputFile.setFileName(path);
        if (!inputFile.open(QFile::ReadOnly))
            return ERR_FILE_OPEN;

        QByteArray buffer = inputFile.readAll();
        inputFile.close();

        TreeModel model;
        FfsParser ffsParser(&model);
        STATUS result = ffsParser.parse(ByteArray(buffer.constData(), buffer.size()));
        if (result)
            return result;

        std::vector<std::pair<ModelIndex, CBString> > messages = ffsParser.getMessages();
        for (size_t i = 0; i < messages.size(); i++) {
            std::cout << messages[i].second << std::endl;
        }

        UEFIExtract uefiextract(&model);

        if (a.arguments().length() == 2) {
            return (uefiextract.dump(model.index(0, 0), fileInfo.fileName().append(".dump").toLocal8Bit().constData()) != ERR_SUCCESS);
        }
        else {
            UINT32 returned = 0;
            for (int i = 2; i < a.arguments().length(); i++) {
                result = uefiextract.dump(model.index(0, 0), fileInfo.fileName().append(".dump").toLocal8Bit().constData(), a.arguments().at(i).toLocal8Bit().constData());
                if (result)
                    returned |= (1 << (i - 1));
            }
            return returned;
        }
    }
    else {
        std::cout << "UEFIExtract 0.10.8" << std::endl << std::endl
                  << "Usage: UEFIExtract imagefile [FileGUID_1 FileGUID_2 ... FileGUID_31]" << std::endl
                  << "Return value is a bit mask where 0 at position N means that file with GUID_N was found and unpacked, 1 otherwise" << std::endl;
        return 1;
    }
}
