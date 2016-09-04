/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright (C) 2006-2007 Rilson Nascimento
 *
 * 17 July 2006
 */

#ifndef DATA_MAINTENANCE_DB_H
#define DATA_MAINTENANCE_DB_H

#include <TxnHarnessDBInterface.h>

#include "TxnBaseDB.h"
#include "TxnRestDB.h"

class CDataMaintenanceDB : public TxnRestDB, public CDataMaintenanceDBInterface
{
    public:
        //CDataMaintenanceDB(CDBConnection *pDBConn) : CTxnBaseDB(pDBConn) {};
        CDataMaintenanceDB(CDBConnection *pDBConn) : TxnRestDB() {};
        ~CDataMaintenanceDB() {};

        void DoDataMaintenanceFrame1(int clientId, const TDataMaintenanceFrame1Input *pIn);

        // Function to pass any exception thrown inside
        // database class frame implementation
        // back into the database class
        void Cleanup(void* pException) {};

};

#endif	// DATA_MAINTENANCE_DB_H
