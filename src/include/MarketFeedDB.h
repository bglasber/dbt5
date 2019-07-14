/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright (C) 2006-2007 Rilson Nascimento
 *
 * 21 July 2006
 */

#ifndef MARKET_FEED_DB_H
#define MARKET_FEED_DB_H

#include <TxnHarnessDBInterface.h> 

#include "TxnBaseDB.h"
#include "TxnRestDB.h"

class CMarketFeedDB : public TxnRestDB, public CMarketFeedDBInterface
{
    public:
        //CMarketFeedDB(CDBConnection *pDBConn) : CTxnBaseDB(pDBConn) {};
        CMarketFeedDB(CBrokerageHouse *bh, CDBConnection *pDBConn) : TxnRestDB(bh) {};
        CMarketFeedDB(CMEESUTtest *t, CDBConnection *pDBConn) : TxnRestDB(t) {};
        ~CMarketFeedDB() {};

        virtual void DoMarketFeedFrame1(
		int clientId,
		const TMarketFeedFrame1Input *pIn,
                TMarketFeedFrame1Output *pOut,
                CSendToMarketInterface *pSendToMarket);

        // Function to pass any exception thrown inside
        // database class frame implementation
        // back into the database class
        void Cleanup(void* pException) {};
};

#endif	// MARKET_FEED_DB_H
