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

#define REST_QUERY_URL "http://localhost:8080/kronos/rest/query/1"
class TxnRestDB {
    CURL *curl;
    TTradeRequest m_TriggeredLimitOrders;
    CBrokerageHouse *bh;

public:
    TxnRestDB();

    std::vector<Json::Value *> *sendQuery( int clientId, string query );


    char *escape(string s);

    void execute( const TBrokerVolumeFrame1Input *pIn,
                  TBrokerVolumeFrame1Output *pOut );

    void execute( const TCustomerPositionFrame1Input *pIn,
                  TCustomerPositionFrame1Output *pOut );

    void execute( const TCustomerPositionFrame2Input *pIn,
                  TCustomerPositionFrame2Output *pOut );

    void execute( const TDataMaintenanceFrame1Input *pIn );

    void execute( const TMarketFeedFrame1Input *pIn,
                  TMarketFeedFrame1Output *pOut,
                  CSendToMarketInterface *pMarketExchange );

    void execute( const TMarketWatchFrame1Input *pIn,
                  TMarketWatchFrame1Output *pOut );

    void execute( const TSecurityDetailFrame1Input *pIn,
                  TSecurityDetailFrame1Output *pOut );

    void execute( const TTradeCleanupFrame1Input *pIn );

    void execute( const TTradeLookupFrame1Input *pIn,
                  TTradeLookupFrame1Output *pOut );

    void execute( const TTradeLookupFrame2Input *pIn,
                  TTradeLookupFrame2Output *pOut );

    void execute( const TTradeLookupFrame3Input *pIn,
                  TTradeLookupFrame3Output *pOut );

    void execute( const TTradeLookupFrame4Input *pIn,
                  TTradeLookupFrame4Output *pOut );

    void execute( const TTradeOrderFrame1Input *pIn,
                  TTradeOrderFrame1Output *pOut );

    void execute( const TTradeOrderFrame2Input *pIn,
                  TTradeOrderFrame2Output *pOut );

    void execute( const TTradeOrderFrame3Input *pIn,
                  TTradeOrderFrame3Output *pOut );

    void execute( const TTradeOrderFrame4Input *pIn,
                  TTradeOrderFrame4Output *pOut );

    void execute( const TTradeResultFrame1Input *pIn,
                  TTradeResultFrame1Output *pOut );

    void execute( const TTradeResultFrame2Input *pIn,
                  TTradeResultFrame2Output *pOut );

    void execute( const TTradeResultFrame3Input *pIn,
                  TTradeResultFrame3Output *pOut );

    void execute( const TTradeResultFrame4Input *pIn,
                  TTradeResultFrame4Output *pOut );

    void execute( const TTradeResultFrame5Input *pIn );

    void execute( const TTradeResultFrame6Input *pIn,
                  TTradeResultFrame6Output *pOut );

    void execute( const TTradeStatusFrame1Input *pIn,
                  TTradeStatusFrame1Output *pOut );

    void execute( const TTradeUpdateFrame1Input *pIn,
                  TTradeUpdateFrame1Output *pOut );

    void execute( const TTradeUpdateFrame2Input *pIn,
                  TTradeUpdateFrame2Output *pOut );

    void execute( const TTradeUpdateFrame3Input *pIn,
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
