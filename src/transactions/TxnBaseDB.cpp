/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright (C) 2006-2007 Rilson Nascimento
 *               2010      Mark Wong
 *
 * 13 June 2006
 */

#include "TxnBaseDB.h"

#include "TxnHarnessSendToMarketInterface.h"

CTxnBaseDB::CTxnBaseDB(CDBConnection *pDB)
{
	this->pDB = pDB;
}

CTxnBaseDB::~CTxnBaseDB()
{
}

void CTxnBaseDB::commitTransaction()
{
	pDB->commit();
}

string CTxnBaseDB::escape(string s)
{
	return pDB->escape(s);
}

void CTxnBaseDB::execute(int clientId, const TBrokerVolumeFrame1Input *pIn,
		TBrokerVolumeFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TCustomerPositionFrame1Input *pIn,
		TCustomerPositionFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TCustomerPositionFrame2Input *pIn,
		TCustomerPositionFrame2Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TDataMaintenanceFrame1Input *pIn)
{
	pDB->execute(clientId, pIn);
}

void CTxnBaseDB::execute(int clientId, const TMarketFeedFrame1Input *pIn,
		TMarketFeedFrame1Output *pOut, CSendToMarketInterface *pMarketExchange)
{
	pDB->execute(clientId, pIn, pOut, pMarketExchange);
}

void CTxnBaseDB::execute(int clientId, const TMarketWatchFrame1Input *pIn,
		TMarketWatchFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TSecurityDetailFrame1Input *pIn,
		TSecurityDetailFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeCleanupFrame1Input *pIn)
{
	pDB->execute(clientId, pIn);
}

void CTxnBaseDB::execute(int clientId, const TTradeLookupFrame1Input *pIn,
		TTradeLookupFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeLookupFrame2Input *pIn,
		TTradeLookupFrame2Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeLookupFrame3Input *pIn,
		TTradeLookupFrame3Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeLookupFrame4Input *pIn,
		TTradeLookupFrame4Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeOrderFrame1Input *pIn,
		TTradeOrderFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeOrderFrame2Input *pIn,
		TTradeOrderFrame2Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeOrderFrame3Input *pIn,
		TTradeOrderFrame3Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeOrderFrame4Input *pIn,
		TTradeOrderFrame4Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeResultFrame1Input *pIn,
		TTradeResultFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeResultFrame2Input *pIn,
		TTradeResultFrame2Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeResultFrame3Input *pIn,
		TTradeResultFrame3Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeResultFrame4Input *pIn,
		TTradeResultFrame4Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeResultFrame5Input *pIn)
{
	pDB->execute(clientId, pIn);
}

void CTxnBaseDB::execute(int clientId, const TTradeResultFrame6Input *pIn,
		TTradeResultFrame6Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeStatusFrame1Input *pIn,
		TTradeStatusFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeUpdateFrame1Input *pIn, TTradeUpdateFrame1Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeUpdateFrame2Input *pIn,
		TTradeUpdateFrame2Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::execute(int clientId, const TTradeUpdateFrame3Input *pIn,
		TTradeUpdateFrame3Output *pOut)
{
	pDB->execute(clientId, pIn, pOut);
}

void CTxnBaseDB::rollbackTransaction()
{
	pDB->rollback();
}

void CTxnBaseDB::startTransaction()
{
	pDB->begin();
}

void CTxnBaseDB::setReadCommitted()
{
	pDB->setReadCommitted();
}

void CTxnBaseDB::setReadUncommitted()
{
	pDB->setReadUncommitted();
}

void CTxnBaseDB::setRepeatableRead()
{
	pDB->setRepeatableRead();
}

void CTxnBaseDB::setSerializable()
{
	pDB->setSerializable();
}
