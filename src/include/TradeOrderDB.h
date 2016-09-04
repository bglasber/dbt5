/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright (C) 2006-2010 Rilson Nascimento
 *
 * 03 July 2006
 */

#ifndef TRADE_ORDER_DB_H
#define TRADE_ORDER_DB_H

#include "TxnHarnessDBInterface.h"

#include "TxnBaseDB.h"
#include "TxnRestDB.h"
#include "DBConnection.h"
using namespace TPCE;

class CTradeOrderDB : public TxnRestDB, public CTradeOrderDBInterface
{
    public:
        //CTradeOrderDB(CDBConnection *pDBConn) : CTxnBaseDB(pDBConn) {};
        CTradeOrderDB(CDBConnection *pDBConn) : TxnRestDB() {};
        ~CTradeOrderDB() {};

        virtual void DoTradeOrderFrame1(
		int clientId,
		const TTradeOrderFrame1Input *pIn,
                TTradeOrderFrame1Output *pOut);
        virtual void DoTradeOrderFrame2(
		int clientId,
		const TTradeOrderFrame2Input *pIn,
                TTradeOrderFrame2Output *pOut);
        virtual void DoTradeOrderFrame3(
		int clientId,
		const TTradeOrderFrame3Input *pIn,
                TTradeOrderFrame3Output *pOut);
        virtual void DoTradeOrderFrame4(
		int clientId,
		const TTradeOrderFrame4Input *pIn,
                TTradeOrderFrame4Output *pOut);
        virtual void DoTradeOrderFrame5(int clientId);
        virtual void DoTradeOrderFrame6(int clientId);

        // Function to pass any exception thrown inside
        // database class frame implementation
        // back into the database class
        void Cleanup(void* pException) {};
};

#endif	// TRADE_ORDER_DB_H
