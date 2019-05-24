#ifndef __TXN_REST_DB_H__
#define __TXN_REST_DB_H__
#include <curl/curl.h>
#include <vector>
#include "TxnHarnessStructs.h"
#include "TxnHarnessSendToMarket.h"

#include "BrokerageHouse.h"
#include "DBT5Consts.h"
#include "json.h"
using TPCE::TBrokerVolumeFrame1Input;
using TPCE::TBrokerVolumeFrame1Output;

#define REST_QUERY_URL "http://%s:%s/kronos/rest/query/%d"
class TxnRestDB {
    CURL *curl;
    TTradeRequest m_TriggeredLimitOrders;
    CBrokerageHouse *bh;
    string host;
    string port;

public:
    TxnRestDB();

    std::vector<Json::Value *> *sendQuery( int clientId, string query );


    char *escape(string s);

    void execute( int clientId, const TBrokerVolumeFrame1Input *pIn,
                  TBrokerVolumeFrame1Output *pOut );

    void execute( int clientId, const TCustomerPositionFrame1Input *pIn,
                  TCustomerPositionFrame1Output *pOut );

    void execute( int clientId, const TCustomerPositionFrame2Input *pIn,
                  TCustomerPositionFrame2Output *pOut );

    void execute( int clientId, const TDataMaintenanceFrame1Input *pIn );

    void execute( int clientId, const TMarketFeedFrame1Input *pIn,
                  TMarketFeedFrame1Output *pOut,
                  CSendToMarketInterface *pMarketExchange );

    void execute( int clientId, const TMarketWatchFrame1Input *pIn,
                  TMarketWatchFrame1Output *pOut );

    void execute( int clientId, const TSecurityDetailFrame1Input *pIn,
                  TSecurityDetailFrame1Output *pOut );

    void execute( int clientId, const TTradeCleanupFrame1Input *pIn );

    void execute( int clientId, const TTradeLookupFrame1Input *pIn,
                  TTradeLookupFrame1Output *pOut );

    void execute( int clientId, const TTradeLookupFrame2Input *pIn,
                  TTradeLookupFrame2Output *pOut );

    void execute( int clientId, const TTradeLookupFrame3Input *pIn,
                  TTradeLookupFrame3Output *pOut );

    void execute( int clientId, const TTradeLookupFrame4Input *pIn,
                  TTradeLookupFrame4Output *pOut );

    void execute( int clientId, const TTradeOrderFrame1Input *pIn,
                  TTradeOrderFrame1Output *pOut );

    void execute( int clientId, const TTradeOrderFrame2Input *pIn,
                  TTradeOrderFrame2Output *pOut );

    void execute( int clientId, const TTradeOrderFrame3Input *pIn,
                  TTradeOrderFrame3Output *pOut );

    void execute( int clientId, const TTradeOrderFrame4Input *pIn,
                  TTradeOrderFrame4Output *pOut );

    void execute( int clientId, const TTradeResultFrame1Input *pIn,
                  TTradeResultFrame1Output *pOut );

    void execute( int clientId, const TTradeResultFrame2Input *pIn,
                  TTradeResultFrame2Output *pOut );

    void execute( int clientId, const TTradeResultFrame3Input *pIn,
                  TTradeResultFrame3Output *pOut );

    void execute( int clientId, const TTradeResultFrame4Input *pIn,
                  TTradeResultFrame4Output *pOut );

    void execute( int clientId, const TTradeResultFrame5Input *pIn );

    void execute( int clientId, const TTradeResultFrame6Input *pIn,
                  TTradeResultFrame6Output *pOut );

    void execute( int clientId, const TTradeStatusFrame1Input *pIn,
                  TTradeStatusFrame1Output *pOut );

    void execute( int clientId, const TTradeUpdateFrame1Input *pIn,
                  TTradeUpdateFrame1Output *pOut );

    void execute( int clientId, const TTradeUpdateFrame2Input *pIn,
                  TTradeUpdateFrame2Output *pOut );

    void execute( int clientId, const TTradeUpdateFrame3Input *pIn,
                  TTradeUpdateFrame3Output *pOut );

    void reconnect();
    void rollbackTransaction();
    void setBrokerageHouse( CBrokerageHouse *bh );
    void setReadCommitted();
    void setReadUncommitted();
    void setRepeatableRead();
    void setSerializable();

    void startTransaction();
    void commitTransaction();
    ~TxnRestDB();
};
#endif //__TXN_REST_DB_H__
