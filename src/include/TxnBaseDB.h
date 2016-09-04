/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright (C) 2006 Rilson Nascimento
 *               2010 Mark Wong
 *
 * Base class for transacation classes
 * 13 June 2006
 */

#ifndef TXN_BASE_DB_H
#define TXN_BASE_DB_H

#include <string>
using namespace std;

#include "DBT5Consts.h"
using namespace TPCE;

#include "DBConnection.h"
#include "locking.h"

class CTxnBaseDB
{
protected:
	CDBConnection *pDB;

	void commitTransaction();
	string escape(string);

	void execute(int clientId, const TBrokerVolumeFrame1Input *,
			TBrokerVolumeFrame1Output *);

	void execute(int clientId, const TCustomerPositionFrame1Input *,
			TCustomerPositionFrame1Output *);
	void execute(int clientId, const TCustomerPositionFrame2Input *,
			TCustomerPositionFrame2Output *);

	void execute(int clientId, const TDataMaintenanceFrame1Input *);

	void execute(int clientId, const TMarketFeedFrame1Input *, TMarketFeedFrame1Output *,
        CSendToMarketInterface *);

	void execute(int clientId, const TMarketWatchFrame1Input *, TMarketWatchFrame1Output *);

	void execute(int clientId, const TSecurityDetailFrame1Input *,
			TSecurityDetailFrame1Output *);

	void execute(int clientId, const TTradeCleanupFrame1Input *);

	void execute(int clientId, const TTradeLookupFrame1Input *, TTradeLookupFrame1Output *);
	void execute(int clientId, const TTradeLookupFrame2Input *, TTradeLookupFrame2Output *);
	void execute(int clientId, const TTradeLookupFrame3Input *, TTradeLookupFrame3Output *);
	void execute(int clientId, const TTradeLookupFrame4Input *, TTradeLookupFrame4Output *);

	void execute(int clientId, const TTradeOrderFrame1Input *, TTradeOrderFrame1Output *);
	void execute(int clientId, const TTradeOrderFrame2Input *, TTradeOrderFrame2Output *);
	void execute(int clientId, const TTradeOrderFrame3Input *, TTradeOrderFrame3Output *);
	void execute(int clientId, const TTradeOrderFrame4Input *, TTradeOrderFrame4Output *);

	void execute(int clientId, const TTradeResultFrame1Input *, TTradeResultFrame1Output *);
	void execute(int clientId, const TTradeResultFrame2Input *, TTradeResultFrame2Output *);
	void execute(int clientId, const TTradeResultFrame3Input *, TTradeResultFrame3Output *);
	void execute(int clientId, const TTradeResultFrame4Input *, TTradeResultFrame4Output *);
	void execute(int clientId, const TTradeResultFrame5Input *);
	void execute(int clientId, const TTradeResultFrame6Input *, TTradeResultFrame6Output *);

	void execute(int clientId, const TTradeStatusFrame1Input *, TTradeStatusFrame1Output *);

	void execute(int clientId, const TTradeUpdateFrame1Input *, TTradeUpdateFrame1Output *);
	void execute(int clientId, const TTradeUpdateFrame2Input *, TTradeUpdateFrame2Output *);
	void execute(int clientId, const TTradeUpdateFrame3Input *, TTradeUpdateFrame3Output *);

	void reconect();

	void rollbackTransaction();

	void setReadCommitted();
	void setReadUncommitted();
	void setRepeatableRead();
	void setSerializable();

	void startTransaction();

public:
	CTxnBaseDB(CDBConnection *pDB);
	~CTxnBaseDB();
};

#endif // TXN_BASE_DB_H
