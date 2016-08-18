#include <curl/curl.h>
#include <sstream>
#include "TxnRestDB.h"
#include "TxnHarnessStructs.h"
#include "TxnHarnessSendToMarket.h" #include "DBT5Consts.h"
#include "json.h"

char *TxnRestDB::escape(string s)
{
    //PQescapeLiteral
    //malloc and ecape chars
    return NULL;
}


bool inline check_count(int should, int is, const char *file, int line) {
    if (should != is) {
        cout << "*** array length (" << is <<
            ") does not match expections (" << should << "): " << file <<
            ":" << line << endl;
        return false;
    }
    return true;
}

// Array Tokenizer
void inline TokenizeArray(const string& str2, vector<string>& tokens)
{
    // This is essentially an empty array. i.e. '{}'
    if (str2.size() < 3)
        return;

    // We only call this function because we need to chop up arrays that
    // are in the format '{{1,2,3},{a,b,c}}', so trim off the braces.
    string str = str2.substr(1, str2.size() - 2);

    // Skip delimiters at beginning.
    string::size_type lastPos = str.find_first_of("{", 0);
    // Find first "non-delimiter".
    string::size_type pos = str.find_first_of("}", lastPos);

    while (string::npos != pos || string::npos != lastPos)
    {
        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos + 1));

        lastPos = str.find_first_of("{", pos);
        pos = str.find_first_of("}", lastPos);
    }
}

// String Tokenizer
// FIXME: This token doesn't handle strings with escaped characters.
void inline TokenizeSmart(const string& str, vector<string>& tokens)
{
    // This is essentially an empty array. i.e. '{}'
    if (str.size() < 3)
        return;

    string::size_type lastPos = 1;
    string::size_type pos = 1;
    bool end = false;
    while (end == false)
    {
        if (str[lastPos] == '"') {
            pos = str.find_first_of("\"", lastPos + 1);
            if (pos == string::npos) {
                pos = str.find_first_of("}", lastPos);
                end = true;
            }
            tokens.push_back(str.substr(lastPos + 1, pos - lastPos - 1));
            lastPos = pos + 2;
        } else if (str[lastPos] == '\0') {
            return;
        } else {
            pos = str.find_first_of(",", lastPos);
            if (pos == string::npos) {
                pos = str.find_first_of("}", lastPos);
                end = true;
            }
            tokens.push_back(str.substr(lastPos, pos - lastPos));
            lastPos = pos + 1;
        }
    }
}

TxnRestDB::TxnRestDB() {
    curl_global_init( CURL_GLOBAL_ALL );
}

size_t write_callback( char *ptr, size_t size, size_t nmemb, void *userdata ) {
    //Strictly speaking, they return an array of JsonObjects, which correspond
    //to the rows. I'm not sure how to read this yet, but given that I killed
    //tembo yet again, I'm going to leave it.
    Json::Reader reader;
    Json::Value *val = new Json::Value();
    reader.parse( ptr, ptr + (size * nmemb), *val );
    Json::Value **jsonPtr = (Json::Value **) userdata;
    *jsonPtr = val;
    return 0;
}

std::vector<Json::Value *> *TxnRestDB::sendQuery( int clientId, string query ){
    CURLcode res;
    char url[64];
    ostringstream os;
    curl = curl_easy_init();
    snprintf( url, 64, REST_QUERY_URL, clientId );
    curl_easy_setopt( curl, CURLOPT_URL, url );

    os << "{ \"query\": \"";
    os << query;
    os << "\" }";

    //Have the write callback set this ptr to the allocated data
    std::vector<Json::Value *> *resultArr = NULL;
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, os.str() );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_callback );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *) (&resultArr) );
    res = curl_easy_perform( curl );
    if( res != CURLE_OK ) {
        //ERROR, handle this!
    }
    //Pull out results, return
    curl_easy_cleanup( curl );
    return resultArr;
}

void TxnRestDB::execute( const TPCE::TBrokerVolumeFrame1Input *pIn,
                         TPCE::TBrokerVolumeFrame1Output *pOut ) {

    std::vector<Json::Value *> *jsonArr;

    ostringstream osBrokers;
    ostringstream osSQL;

    //Unrolled Transaction
    osSQL << "SELECT b_name as broker_name, ";
    osSQL << "sum(tr_qty * tr_bid_price) as volume ";
    osSQL << "FROM trade_request, sector, industry, ";
    osSQL << "company, broker, security ";
    osSQL << "WHERE tr_b_id = b_id AND ";
    osSQL << "tr_s_symb = s_symb AND ";
    osSQL << "s_co_id = co_id AND ";
    osSQL << "co_in_id = in_id AND ";
    osSQL << "sc_id = in_sc_id AND ";
    osSQL << "b_name IN ( ";
    int i = 0;
    osSQL << "'" << pIn->broker_list[i] << "'";
    for (i = 1; pIn->broker_list[i][0] != '\0' &&
            i < TPCE::max_broker_list_len; i++) {
        osSQL << ", '" << pIn->broker_list[i] << "'";
    }
    osSQL << " ) AND ";
    osSQL << "sc_name = '" << pIn->sector_name << "' ";
    osSQL << "GROUP BY b_name ORDER BY 2 DESC";

    jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->list_len = jsonArr->at(0)->get("list_len", "").asInt();

    vector<string> vAux;
    vector<string>::iterator p;

    TokenizeSmart(jsonArr->at(0)->get("broker_name", "").asString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->broker_name[i], (*p).c_str(), cB_NAME_len);
        pOut->broker_name[i][cB_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->list_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("volume", "").asString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->volume[i] = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->list_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TCustomerPositionFrame1Input *pIn,
                         TCustomerPositionFrame1Output *pOut ) {
    ostringstream osSQL;
    std::vector<Json::Value *> *jsonArr;

    //Unrolled sproc
    //Get the cust_id if we don't have it
    long cust_id = pIn->cust_id;
    if( cust_id == 0 ){
        osSQL << "SELECT cust_id FROM customer WHERE ";
        osSQL << "c_tax_id = " << pIn->tax_id;
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        cust_id = jsonArr->at(0)->get( "cust_id", "" ).asInt64();
        pOut->cust_id = jsonArr->at(0)->get( "cust_id", "" ).asInt64();
        //TODO: free jsonArr
        //Reset stringstream
        osSQL = std::ostringstream();
    }

    //Retrieve Customer Fields
    osSQL << "SELECT c_st_id, c_l_name, c_f_name, c_m_name, ";
    osSQL << "c_gndr, c_tier, c_dob, c_ad_id, c_ctry_1, ";
    osSQL << "c_area_1, c_local_1, c_ext_1, c_ctry_2, ";
    osSQL << "c_area_2, c_local_2, c_ext_2, c_ctry_3, ";
    osSQL << "c_area_3, c_local_3, c_ext_3, c_email_1, c_email_2 ";
    osSQL << "FROM customer where c_id = " << cust_id;
    jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->acct_len = jsonArr->at(0)->get("acct_len", "").asInt();
    pOut->cust_id = jsonArr->at(0)->get("cust_id", "").asInt64();

    pOut->c_ad_id = jsonArr->at(0)->get("c_ad_id","").asInt64();

    strncpy(pOut->c_area_1, jsonArr->at(0)->get("c_area_1", "").asCString(), cAREA_len);
    pOut->c_area_1[cAREA_len] = '\0';
    strncpy(pOut->c_area_2, jsonArr->at(0)->get("c_area_2", "").asCString(), cAREA_len);
    pOut->c_area_2[cAREA_len] = '\0';
    strncpy(pOut->c_area_3, jsonArr->at(0)->get("c_area_3", "").asCString(), cAREA_len);
    pOut->c_area_3[cAREA_len] = '\0';

    strncpy(pOut->c_ctry_1, jsonArr->at(0)->get("c_ctry_1", "").asCString(), cCTRY_len);
    pOut->c_ctry_1[cCTRY_len] = '\0';
    strncpy(pOut->c_ctry_2, jsonArr->at(0)->get("c_ctry_2", "").asCString(), cCTRY_len);
    pOut->c_ctry_2[cCTRY_len] = '\0';
    strncpy(pOut->c_ctry_3, jsonArr->at(0)->get("c_ctry_3", "").asCString(), cCTRY_len);
    pOut->c_ctry_3[cCTRY_len] = '\0';

    sscanf(jsonArr->at(0)->get("c_dob", "").asCString(), "%hd-%hd-%hd", &pOut->c_dob.year,
            &pOut->c_dob.month, &pOut->c_dob.day);

    strncpy(pOut->c_email_1, jsonArr->at(0)->get("c_email_1", "").asCString(), cEMAIL_len);
    pOut->c_email_1[cEMAIL_len] = '\0';
    strncpy(pOut->c_email_2, jsonArr->at(0)->get("c_email_2", "").asCString(), cEMAIL_len);
    pOut->c_email_2[cEMAIL_len] = '\0';

    strncpy(pOut->c_ext_1, jsonArr->at(0)->get("c_ext_1", "").asCString(), cEXT_len);
    pOut->c_ext_1[cEXT_len] = '\0';
    strncpy(pOut->c_ext_2, jsonArr->at(0)->get("c_ext_2", "").asCString(), cEXT_len);
    pOut->c_ext_2[cEXT_len] = '\0';
    strncpy(pOut->c_ext_3, jsonArr->at(0)->get("c_ext_3", "").asCString(), cEXT_len);
    pOut->c_ext_3[cEXT_len] = '\0';

    strncpy(pOut->c_f_name, jsonArr->at(0)->get("c_f_name", "").asCString(), cF_NAME_len);
    pOut->c_f_name[cF_NAME_len] = '\0';
    strncpy(pOut->c_gndr, jsonArr->at(0)->get("c_gndr", "").asCString(), cGNDR_len);
    pOut->c_gndr[cGNDR_len] = '\0';
    strncpy(pOut->c_l_name, jsonArr->at(0)->get("c_l_name", "").asCString(), cL_NAME_len);
    pOut->c_l_name[cL_NAME_len] = '\0';

    strncpy(pOut->c_local_1, jsonArr->at(0)->get("c_local_1", "").asCString(), cLOCAL_len);
    pOut->c_local_1[cLOCAL_len] = '\0';
    strncpy(pOut->c_local_2, jsonArr->at(0)->get("c_local_2", "").asCString(), cLOCAL_len);
    pOut->c_local_2[cLOCAL_len] = '\0';
    strncpy(pOut->c_local_3, jsonArr->at(0)->get("c_local_3", "").asCString(), cLOCAL_len);
    pOut->c_local_3[cLOCAL_len] = '\0';

    strncpy(pOut->c_m_name, jsonArr->at(0)->get("c_m_name", "").asCString(), cM_NAME_len);
    pOut->c_m_name[cM_NAME_len] = '\0';
    strncpy(pOut->c_st_id, jsonArr->at(0)->get("c_st_id", "").asCString(), cST_ID_len);
    pOut->c_st_id[cST_ID_len] = '\0';
    strncpy(&pOut->c_tier, jsonArr->at(0)->get("c_tier", "").asCString(), 1);

    //TODO: free jsonArr
    //Reset stringstream
    osSQL = std::ostringstream();

    //Now retrieve account information
    osSQL << "SELECT ca_id as acct_id, ca_bal as cash_bal, ";
    osSQL << "IFNULL(sum(hs_qty * lt_price), 0) as assets_total ";
    osSQL << "FROM customer_account LEFT OUTER JOIN ";
    osSQL << "holding_summary ON hs_ca_id = ca_id, ";
    osSQL << "last_trade WHERE ca_c_id = " << cust_id;
    osSQL << " AND lt_s_symb = hs_s_symb GROUP BY ";
    osSQL << "ca_id, ca_bal ORDER BY 3 ASC LIMIT 10";

    vector<string> vAux;
    vector<string>::iterator p;

    TokenizeSmart(jsonArr->at(0)->get("acct_id", "").asString(), vAux);

    int i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->acct_id[i] = atol( (*p).c_str() );
        ++i;
    }
    check_count(pOut->acct_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("asset_total", "").asString(), vAux);

    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->asset_total[i] = atof( (*p).c_str() );
        ++i;
    }
    check_count(pOut->acct_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();


    TokenizeSmart(jsonArr->at(0)->get("cash_bal", "").asString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->cash_bal[i] = atof( (*p).c_str() );
        ++i;
    }
    check_count(pOut->acct_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TCustomerPositionFrame2Input *pIn,
                         TCustomerPositionFrame2Output *pOut )
{
    //TODO: unfold this sproc
    ostringstream osSQL;
    osSQL << "SELECT * FROM CustomerPositionFrame2(" << pIn->acct_id << ")";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->hist_len = jsonArr->at(0)->get("hist_len","").asInt();

    vector<string> vAux;
    vector<string>::iterator p;
    int i;

    TokenizeSmart(jsonArr->at(0)->get("hist_dts", "").asString(), vAux);

    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->hist_dts[i].year,
                &pOut->hist_dts[i].month,
                &pOut->hist_dts[i].day,
                &pOut->hist_dts[i].hour,
                &pOut->hist_dts[i].minute,
                &pOut->hist_dts[i].second);
        ++i;
    }
    check_count(pOut->hist_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("qty", "").asString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->qty[i] = atoi((*p).c_str());
        ++i;
    }
    check_count(pOut->hist_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("symbol", "").asString(), vAux);

    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->symbol[i], (*p).c_str(), cSYMBOL_len);
        pOut->symbol[i][cSYMBOL_len] = '\0';
        ++i;
    }
    check_count(pOut->hist_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_id", "").asString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_id[i] = atol((*p).c_str());
        ++i;
    }
    check_count(pOut->hist_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_status", "").asString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_status[i], (*p).c_str(), cST_NAME_len);
        pOut->trade_status[i][cST_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->hist_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TDataMaintenanceFrame1Input *pIn ) {
    ostringstream osSQL;
    //TODO: unroll this sproc
    osSQL << "SELECT * FROM DataMaintenanceFrame1(" <<
        pIn->acct_id << ", " <<
        pIn->c_id << ", " <<
        pIn->co_id << ", " <<
        pIn->day_of_month << ", '" <<
        pIn->symbol << "', '" <<
        pIn->table_name << "', '" <<
        pIn->tx_id << "', " <<
        pIn->vol_incr << ")";

    //Free this
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
}

void TxnRestDB::execute( const TMarketFeedFrame1Input *pIn,
                         TMarketFeedFrame1Output *pOut,
                         CSendToMarketInterface *pMarketExchange ) {

    ostringstream osSymbol, osPrice, osQty;

    for (unsigned int i = 0;
            i < (sizeof(pIn->Entries) / sizeof(pIn->Entries[0])); ++i) {
        if (i == 0) {
            osSymbol << "\"" << pIn->Entries[i].symbol;
            osPrice << pIn->Entries[i].price_quote;
            osQty << pIn->Entries[i].trade_qty;
        } else {
            osSymbol << "\",\"" << pIn->Entries[i].symbol;
            osPrice << "," << pIn->Entries[i].price_quote;
            osQty << "," << pIn->Entries[i].trade_qty;
        }
    }
    osSymbol << "\"";

    ostringstream osSQL;
    osSQL << "SELECT * FROM MarketFeedFrame1('{" <<
        osPrice.str() << "}','" <<
        pIn->StatusAndTradeType.status_submitted << "','{" <<
        osSymbol.str() << "}', '{" <<
        osQty.str() << "}','" <<
        pIn->StatusAndTradeType.type_limit_buy << "','" <<
        pIn->StatusAndTradeType.type_limit_sell << "','" <<
        pIn->StatusAndTradeType.type_stop_loss << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->num_updated = jsonArr->at(0)->get("num_updated", "").asInt();
    pOut->send_len = jsonArr->at(0)->get("send_len", "").asInt();

    vector<string> v1;
    vector<string>::iterator p1;
    vector<string> v2;
    vector<string>::iterator p2;
    vector<string> v3;
    vector<string>::iterator p3;
    vector<string> v4;
    vector<string>::iterator p4;
    vector<string> v5;
    vector<string>::iterator p5;

    TokenizeSmart(jsonArr->at(0)->get("symbol", "").asString(), v1);
    TokenizeSmart(jsonArr->at(0)->get("trade_id", "").asString(), v2);
    TokenizeSmart(jsonArr->at(0)->get("price_quote", "").asString(), v3);
    TokenizeSmart(jsonArr->at(0)->get("trade_qty", "").asString(), v4);
    TokenizeSmart(jsonArr->at(0)->get("trade_type", "").asString(), v5);

    // FIXME: Consider altering to match spec.  Since PostgreSQL cannot
    // control transaction from within a stored function and because we're
    // making the call to the Market Exchange Emulator from outside
    // the transaction, consider altering the code to call a stored
    // function once per symbol to match the transaction rules in
    // the spec.
    int i = 0;
    bool bSent;
    for (p1 = v1.begin(), p2 = v2.begin(), p3 = v3.begin(), p4 = v4.begin(),
            p5 = v5.begin(); p1 != v1.end(); ++p1, ++p2, ++p3, ++p4) {
        strncpy(m_TriggeredLimitOrders.symbol, (*p1).c_str(), cSYMBOL_len);
        m_TriggeredLimitOrders.symbol[cSYMBOL_len] = '\0';
        m_TriggeredLimitOrders.trade_id = atol((*p2).c_str());
        m_TriggeredLimitOrders.price_quote = atof((*p3).c_str());
        m_TriggeredLimitOrders.trade_qty = atoi((*p4).c_str());
        strncpy(m_TriggeredLimitOrders.trade_type_id, (*p5).c_str(),
                cTT_ID_len);
        m_TriggeredLimitOrders.trade_type_id[cTT_ID_len] = '\0';

        bSent = pMarketExchange->SendToMarketFromFrame(
                m_TriggeredLimitOrders);
        ++i;
    }
    check_count(pOut->send_len, i, __FILE__, __LINE__);
}

void TxnRestDB::execute( const TMarketWatchFrame1Input *pIn,
                         TMarketWatchFrame1Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM MarketWatchFrame1(" <<
        pIn->acct_id << "," <<
        pIn->c_id << "," <<
        pIn->ending_co_id << ",'" <<
        pIn->industry_name << "','" <<
        pIn->start_day.year << "-" <<
        pIn->start_day.month << "-" <<
        pIn->start_day.day << "'," <<
        pIn->starting_co_id << ")";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    //TODO: Correct column name?
    pOut->pct_change = jsonArr->at(0)->get("wi_s_symb","").asFloat();

}

void TxnRestDB::execute( const TSecurityDetailFrame1Input *pIn,
                         TSecurityDetailFrame1Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM SecurityDetailFrame1(" <<
        (pIn->access_lob_flag == 0 ? "false" : "true") << "," <<
        pIn->max_rows_to_return << ",'" <<
        pIn->start_day.year << "-" <<
        pIn->start_day.month << "-" <<
        pIn->start_day.day << "','" <<
        pIn->symbol << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->fin_len = jsonArr->at(0)->get("fin_len", "").asInt();
    pOut->day_len = jsonArr->at(0)->get("day_len", "").asInt();
    pOut->news_len = jsonArr->at(0)->get("news_len", "").asInt();

    pOut->s52_wk_high = jsonArr->at(0)->get("s52_wk_high", "").asFloat();
    sscanf(jsonArr->at(0)->get("s52_wk_high_date","").asCString(), "%hd-%hd-%hd",
            &pOut->s52_wk_high_date.year,
            &pOut->s52_wk_high_date.month,
            &pOut->s52_wk_high_date.day);
    pOut->s52_wk_low = jsonArr->at(0)->get("s52_wk_low", "").asFloat();
    sscanf(jsonArr->at(0)->get("s52_wk_low_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->s52_wk_low_date.year,
            &pOut->s52_wk_low_date.month,
            &pOut->s52_wk_low_date.day);

    strncpy(pOut->ceo_name, jsonArr->at(0)->get("ceo_name", "").asCString(), cCEO_NAME_len);
    pOut->ceo_name[cCEO_NAME_len] = '\0';
    strncpy(pOut->co_ad_cty, jsonArr->at(0)->get("co_ad_cty", "").asCString(), cAD_CTRY_len);
    pOut->co_ad_cty[cAD_CTRY_len] = '\0';
    strncpy(pOut->co_ad_div, jsonArr->at(0)->get("co_ad_div", "").asCString(), cAD_DIV_len);
    pOut->co_ad_div[cAD_DIV_len] = '\0';
    strncpy(pOut->co_ad_line1, jsonArr->at(0)->get("co_ad_line1", "").asCString(), cAD_LINE_len);
    pOut->co_ad_line1[cAD_LINE_len] = '\0';
    strncpy(pOut->co_ad_line2, jsonArr->at(0)->get("co_ad_line2", "").asCString(), cAD_LINE_len);
    pOut->co_ad_line2[cAD_LINE_len] = '\0';
    strncpy(pOut->co_ad_town, jsonArr->at(0)->get("co_ad_town", "").asCString(), cAD_TOWN_len);
    pOut->co_ad_town[cAD_TOWN_len] = '\0';
    strncpy(pOut->co_ad_zip, jsonArr->at(0)->get("co_ad_zip", "").asCString(), cAD_ZIP_len);
    pOut->co_ad_zip[cAD_ZIP_len] = '\0';
    strncpy(pOut->co_desc, jsonArr->at(0)->get("co_desc", "").asCString(), cCO_DESC_len);
    pOut->co_desc[cCO_DESC_len] = '\0';
    strncpy(pOut->co_name, jsonArr->at(0)->get("co_name", "").asCString(), cCO_NAME_len);
    pOut->co_name[cCO_NAME_len] = '\0';
    strncpy(pOut->co_st_id, jsonArr->at(0)->get("co_st_id", "").asCString(), cST_ID_len);
    pOut->co_st_id[cST_ID_len] = '\0';

    vector<string> vAux;
    vector<string>::iterator p;
    TokenizeSmart(jsonArr->at(0)->get("cp_co_name", "").asString(), vAux);
    int i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->cp_co_name[i], (*p).c_str(), cCO_NAME_len);
        pOut->cp_co_name[i][cCO_NAME_len] = '\0';
        ++i;
    }
    // FIXME: The stored functions for PostgreSQL are designed to return 3
    // items in the array, even though it's not required.
    check_count(3, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cp_in_name", "").asString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->cp_in_name[i], (*p).c_str(), cIN_NAME_len);
        pOut->cp_in_name[i][cIN_NAME_len] = '\0';
        ++i;
    }

    // FIXME: The stored functions for PostgreSQL are designed to return 3
    // items in the array, even though it's not required.
    check_count(3, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeArray(jsonArr->at(0)->get("i_day", "").asString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;

        TokenizeSmart((*p).c_str(), v2);

        p2 = v2.begin();
        sscanf((*p2++).c_str(), "%hd-%hd-%hd",
                &pOut->day[i].date.year,
                &pOut->day[i].date.month,
                &pOut->day[i].date.day);
        pOut->day[i].close = atof((*p2++).c_str());
        pOut->day[i].high = atof((*p2++).c_str());
        pOut->day[i].low = atof((*p2++).c_str());
        pOut->day[i].vol = atoi((*p2++).c_str());
        ++i;
        v2.clear();
    }
    check_count(pOut->day_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    pOut->divid = jsonArr->at(0)->get("divid","").asFloat();

    strncpy(pOut->ex_ad_cty, jsonArr->at(0)->get("ex_ad_cty","").asCString(), cAD_CTRY_len);
    pOut->ex_ad_cty[cAD_CTRY_len] = '\0';
    strncpy(pOut->ex_ad_div, jsonArr->at(0)->get("ex_ad_div", "").asCString(), cAD_DIV_len);
    pOut->ex_ad_div[cAD_DIV_len] = '\0';
    strncpy(pOut->ex_ad_line1, jsonArr->at(0)->get("ex_ad_line1", "").asCString(), cAD_LINE_len);
    pOut->ex_ad_line1[cAD_LINE_len] = '\0';
    strncpy(pOut->ex_ad_line2, jsonArr->at(0)->get("ex_ad_line2", "").asCString(), cAD_LINE_len);
    pOut->ex_ad_line2[cAD_LINE_len] = '\0';
    strncpy(pOut->ex_ad_town, jsonArr->at(0)->get("ex_ad_town", "").asCString(), cAD_TOWN_len);
    pOut->ex_ad_town[cAD_TOWN_len]  = '\0';
    strncpy(pOut->ex_ad_zip, jsonArr->at(0)->get("ex_ad_zip", "").asCString(), cAD_ZIP_len);
    pOut->ex_ad_zip[cAD_ZIP_len] = '\0';
    pOut->ex_close = jsonArr->at(0)->get("ex_close", "").asInt();
    sscanf(jsonArr->at(0)->get("ex_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->ex_date.year,
            &pOut->ex_date.month,
            &pOut->ex_date.day);
    strncpy(pOut->ex_desc, jsonArr->at(0)->get("ex_desc", "").asCString(), cEX_DESC_len);
    pOut->ex_desc[cEX_DESC_len] = '\0';
    strncpy(pOut->ex_name, jsonArr->at(0)->get("ex_name", "").asCString(), cEX_NAME_len);
    pOut->ex_name[cEX_NAME_len] = '\0';
    pOut->ex_num_symb = jsonArr->at(0)->get("ex_num_symb", "").asInt();
    pOut->ex_open = jsonArr->at(0)->get("ex_open", "").asInt();

    TokenizeArray(jsonArr->at(0)->get("fin", "").asString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;

        TokenizeSmart((*p).c_str(), v2);

        p2 = v2.begin();
        pOut->fin[i].year = atoi((*p2++).c_str());
        pOut->fin[i].qtr = atoi((*p2++).c_str());
        sscanf((*p2++).c_str(), "%hd-%hd-%hd",
                &pOut->fin[i].start_date.year,
                &pOut->fin[i].start_date.month,
                &pOut->fin[i].start_date.day);
        pOut->fin[i].rev = atof((*p2++).c_str());
        pOut->fin[i].net_earn = atof((*p2++).c_str());
        pOut->fin[i].basic_eps = atof((*p2++).c_str());
        pOut->fin[i].dilut_eps = atof((*p2++).c_str());
        pOut->fin[i].margin = atof((*p2++).c_str());
        pOut->fin[i].invent = atof((*p2++).c_str());
        pOut->fin[i].assets = atof((*p2++).c_str());
        pOut->fin[i].liab = atof((*p2++).c_str());
        pOut->fin[i].out_basic = atof((*p2++).c_str());
        pOut->fin[i].out_dilut = atof((*p2++).c_str());
        ++i;
        v2.clear();
    }
    check_count(pOut->fin_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    pOut->last_open = jsonArr->at(0)->get("last_open","").asFloat();
    pOut->last_price = jsonArr->at(0)->get("last_price", "").asFloat();
    pOut->last_vol = jsonArr->at(0)->get("last_vol", "").asInt();

    TokenizeArray(jsonArr->at(0)->get("i_news", "").asString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;

        TokenizeSmart((*p).c_str(), v2);

        p2 = v2.begin();
        // FIXME: Postgresql can actually return 5 times the amount of data due
        // to escaped characters.  Cap the data at the length that EGen defines
        // it and hope it isn't a problem for continuing the test correctly.
        strncpy(pOut->news[i].item, (*p2++).c_str(), cNI_ITEM_len);
        pOut->news[i].item[cNI_ITEM_len] = '\0';
        sscanf((*p2++).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->news[i].dts.year,
                &pOut->news[i].dts.month,
                &pOut->news[i].dts.day,
                &pOut->news[i].dts.hour,
                &pOut->news[i].dts.minute,
                &pOut->news[i].dts.second);
        strncpy(pOut->news[i].src, (*p2++).c_str(), cNI_SOURCE_len);
        pOut->news[i].src[cNI_SOURCE_len] = '\0';
        strncpy(pOut->news[i].auth, (*p2++).c_str(), cNI_AUTHOR_len);
        pOut->news[i].auth[cNI_AUTHOR_len] = '\0';
        strncpy(pOut->news[i].headline, (*p2++).c_str(), cNI_HEADLINE_len);
        pOut->news[i].headline[cNI_HEADLINE_len] = '\0';
        strncpy(pOut->news[i].summary, (*p2++).c_str(), cNI_SUMMARY_len);
        pOut->news[i].summary[cNI_SUMMARY_len] = '\0';
        ++i;
        v2.clear();
    }
    check_count(pOut->news_len, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    sscanf(jsonArr->at(0)->get("open_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->open_date.year,
            &pOut->open_date.month,
            &pOut->open_date.day);
    pOut->pe_ratio = jsonArr->at(0)->get("pe_ration", "").asFloat();
    strncpy(pOut->s_name, jsonArr->at(0)->get("s_name", "").asCString(), cS_NAME_len);
    pOut->s_name[cS_NAME_len] = '\0';
    pOut->num_out = jsonArr->at(0)->get("num_out", "").asInt64();
    strncpy(pOut->sp_rate, jsonArr->at(0)->get("sp_rate", "").asCString(), cSP_RATE_len);
    pOut->sp_rate[cSP_RATE_len] = '\0';
    sscanf(jsonArr->at(0)->get("start_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->start_date.year,
            &pOut->start_date.month,
            &pOut->start_date.day);
    pOut->yield = jsonArr->at(0)->get("yield", "").asFloat();
}

void TxnRestDB::execute( const TTradeCleanupFrame1Input *pIn ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeCleanupFrame1('" <<
        pIn->st_canceled_id << "','" <<
        pIn->st_pending_id << "','" <<
        pIn->st_submitted_id << "'," <<
        pIn->start_trade_id << ")";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
}

void TxnRestDB::execute( const TTradeLookupFrame1Input *pIn,
                         TTradeLookupFrame1Output *pOut ) {
    ostringstream osTrades;
    int i = 0;
    osTrades << pIn->trade_id[i];
    for ( i = 1; i < pIn->max_trades; i++) {
        osTrades << "," << pIn->trade_id[i];
    }

    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeLookupFrame1(" <<
        pIn->max_trades << ",'{" <<
        osTrades.str() << "}')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->at(0)->get("num_found", "").asInt();

    vector<string> vAux;
    vector<string>::iterator p;

    TokenizeSmart(jsonArr->at(0)->get("bid_price", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].bid_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_amount", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].cash_transaction_amount = atof((*p).c_str());
        ++i;
    }
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_dts", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second);
        ++i;
    }
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_name", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].cash_transaction_name, (*p).c_str(),
                cCT_NAME_len);
        pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
        ++i;
    }
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("exec_name", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].exec_name, (*p).c_str(), cEXEC_NAME_len);
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("is_cash", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].is_cash = atoi((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("is_market","").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].is_market = atoi((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_amount", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].settlement_amount = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_cash_due_date","").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        ++i;
    }
    if (!check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__)) {
        cout << "*** settlement_cash_due_date = " <<
            jsonArr->at(0)->get("settlement_cash_due_date", "").asString() << endl;
    }
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_cash_type","").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].settlement_cash_type, (*p).c_str(),
                cSE_CASH_TYPE_len);
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeArray(jsonArr->at(0)->get("trade_history_dts","").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        int j = 0;
        for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
            sscanf((*p2).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].trade_history_dts[j].year,
                    &pOut->trade_info[i].trade_history_dts[j].month,
                    &pOut->trade_info[i].trade_history_dts[j].day,
                    &pOut->trade_info[i].trade_history_dts[j].hour,
                    &pOut->trade_info[i].trade_history_dts[j].minute,
                    &pOut->trade_info[i].trade_history_dts[j].second);
            ++j;
        }
        ++i;
    }
    vAux.clear();

    TokenizeArray(jsonArr->at(0)->get("trade_history_status_id", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        int j = 0;
        for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
            strncpy(pOut->trade_info[i].trade_history_status_id[j],
                    (*p2).c_str(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] =
                '\0';
            ++j;
        }
        ++i;
    }
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_price","").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].trade_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TTradeLookupFrame2Input *pIn,
                         TTradeLookupFrame2Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeLookupFrame2(" <<
        pIn->acct_id << ",'" <<
        pIn->end_trade_dts.year << "-" <<
        pIn->end_trade_dts.month << "-" <<
        pIn->end_trade_dts.day << " " <<
        pIn->end_trade_dts.hour << ":" <<
        pIn->end_trade_dts.minute << ":" <<
        pIn->end_trade_dts.second << "'," <<
        pIn->max_trades << ",'" <<
        pIn->start_trade_dts.year << "-" <<
        pIn->start_trade_dts.month << "-" <<
        pIn->start_trade_dts.day << " " <<
        pIn->start_trade_dts.hour << ":" <<
        pIn->start_trade_dts.minute << ":" <<
        pIn->start_trade_dts.second << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->at(0)->get("num_found","").asInt();

    vector<string> vAux;
    vector<string>::iterator p;

    TokenizeSmart(jsonArr->at(0)->get("bid_price", "").asCString(), vAux);
    int i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].bid_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_amount", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].cash_transaction_amount = atof((*p).c_str());
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_dts", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second);
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_name", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].cash_transaction_name, (*p).c_str(),
                cCT_NAME_len);
        pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("exec_name", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].exec_name, (*p).c_str(), cEXEC_NAME_len);
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("is_cash", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].is_cash = atoi((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_amount", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].settlement_amount = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_cash_due_date", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_cash_type", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].settlement_cash_type, (*p).c_str(),
                cSE_CASH_TYPE_len);
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeArray(jsonArr->at(0)->get("trade_history_dts", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        int j = 0;
        for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
            sscanf((*p2).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].trade_history_dts[j].year,
                    &pOut->trade_info[i].trade_history_dts[j].month,
                    &pOut->trade_info[i].trade_history_dts[j].day,
                    &pOut->trade_info[i].trade_history_dts[j].hour,
                    &pOut->trade_info[i].trade_history_dts[j].minute,
                    &pOut->trade_info[i].trade_history_dts[j].second);
            ++j;
        }
        ++i;
    }
    vAux.clear();

    TokenizeArray(jsonArr->at(0)->get("trade_history_status_id", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        int j = 0;
        for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
            strncpy(pOut->trade_info[i].trade_history_status_id[j],
                    (*p2).c_str(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] =
                '\0';
            ++j;
        }
        ++i;
    }
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_list", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].trade_id = atol((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_price", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].trade_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TTradeLookupFrame3Input *pIn,
                         TTradeLookupFrame3Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeLookupFrame3('" <<
        pIn->end_trade_dts.year << "-" <<
        pIn->end_trade_dts.month << "-" <<
        pIn->end_trade_dts.day << " " <<
        pIn->end_trade_dts.hour << ":" <<
        pIn->end_trade_dts.minute << ":" <<
        pIn->end_trade_dts.second << "'," <<
        pIn->max_acct_id << "," <<
        pIn->max_trades << ",'" <<
        pIn->start_trade_dts.year << "-" <<
        pIn->start_trade_dts.month << "-" <<
        pIn->start_trade_dts.day << " " <<
        pIn->start_trade_dts.hour << ":" <<
        pIn->start_trade_dts.minute << ":" <<
        pIn->start_trade_dts.second << "','" <<
        pIn->symbol << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->at(0)->get("num_found", "").asInt();

    vector<string> vAux;
    vector<string>::iterator p;

    TokenizeSmart(jsonArr->at(0)->get("acct_id", "").asCString(), vAux);
    int i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].acct_id = atol((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_amount", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].cash_transaction_amount = atof((*p).c_str());
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_dts", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second);
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("cash_transaction_name", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].cash_transaction_name, (*p).c_str(),
                cCT_NAME_len);
        pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("exec_name", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].exec_name, (*p).c_str(),
                cEXEC_NAME_len);
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("is_cash", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].is_cash = atoi((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("price", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("quantity", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].quantity = atoi((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_amount", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].settlement_amount = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_cash_due_date", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("settlement_cash_type", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].settlement_cash_type, (*p).c_str(),
                cSE_CASH_TYPE_len);
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_dts", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_dts.year,
                &pOut->trade_info[i].trade_dts.month,
                &pOut->trade_info[i].trade_dts.day,
                &pOut->trade_info[i].trade_dts.hour,
                &pOut->trade_info[i].trade_dts.minute,
                &pOut->trade_info[i].trade_dts.second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeArray(jsonArr->at(0)->get("trade_history_dts", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        int j = 0;
        for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
            sscanf((*p2).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].trade_history_dts[j].year,
                    &pOut->trade_info[i].trade_history_dts[j].month,
                    &pOut->trade_info[i].trade_history_dts[j].day,
                    &pOut->trade_info[i].trade_history_dts[j].hour,
                    &pOut->trade_info[i].trade_history_dts[j].minute,
                    &pOut->trade_info[i].trade_history_dts[j].second);
            ++j;
        }
        ++i;
    }
    vAux.clear();

    TokenizeArray(jsonArr->at(0)->get("trade_history_status_id", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        int j = 0;
        for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
            strncpy(pOut->trade_info[i].trade_history_status_id[j],
                    (*p2).c_str(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] =
                '\0';
            ++j;
        }
        ++i;
    }
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_list", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].trade_id = atol((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart(jsonArr->at(0)->get("trade_type", "").asCString(), vAux);
    i = 0;
    for (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].trade_type, (*p).c_str(), cTT_ID_len);
        pOut->trade_info[i].trade_type[cTT_ID_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TTradeLookupFrame4Input *pIn,
                         TTradeLookupFrame4Output *pOut ) {

    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeLookupFrame4(" <<
        pIn->acct_id << ",'" <<
        pIn->trade_dts.year << "-" <<
        pIn->trade_dts.month << "-" <<
        pIn->trade_dts.day << " " <<
        pIn->trade_dts.hour << ":" <<
        pIn->trade_dts.minute << ":" <<
        pIn->trade_dts.second << "')";

        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

        pOut->num_found = jsonArr->at(0)->get("num_found", "").asInt();
        pOut->num_trades_found = jsonArr->at(0)->get("num_trades_found", "").asInt();

        vector<string> vAux;
        vector<string>::iterator p;

        TokenizeSmart(jsonArr->at(0)->get("holding_history_id", "").asCString(), vAux);
        int i = 0;
        for (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].holding_history_id = atol((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart(jsonArr->at(0)->get("holding_history_trade_id", "").asCString(), vAux);
        i = 0;
        for (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].holding_history_trade_id = atol((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart(jsonArr->at(0)->get("quantity_after", "").asCString(), vAux);
        i = 0;
        for (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].quantity_after = atol((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart(jsonArr->at(0)->get("quantity_before", "").asCString(), vAux);
        i = 0;
        for (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].quantity_before = atol((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        pOut->trade_id = jsonArr->at(0)->get("trade_id", "").asInt64();
}

void TxnRestDB::execute( const TTradeOrderFrame1Input *pIn,
                         TTradeOrderFrame1Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeOrderFrame1(" << pIn->acct_id << ")";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    strncpy(pOut->acct_name, jsonArr->at(0)->get("acct_name", "").asCString(), cCA_NAME_len);
    pOut->acct_name[cCA_NAME_len] ='\0';
    pOut->broker_id = jsonArr->at(0)->get("broker_id", "").asInt64();
    strncpy(pOut->broker_name, jsonArr->at(0)->get("broker_name", "").asCString(), cB_NAME_len);
    pOut->broker_name[cB_NAME_len]  ='\0';
    strncpy(pOut->cust_f_name, jsonArr->at(0)->get("cust_f_name", "").asCString(), cF_NAME_len);
    pOut->cust_f_name[cF_NAME_len] = '\0';
    pOut->cust_id = jsonArr->at(0)->get("cust_id", "").asInt64();
    strncpy(pOut->cust_l_name, jsonArr->at(0)->get("cust_l_name", "").asCString(), cL_NAME_len);
    pOut->cust_l_name[cL_NAME_len] = '\0';
    pOut->cust_tier = jsonArr->at(0)->get("cust_tier", "").asInt64();
    pOut->num_found = jsonArr->at(0)->get("num_found", "").asInt();
    strncpy(pOut->tax_id, jsonArr->at(0)->get("tax_id", "").asCString(), cTAX_ID_len);
    pOut->tax_id[cTAX_ID_len] = '\0';
    pOut->tax_status = jsonArr->at(0)->get("tax_id", "").asInt();
}

void TxnRestDB::execute( const TTradeOrderFrame2Input *pIn,
                         TTradeOrderFrame2Output *pOut ) {
    ostringstream osSQL;
    char *tmpstr;
    osSQL << "SELECT * FROM TradeOrderFrame2(" <<
        pIn->acct_id << ",";
    //
    tmpstr = escape(pIn->exec_f_name);
    osSQL << tmpstr;
    free( tmpstr );
    osSQL << ",";
    tmpstr = escape(pIn->exec_l_name);
    osSQL << tmpstr;
    free( tmpstr );
    osSQL << ",'" << pIn->exec_tax_id<<"')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    //0,0 should be i_bid_price
    //test for null
    if ( strncmp("null", jsonArr->at(0)->get("ap_acl", "").asCString(), 4 ) != 0 ) {
        strncpy(pOut->ap_acl, jsonArr->at(0)->get("ap_acl", "").asCString(), cACL_len);
        pOut->ap_acl[cACL_len] = '\0';
    } else {
        pOut->ap_acl[0] = '\0';
    }
}

void TxnRestDB::execute( const TTradeOrderFrame3Input *pIn,
                         TTradeOrderFrame3Output *pOut ) {
    ostringstream osSQL;
    char *tmpstr;
    osSQL << "SELECT * FROM TradeOrderFrame3(" <<
        pIn->acct_id << "," <<
        pIn->cust_id << "," <<
        pIn->cust_tier << "::SMALLINT," <<
        pIn->is_lifo << "::SMALLINT,'" <<
        pIn->issue << "','" <<
        pIn->st_pending_id << "','" <<
        pIn->st_submitted_id << "'," <<
        pIn->tax_status << "::SMALLINT," <<
        pIn->trade_qty << ",'" <<
        pIn->trade_type_id << "'," <<
        pIn->type_is_margin << "::SMALLINT,";
    tmpstr = escape(pIn->co_name);
    osSQL << tmpstr;
    free(tmpstr);
    osSQL << "," <<
        pIn->requested_price << ",'" <<
        pIn->symbol << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    strncpy(pOut->co_name, jsonArr->at(0)->get("co_name", "").asCString(), cCO_NAME_len);
    pOut->requested_price = jsonArr->at(0)->get("requested_price", "").asFloat();
    strncpy(pOut->symbol, jsonArr->at(0)->get("symbol", "").asCString(), cSYMBOL_len);
    pOut->symbol[cSYMBOL_len] = '\0';
    pOut->buy_value = jsonArr->at(0)->get("buy_value", "").asFloat();
    pOut->charge_amount = jsonArr->at(0)->get("charge_amount", "").asFloat();
    pOut->comm_rate = jsonArr->at(0)->get("comm_rate", "").asFloat();
    pOut->acct_assets = jsonArr->at(0)->get("cust_assets", "").asFloat();
    pOut->market_price = jsonArr->at(0)->get("market_price", "").asFloat();
    strncpy(pOut->s_name, jsonArr->at(0)->get("s_name", "").asCString(), cS_NAME_len);
    pOut->s_name[cS_NAME_len] = '\0';
    pOut->sell_value = jsonArr->at(0)->get("sell_value", "").asFloat();
    strncpy(pOut->status_id, jsonArr->at(0)->get("status_id", "").asCString(), cTH_ST_ID_len);
    pOut->status_id[cTH_ST_ID_len] = '\0';
    pOut->tax_amount = jsonArr->at(0)->get("tax_amount","").asFloat();
    pOut->type_is_market = (jsonArr->at(0)->get("type_is_market", "").asCString()[0] == 't' ? 1 : 0);
    pOut->type_is_sell = (jsonArr->at(0)->get("type_is_sell", "").asCString()[0] == 't' ? 1 : 0);
}

void TxnRestDB::execute( const TTradeOrderFrame4Input *pIn,
                         TTradeOrderFrame4Output *pOut) {
    ostringstream osSQL;
    char *tmpstr;
    osSQL << "SELECT * FROM TradeOrderFrame4(" <<
        pIn->acct_id << "," <<
        pIn->broker_id << "," <<
        pIn->charge_amount << "," <<
        pIn->comm_amount << ",";
    tmpstr = escape( pIn->exec_name );
    osSQL << tmpstr;
    free( tmpstr );

    osSQL << "," <<
        pIn->is_cash << "::SMALLINT," <<
        pIn->is_lifo << "::SMALLINT," <<
        pIn->requested_price << ",'" <<
        pIn->status_id << "','" <<
        pIn->symbol << "'," <<
        pIn->trade_qty << ",'" <<
        pIn->trade_type_id << "'," <<
        pIn->type_is_market << "::SMALLINT)";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->trade_id = jsonArr->at(0)->get("trade_id", "").asInt64();
}

void TxnRestDB::execute( const TTradeResultFrame1Input *pIn,
                         TTradeResultFrame1Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeResultFrame1(" << pIn->trade_id << ")";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->acct_id = jsonArr->at(0)->get( "acct_id" , "" ).asInt64();
    pOut->charge = jsonArr->at(0)->get( "charge", "" ).asFloat();
    pOut->hs_qty = jsonArr->at(0)->get( "hs_qty", "" ).asInt();
    pOut->is_lifo = jsonArr->at(0)->get( "is_lifo", "" ).asInt();
    pOut->num_found = jsonArr->at(0)->get( "num_found", "").asInt();
    strncpy(pOut->symbol, jsonArr->at(0)->get( "symbol", "").asCString(), cSYMBOL_len);
    pOut->symbol[cSYMBOL_len] = '\0';
    pOut->trade_is_cash = jsonArr->at(0)->get( "trade_is_cash", "").asInt();
    pOut->trade_qty = jsonArr->at(0)->get( "trade_qty", "" ).asInt();
    strncpy(pOut->type_id, jsonArr->at(0)->get( "type_id", "" ).asCString(), cTT_ID_len);
    pOut->type_id[cTT_ID_len] = '\0';
    pOut->type_is_market = jsonArr->at(0)->get( "is_market", "" ).asInt();
    pOut->type_is_sell = jsonArr->at(0)->get("type_is_sell", "").asInt();
    strncpy(pOut->type_name, jsonArr->at(0)->get("type_name", "").asCString(), cTT_NAME_len);
    pOut->type_name[cTT_NAME_len] = '\0';
}

void TxnRestDB::execute( const TTradeResultFrame2Input *pIn,
                             TTradeResultFrame2Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeResultFrame2(" <<
        pIn->acct_id << "," <<
        pIn->hs_qty << "," <<
        pIn->is_lifo << "::SMALLINT,'" <<
        pIn->symbol << "'," <<
        pIn->trade_id << "," <<
        pIn->trade_price << "," <<
        pIn->trade_qty << "," <<
        pIn->type_is_sell << "::SMALLINT)";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->broker_id = jsonArr->at(0)->get( "broker_id", "" ).asInt64();
    pOut->buy_value = jsonArr->at(0)->get( "buy_value", "" ).asFloat();
    pOut->cust_id = jsonArr->at(0)->get( "cust_id", "" ).asInt64();
    pOut->sell_value = jsonArr->at(0)->get( "sell_value", "" ).asFloat();
    pOut->tax_status = jsonArr->at(0)->get( "tax_status", "" ).asInt();
    sscanf( jsonArr->at(0)->get( "trade_dts", "" ).asCString(), "%hd-%hd-%hd %hd:%hd:%hd.%*d",
            &pOut->trade_dts.year,
            &pOut->trade_dts.month,
            &pOut->trade_dts.day,
            &pOut->trade_dts.hour,
            &pOut->trade_dts.minute,
            &pOut->trade_dts.second);
}

void TxnRestDB::execute( const TTradeResultFrame3Input *pIn,
                         TTradeResultFrame3Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeResultFrame3(" <<
        pIn->buy_value << "," <<
        pIn->cust_id << "," <<
        pIn->sell_value << "," <<
        pIn->trade_id << ")";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->tax_amount = jsonArr->at(0)->get( "tax_amount", "" ).asFloat();
}

void TxnRestDB::execute( const TTradeResultFrame4Input *pIn,
                         TTradeResultFrame4Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeResultFrame4(" <<
        pIn->cust_id << ",'" <<
        pIn->symbol << "'," <<
        pIn->trade_qty << ",'" <<
        pIn->type_id << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->comm_rate = jsonArr->at(0)->get( "comm_rate", "" ).asFloat();
    strncpy(pOut->s_name, jsonArr->at(0)->get( "s_name", "").asCString(), cS_NAME_len);
    pOut->s_name[cS_NAME_len] = '\0';
}

void TxnRestDB::execute( const TTradeResultFrame5Input *pIn ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeResultFrame5(" <<
        pIn->broker_id << "," <<
        pIn->comm_amount << ",'" <<
        pIn->st_completed_id << "','" <<
        pIn->trade_dts.year << "-" <<
        pIn->trade_dts.month << "-" <<
        pIn->trade_dts.day << " " <<
        pIn->trade_dts.hour << ":" <<
        pIn->trade_dts.minute << ":" <<
        pIn->trade_dts.second << "'," <<
        pIn->trade_id << "," <<
        pIn->trade_price << ")";

    // For PostgreSQL, see comment in the Concurrency Control chapter, under
    // the Transaction Isolation section for dealing with serialization
    // failures.  These serialization failures can occur with REPEATABLE READS
    // or SERIALIZABLE.
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
}

void TxnRestDB::execute( const TTradeResultFrame6Input *pIn,
                         TTradeResultFrame6Output *pOut ) {
    ostringstream osSQL;
    char *tmpstr;
    osSQL << "SELECT * FROM TradeResultFrame6(" <<
        pIn->acct_id << ",'" <<
        pIn->due_date.year << "-"<<
        pIn->due_date.month << "-" <<
        pIn->due_date.day << " " <<
        pIn->due_date.hour << ":" <<
        pIn->due_date.minute << ":" <<
        pIn->due_date.second << "',";
    tmpstr = escape( pIn->s_name );
    osSQL << tmpstr;
    free( tmpstr );
    osSQL << ", " <<
        pIn->se_amount << ",'" <<
        pIn->trade_dts.year << "-" <<
        pIn->trade_dts.month << "-" <<
        pIn->trade_dts.day << " " <<
        pIn->trade_dts.hour << ":" <<
        pIn->trade_dts.minute << ":" <<
        pIn->trade_dts.second << "'," <<
        pIn->trade_id << "," <<
        pIn->trade_is_cash << "::SMALLINT," <<
        pIn->trade_qty << ",'" <<
        pIn->type_name << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->acct_bal = jsonArr->at(0)->get( "acct_bal", "" ).asFloat();
}

void TxnRestDB::execute( const TTradeStatusFrame1Input *pIn,
                         TTradeStatusFrame1Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeStatusFrame1(" << pIn->acct_id << ")";

        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

        vector<string> vAux;
        vector<string>::iterator p;

        pOut->num_found = jsonArr->at(0)->get( "num_found", "" ).asInt();

        strncpy(pOut->broker_name, jsonArr->at(0)->get( "broker_name", "" ).asCString(), cB_NAME_len);
        pOut->broker_name[cB_NAME_len] = '\0';

        TokenizeSmart( jsonArr->at(0)->get( "charge", "" ).asCString(), vAux);
        int i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->charge[i] = atof((*p).c_str());
            ++i;
        }
        vAux.clear();

        strncpy(pOut->cust_f_name, jsonArr->at(0)->get( "cust_f_name", "" ).asCString(), cF_NAME_len);
        pOut->cust_f_name[cF_NAME_len] = '\0';
        strncpy(pOut->cust_l_name, jsonArr->at(0)->get( "cust_l_name", "" ).asCString(), cL_NAME_len);
        pOut->cust_l_name[cL_NAME_len] = '\0';

        int len = i;

        TokenizeSmart(jsonArr->at(0)->get( "ex_name", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->ex_name[i], (*p).c_str(), cEX_NAME_len);
            pOut->ex_name[i][cEX_NAME_len] = '\0';
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart(jsonArr->at(0)->get( "exec_name", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->exec_name[i], (*p).c_str(), cEXEC_NAME_len);
            pOut->exec_name[i][cEXEC_NAME_len] = '\0';
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "s_name", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->s_name[i], (*p).c_str(), cS_NAME_len);
            pOut->s_name[i][cS_NAME_len] = '\0';
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "status_name", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->status_name[i], (*p).c_str(), cST_NAME_len);
            pOut->status_name[i][cST_NAME_len] = '\0';
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();
        TokenizeSmart( jsonArr->at(0)->get( "symbol", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->symbol[i], (*p).c_str(), cSYMBOL_len);
            pOut->symbol[i][cSYMBOL_len] = '\0';
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "trade_dts", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_dts[i].year,
                    &pOut->trade_dts[i].month,
                    &pOut->trade_dts[i].day,
                    &pOut->trade_dts[i].hour,
                    &pOut->trade_dts[i].minute,
                    &pOut->trade_dts[i].second);
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "trade_id", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_id[i] = atol((*p).c_str());
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "trade_qty", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_qty[i] = atoi((*p).c_str());
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "type_name", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->type_name[i], (*p).c_str(), cTT_NAME_len);
            pOut->type_name[i][cTT_NAME_len] = '\0';
            ++i;
        }
        check_count(len, vAux.size(), __FILE__, __LINE__);
        vAux.clear();
    }

void TxnRestDB::execute( const TTradeUpdateFrame1Input *pIn,
                         TTradeUpdateFrame1Output *pOut ) {

    ostringstream osTrades;
    int i = 0;
    osTrades << pIn->trade_id[i];
    for (i = 1; i < pIn->max_trades; i++) {
        osTrades << "," << pIn->trade_id[i];
    }

    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeUpdateFrame1(" <<
        pIn->max_trades << "," <<
        pIn->max_updates << ",'{" <<
        osTrades.str() << "}')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->at(0)->get( "num_found", "" ).asInt();

    vector<string> vAux;
    vector<string>::iterator p;

    TokenizeSmart( jsonArr->at(0)->get( "bid_price", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].bid_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "cash_transaction_amount", "").asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].cash_transaction_amount = atof((*p).c_str());
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "cash_transaction_dts", "").asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second);
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "cash_transaction_name", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].cash_transaction_name, (*p).c_str(),
                cCT_NAME_len);
        pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
        ++i;
    }
    // FIXME: According to spec, this may not match the returned number found?
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "exec_name", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].exec_name, (*p).c_str(), cEXEC_NAME_len);
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
    TokenizeSmart( jsonArr->at(0)->get( "is_cash", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].is_cash = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "is_market", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].is_market = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "settlement_amount", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].settlement_amount = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "settlement_cash_due_date", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "settlement_cash_type", "" ).asCString(), vAux );
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].settlement_cash_type, (*p).c_str(),
                cSE_CASH_TYPE_len);
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    pOut->num_updated = jsonArr->at(0)->get( "num_updated", "" ).asInt();

    TokenizeArray( jsonArr->at(0)->get( "trade_history_dts", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        p2 = v2.begin();
        sscanf((*p2++).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[0].year,
                &pOut->trade_info[i].trade_history_dts[0].month,
                &pOut->trade_info[i].trade_history_dts[0].day,
                &pOut->trade_info[i].trade_history_dts[0].hour,
                &pOut->trade_info[i].trade_history_dts[0].minute,
                &pOut->trade_info[i].trade_history_dts[0].second);
        sscanf((*p2++).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[1].year,
                &pOut->trade_info[i].trade_history_dts[1].month,
                &pOut->trade_info[i].trade_history_dts[1].day,
                &pOut->trade_info[i].trade_history_dts[1].hour,
                &pOut->trade_info[i].trade_history_dts[1].minute,
                &pOut->trade_info[i].trade_history_dts[1].second);
        sscanf((*p2).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[2].year,
                &pOut->trade_info[i].trade_history_dts[2].month,
                &pOut->trade_info[i].trade_history_dts[2].day,
                &pOut->trade_info[i].trade_history_dts[2].hour,
                &pOut->trade_info[i].trade_history_dts[2].minute,
                &pOut->trade_info[i].trade_history_dts[2].second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeArray( jsonArr->at(0)->get( "trade_history_status_id", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        p2 = v2.begin();
        strncpy(pOut->trade_info[i].trade_history_status_id[0],
                (*p2++).c_str(), cTH_ST_ID_len);
        pOut->trade_info[i].trade_history_status_id[0][cTH_ST_ID_len] = '\0';
        strncpy(pOut->trade_info[i].trade_history_status_id[1],
                (*p2++).c_str(), cTH_ST_ID_len);
        pOut->trade_info[i].trade_history_status_id[1][cTH_ST_ID_len] = '\0';
        strncpy(pOut->trade_info[i].trade_history_status_id[3],
                (*p2).c_str(), cTH_ST_ID_len);
        pOut->trade_info[i].trade_history_status_id[3][cTH_ST_ID_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "trade_price", "").asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].trade_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TTradeUpdateFrame2Input *pIn,
                         TTradeUpdateFrame2Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeUpdateFrame2(" <<
        pIn->acct_id << ",'" <<
        pIn->end_trade_dts.year << "-" <<
        pIn->end_trade_dts.month << "-" <<
        pIn->end_trade_dts.day << " " <<
        pIn->end_trade_dts.hour << ":" <<
        pIn->end_trade_dts.minute << ":" <<
        pIn->end_trade_dts.second << "'," <<
        pIn->max_trades << "," <<
        pIn->max_updates << ",'" <<
        pIn->start_trade_dts.year << "-" <<
        pIn->start_trade_dts.month << "-" <<
        pIn->start_trade_dts.day << " " <<
        pIn->start_trade_dts.hour << ":" <<
        pIn->start_trade_dts.minute << ":" <<
        pIn->start_trade_dts.second << "')";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->at(0)->get( "num_found", "" ).asInt();

    vector<string> vAux;
    vector<string>::iterator p;

    TokenizeSmart( jsonArr->at(0)->get( "bid_price", "" ).asCString(), vAux);
    int i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].bid_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get("cash_transaction_amount", "").asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].cash_transaction_amount = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get("cash_transaction_dts", "").asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "cash_transaction_name", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].cash_transaction_name, (*p).c_str(),
                cCT_NAME_len);
        pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "exec_name", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].exec_name, (*p).c_str(), cEXEC_NAME_len);
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "is_cash", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].is_cash = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    pOut->num_updated = jsonArr->at(0)->get( "num_updated", "" ).asInt();

    TokenizeSmart( jsonArr->at(0)->get( "settlement_amount", "").asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].settlement_amount = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "settlement_cash_due_date", "").asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "settlement_cash_type", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        strncpy(pOut->trade_info[i].settlement_cash_type, (*p).c_str(),
                cSE_CASH_TYPE_len);
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeArray( jsonArr->at(0)->get( "trade_history_dts", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        p2 = v2.begin();
        sscanf((*p2++).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[0].year,
                &pOut->trade_info[i].trade_history_dts[0].month,
                &pOut->trade_info[i].trade_history_dts[0].day,
                &pOut->trade_info[i].trade_history_dts[0].hour,
                &pOut->trade_info[i].trade_history_dts[0].minute,
                &pOut->trade_info[i].trade_history_dts[0].second);
        sscanf((*p2++).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[1].year,
                &pOut->trade_info[i].trade_history_dts[1].month,
                &pOut->trade_info[i].trade_history_dts[1].day,
                &pOut->trade_info[i].trade_history_dts[1].hour,
                &pOut->trade_info[i].trade_history_dts[1].minute,
                &pOut->trade_info[i].trade_history_dts[1].second);
        sscanf((*p2).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[2].year,
                &pOut->trade_info[i].trade_history_dts[2].month,
                &pOut->trade_info[i].trade_history_dts[2].day,
                &pOut->trade_info[i].trade_history_dts[2].hour,
                &pOut->trade_info[i].trade_history_dts[2].minute,
                &pOut->trade_info[i].trade_history_dts[2].second);
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeArray( jsonArr->at(0)->get( "trade_history_status_id", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        vector<string> v2;
        vector<string>::iterator p2;
        TokenizeSmart((*p).c_str(), v2);
        p2 = v2.begin();
        strncpy(pOut->trade_info[i].trade_history_status_id[0],
                (*p2++).c_str(), cTH_ST_ID_len);
        pOut->trade_info[i].trade_history_status_id[0][cTH_ST_ID_len] = '\0';
        strncpy(pOut->trade_info[i].trade_history_status_id[1],
                (*p2++).c_str(), cTH_ST_ID_len);
        pOut->trade_info[i].trade_history_status_id[1][cTH_ST_ID_len] = '\0';
        strncpy(pOut->trade_info[i].trade_history_status_id[3],
                (*p2).c_str(), cTH_ST_ID_len);
        pOut->trade_info[i].trade_history_status_id[3][cTH_ST_ID_len] = '\0';
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "trade_list", "" ).asCString(), vAux );
    this->bh = bh;
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].trade_id = atol((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();

    TokenizeSmart( jsonArr->at(0)->get( "trade_price", "" ).asCString(), vAux);
    i = 0;
    for  (p = vAux.begin(); p != vAux.end(); ++p) {
        pOut->trade_info[i].trade_price = atof((*p).c_str());
        ++i;
    }
    check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
    vAux.clear();
}

void TxnRestDB::execute( const TTradeUpdateFrame3Input *pIn,
                         TTradeUpdateFrame3Output *pOut ) {

    ostringstream osSQL;
    osSQL << "SELECT * FROM TradeUpdateFrame3('" <<
        pIn->end_trade_dts.year << "-" <<
        pIn->end_trade_dts.month << "-" <<
        pIn->end_trade_dts.day << " " <<
        pIn->end_trade_dts.hour << ":" <<
        pIn->end_trade_dts.minute << ":" <<
        pIn->end_trade_dts.second << "'," <<
        pIn->max_acct_id << "," <<
        pIn->max_trades << "," <<
        pIn->max_updates << ",'" <<
        pIn->start_trade_dts.year << "-" <<
        pIn->start_trade_dts.month << "-" <<
        pIn->start_trade_dts.day << " " <<
        pIn->start_trade_dts.hour << ":" <<
        pIn->start_trade_dts.minute << ":" <<
        pIn->start_trade_dts.second << "','" <<
        pIn->symbol << "')";

        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        pOut->num_found = jsonArr->at(0)->get( "num_found", "" ).asInt();

        vector<string> vAux;
        vector<string>::iterator p;

        TokenizeSmart( jsonArr->at(0)->get( "acct_id", "" ).asCString(), vAux);
        int i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].acct_id = atol((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "cash_transaction_amount", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].cash_transaction_amount = atof((*p).c_str());
            ++i;
        }
        // FIXME: According to spec, this may not match the returned number found?
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "cash_transaction_dts", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].cash_transaction_dts.year,
                    &pOut->trade_info[i].cash_transaction_dts.month,
                    &pOut->trade_info[i].cash_transaction_dts.day,
                    &pOut->trade_info[i].cash_transaction_dts.hour,
                    &pOut->trade_info[i].cash_transaction_dts.minute,
                    &pOut->trade_info[i].cash_transaction_dts.second);
            ++i;
        }
        // FIXME: According to spec, this may not match the returned number found?
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "cash_transaction_name", ""  ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->trade_info[i].cash_transaction_name, (*p).c_str(),
                    cCT_NAME_len);
            pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
            ++i;
        }
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "exec_name", "" ).asCString(), vAux );
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->trade_info[i].exec_name, (*p).c_str(), cEXEC_NAME_len);
            pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "is_cash", "" ).asCString(), vAux );
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].is_cash = atof((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        pOut->num_updated = jsonArr->at(0)->get( "num_updated", "" ).asInt();

        TokenizeSmart( jsonArr->at(0)->get("num_updated", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].price = atof((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "quantity", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].quantity = atoi((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "s_name", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->trade_info[i].s_name, (*p).c_str(), cS_NAME_len);
            pOut->trade_info[i].s_name[cS_NAME_len] = '\0';
            ++i;
        }
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "settlement_amount", "" ).asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].settlement_amount = atof((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "settlement_cash_due_date", "").asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].settlement_cash_due_date.year,
                    &pOut->trade_info[i].settlement_cash_due_date.month,
                    &pOut->trade_info[i].settlement_cash_due_date.day,
                    &pOut->trade_info[i].settlement_cash_due_date.hour,
                    &pOut->trade_info[i].settlement_cash_due_date.minute,
                    &pOut->trade_info[i].settlement_cash_due_date.second);
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "settlement_cash_type", "").asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->trade_info[i].settlement_cash_type, (*p).c_str(),
                    cSE_CASH_TYPE_len);
            pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "trade_dts", "").asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            sscanf((*p).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].trade_dts.year,
                    &pOut->trade_info[i].trade_dts.month,
                    &pOut->trade_info[i].trade_dts.day,
                    &pOut->trade_info[i].trade_dts.hour,
                    &pOut->trade_info[i].trade_dts.minute,
                    &pOut->trade_info[i].trade_dts.second);
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeArray( jsonArr->at(0)->get( "trade_history_dts", "").asCString(), vAux);
        i = 0;
        for (p = vAux.begin(); p != vAux.end(); ++p) {
            vector<string> v2;
            vector<string>::iterator p2;
            TokenizeSmart((*p).c_str(), v2);
            int j = 0;
            for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
                sscanf((*p2).c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
                        &pOut->trade_info[i].trade_history_dts[j].year,
                        &pOut->trade_info[i].trade_history_dts[j].month,
                        &pOut->trade_info[i].trade_history_dts[j].day,
                        &pOut->trade_info[i].trade_history_dts[j].hour,
                        &pOut->trade_info[i].trade_history_dts[j].minute,
                        &pOut->trade_info[i].trade_history_dts[j].second);
                ++j;
            }
            ++i;
        }
        vAux.clear();

        TokenizeArray( jsonArr->at(0)->get( "trade_history_status_id", "").asCString(), vAux);
        i = 0;
        for (p = vAux.begin(); p != vAux.end(); ++p) {
            vector<string> v2;
            vector<string>::iterator p2;
            TokenizeSmart((*p).c_str(), v2);
            int j = 0;
            for (p2 = v2.begin(); p2 != v2.end(); ++p2) {
                strncpy(pOut->trade_info[i].trade_history_status_id[j],
                        (*p2).c_str(), cTH_ST_ID_len);
                pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] =
                    '\0';
                ++j;
            }
            ++i;
        }
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "trade_list", "").asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            pOut->trade_info[i].trade_id = atol((*p).c_str());
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "type_name", "").asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->trade_info[i].type_name, (*p).c_str(), cTT_NAME_len);
            pOut->trade_info[i].type_name[cTT_NAME_len] = '\0';
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();

        TokenizeSmart( jsonArr->at(0)->get( "trade_type", "").asCString(), vAux);
        i = 0;
        for  (p = vAux.begin(); p != vAux.end(); ++p) {
            strncpy(pOut->trade_info[i].trade_type, (*p).c_str(), cTT_ID_len);
            pOut->trade_info[i].trade_type[cTT_ID_len] = '\0';
            ++i;
        }
        check_count(pOut->num_found, vAux.size(), __FILE__, __LINE__);
        vAux.clear();
}
void TxnRestDB::reconnect()
{
}

void TxnRestDB::rollbackTransaction()
{
}

void TxnRestDB::setBrokerageHouse( CBrokerageHouse *bh )
{
    this->bh = bh;
}

void TxnRestDB::setReadCommitted()
{
}

void TxnRestDB::setReadUncommitted()
{
}

void TxnRestDB::setRepeatableRead()
{
}

void TxnRestDB::setSerializable()
{
}

void TxnRestDB::startTransaction()
{
}

void TxnRestDB::commitTransaction()
{
}

TxnRestDB::~TxnRestDB() {
    curl_global_cleanup();
}
