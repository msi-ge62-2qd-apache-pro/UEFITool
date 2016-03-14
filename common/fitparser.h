/* fitparser.h

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHWARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/

#ifndef __FITPARSER_H__
#define __FITPARSER_H__

#include <vector>

#include "treemodel.h"
#include "utility.h"
#include "parsingdata.h"
#include "fit.h"
#include "types.h"
#include "treemodel.h"

class TreeModel;

class FitParser
{
public:
    // Default constructor and destructor
    FitParser(TreeModel* treeModel) : model(treeModel) {};
    ~FitParser() {};

    // Returns messages 
    std::vector<std::pair<ModelIndex, CBString> > getMessages() const { return messagesVector; };
    // Clears messages
    void clearMessages() { messagesVector.clear(); };

    STATUS parse(const ModelIndex & index, const ModelIndex & lastVtf);
    std::vector<std::vector<CBString> > getFitTable() const { return fitTable; }

private:
    TreeModel *model;
    std::vector<std::pair<ModelIndex, CBString> > messagesVector;
    ModelIndex lastVtf;
    std::vector<std::vector<CBString> > fitTable;
    
    STATUS findFitRecursive(const ModelIndex & index, ModelIndex & found, UINT32 & fitOffset);
    CBString fitEntryTypeToString(UINT8 type);

    // Message helper
    void msg(const ModelIndex &index, const CBString & message) { messagesVector.push_back(std::pair<ModelIndex, CBString>(index, message)); }
};

#endif
