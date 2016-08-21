#include <curl/curl.h>
#include <sstream>
#include "TxnRestDB.h"
#include "TxnHarnessStructs.h"
#include "TxnHarnessSendToMarket.h"
#include "DBT5Consts.h"
#include "json.h"

char *TxnRestDB::escape(string s)
{
    //PQescapeLiteral
    //malloc and ecape chars
    return NULL;
}

/* These might just be the most ridiculous macros I have ever written
*  I bet they're fast though
*/
#define INT_VEC_ASSIGN( field, vec, out_struct, out ) \
    do {\
        _Pragma("GCC ivdep") \
        for( unsigned vec_iter = 0; vec_iter < vec->size(); vec_iter++ ) {\
            int ptr_offset = offsetof( out_struct, field ); \
            ((int *)(((char *)out)+ptr_offset))[vec_iter] = vec->at(vec_iter)->get( # field, "" ).asInt(); \
        } \
    } while(0)


#define LONG_VEC_ASSIGN( field, vec, out_struct, out ) \
    do {\
        _Pragma("GCC ivdep") \
        for( unsigned vec_iter = 0; vec_iter < vec->size(); vec_iter++ ) {\
            int ptr_offset = offsetof( out_struct, field ); \
            ((long *)(((char *)out)+ptr_offset))[vec_iter] = vec->at(vec_iter)->get( # field, "" ).asInt64(); \
        } \
    } while(0)

#define FLOAT_VEC_ASSIGN( field, vec, out_struct, out ) \
    do {\
        _Pragma("GCC ivdep") \
        for( unsigned vec_iter = 0; vec_iter < vec->size(); vec_iter++ ) {\
            int ptr_offset = offsetof( out_struct, field ); \
            ((float *)(((char *)out)+ptr_offset))[vec_iter] = vec->at(vec_iter)->get( # field, "" ).asFloat(); \
        } \
    } while(0)

#define STR_VEC_ASSIGN( field, field_max_len, vec, out_struct, out ) \
    do {\
        _Pragma("GCC ivdep") \
        for( unsigned vec_iter = 0; vec_iter < vec->size(); vec_iter++ ) {\
            int ptr_offset = offsetof( out_struct, field ); \
            strncpy( ((char **) (((char *)out)+ptr_offset))[vec_iter], vec->at(vec_iter)->get( # field, "" ).asCString(), field_max_len ); \
            ((char **) (((char *)out)+ptr_offset))[vec_iter][field_max_len] = '\0'; \
        } \
    } while(0)

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
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, os.str().c_str() );
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

void TxnRestDB::execute( const TBrokerVolumeFrame1Input *pIn,
                         TBrokerVolumeFrame1Output *pOut ) {

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
    osSQL << "'" << pIn->broker_list[0] << "'";
    for (int i = 1; pIn->broker_list[i][0] != '\0' &&
            i < TPCE::max_broker_list_len; i++) {
        osSQL << ", '" << pIn->broker_list[i] << "'";
    }
    osSQL << " ) AND ";
    osSQL << "sc_name = '" << pIn->sector_name << "' ";
    osSQL << "GROUP BY b_name ORDER BY 2 DESC";

    jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->list_len = jsonArr->size(); //row_count

    STR_VEC_ASSIGN( broker_name, cB_NAME_len, jsonArr, TBrokerVolumeFrame1Output, pOut );
    FLOAT_VEC_ASSIGN( volume, jsonArr, TBrokerVolumeFrame1Output, pOut );

    //TODO: Free JSON
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
        osSQL.clear();	
        osSQL.str("");
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
    osSQL.clear();
    osSQL.str("");

    //Now retrieve account information
    osSQL << "SELECT ca_id as acct_id, ca_bal as cash_bal, ";
    osSQL << "IFNULL(sum(hs_qty * lt_price), 0) as assets_total ";
    osSQL << "FROM customer_account LEFT OUTER JOIN ";
    osSQL << "holding_summary ON hs_ca_id = ca_id, ";
    osSQL << "last_trade WHERE ca_c_id = " << cust_id;
    osSQL << " AND lt_s_symb = hs_s_symb GROUP BY ";
    osSQL << "ca_id, ca_bal ORDER BY 3 ASC LIMIT 10";

    LONG_VEC_ASSIGN( acct_id, jsonArr, TCustomerPositionFrame1Output, pOut );
    FLOAT_VEC_ASSIGN( asset_total, jsonArr, TCustomerPositionFrame1Output, pOut );
    FLOAT_VEC_ASSIGN( cash_bal, jsonArr, TCustomerPositionFrame1Output, pOut );
}

void TxnRestDB::execute( const TCustomerPositionFrame2Input *pIn,
                         TCustomerPositionFrame2Output *pOut )
{

    ostringstream osSQL;
    osSQL << "SELECT t_id as trade_id, t_s_symb as symbol, t_qty as qty, st_name as trade_status, th_dts as hist_dts ";
    osSQL << "FROM ( SELECT t_id as id FROM trade WHERE t_ca_id = " << pIn->acct_id;
    osSQL << " ORDER BY t_dts DESC LIMIT 10 ) as T, TRADE, TRADE_HISTORY, ";
    osSQL << "STATUS_TYPE WHERE t_id = id AND th_t_id = t_id AND ";
    osSQL << "st_id = th_st_id ORDER BY th_dts DESC LIMIT 30";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->hist_len = jsonArr->size();

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        sscanf(jsonArr->at(i)->get("hist_dts", "" ).asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->hist_dts[i].year,
                &pOut->hist_dts[i].month,
                &pOut->hist_dts[i].day,
                &pOut->hist_dts[i].hour,
                &pOut->hist_dts[i].minute,
                &pOut->hist_dts[i].second);
    }

    INT_VEC_ASSIGN( qty, jsonArr, TCustomerPositionFrame2Output, pOut );
    STR_VEC_ASSIGN( symbol, cSYMBOL_len, jsonArr, TCustomerPositionFrame2Output, pOut );
    LONG_VEC_ASSIGN( trade_id, jsonArr, TCustomerPositionFrame2Output, pOut );
    STR_VEC_ASSIGN( trade_status, cST_NAME_len, jsonArr, TCustomerPositionFrame2Output, pOut );

    //TODO: free jsonArr
}

void TxnRestDB::execute( const TDataMaintenanceFrame1Input *pIn ) {

    //unrolled sproc
    if( strcmp( pIn->table_name, "account_permission" ) == 0 ) {
        //ACCOUNT_PERMISSION TABLE
        ostringstream osSQL;
        osSQL << "SELECT ap_acl FROM account_permission ";
        osSQL << "WHERE ap_ca_id = " << pIn->acct_id << " ORDER BY ";
        osSQL << "ap_acl DESC LIMIT 1";

        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        const char *buff = jsonArr->at(0)->get( "ap_acl", "" ).asCString();
        //TODO: free jsonArr
        if( strcmp( buff, "1111" ) != 0 ) {
            //reset stringstream
            osSQL.clear();
            osSQL.str("");	
            osSQL << "UPDATE account_permission SET ";
            osSQL << "ap_acl = '1111' WHERE ap_ca_id = ";
            osSQL << pIn->acct_id << "ap_acl = '" << buff << "'";
            //TODO: free buff?
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            (void) jsonArr;
        } else {
            //reset stringstream
            osSQL.clear();
            osSQL.str("");	

            osSQL << "UPDATE account_permission SET ";
            osSQL << "ap_acl = '0011' WHERE ap_ca_id = ";
            osSQL << pIn->acct_id << "ap_acl = '" << buff << "'";
            //TODO: free buff?
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            (void) jsonArr;

        }
    } else if( strcmp( pIn->table_name, "address" ) == 0 ) {
        //ADDRESS TABLE
        string line2;
        long ad_id;
        //Retrieve by customer id
        if( pIn->c_id != 0 ) {
            ostringstream osSQL;
            osSQL << "SELECT ad_line2, ad_id FROM ";
            osSQL << "address, customer WHERE ad_id = c_ad_id AND ";
            osSQL << "c_id = " << pIn->c_id;
            std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
            line2 = jsonArr->at(0)->get("ad_line2", "").asString();
            ad_id = jsonArr->at(0)->get("ad_id", "").asInt64();
            //TODO: free jsonArr
        //Retrieve by company id
        } else {
            ostringstream osSQL;
            osSQL << "SELECT ad_line2, ad_id FROM address, company ";
            osSQL << "WHERE ad_id = co_ad_id AND ";
            osSQL << "co_id = " << pIn->co_id;
            std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
            line2 = jsonArr->at(0)->get("ad_line2", "").asString();
            ad_id = jsonArr->at(0)->get("ad_id", "").asInt64();
            //TODO: free jsonArr
        }
        //Toggle line2 entry
        if( strcmp(line2.c_str(), "Apt. 10C") != 0 ) {
            ostringstream osSQL;
            osSQL << "UPDATE address SET ad_line2 ";
            osSQL << "= 'Apt. 10C' WHERE ad_id = ";
            osSQL << ad_id;
            std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
            (void) jsonArr; //dodge compiler warning unused
            //TODO: free jsonArr
        } else {
            ostringstream osSQL;
            osSQL << "UPDATE address SET ad_line2 ";
            osSQL << "= 'Apt. 22' WHERE ad_id = ";
            osSQL << ad_id;
            std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
            (void) jsonArr; //dodge compiler warning unused
            //TODO: free jsonArr
        }
    } else if( strcmp( pIn->table_name, "company" ) ) {
        //COMPANY TABLE
        ostringstream osSQL;
        osSQL << "SELECT co_sp_rate FROM company ";
        osSQL << "WHERE co_id = " << pIn->co_id;
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        string sp_rate = jsonArr->at(0)->get("co_sp_rate", "").asString();
        //TODO: free jsonArr
        if( strcmp( sp_rate.c_str(), "ABA" ) != 0 ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "UPDATE company SET co_sp_rate = ";
            osSQL << "'ABA' WHERE co_id " << pIn->co_id;
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
            (void) jsonArr;
        } else {
            osSQL.clear();
            osSQL.str("");
            osSQL << "UPDATE company SET co_sp_rate = ";
            osSQL << "'AAA' WHERE co_id " << pIn->co_id;
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
            (void) jsonArr;
        }
    } else if( strcmp( pIn->table_name, "customer" ) == 0 ) {
        //CUSTOMER TABLE
        int lenMindSpring = strlen("@mindspring.com");
        ostringstream osSQL;
        osSQL << "SELECT c_email_2 FROM customer WHERE ";
        osSQL << "c_id = " << pIn->c_id;
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        string email2 = jsonArr->at(0)->get("c_email_2", "").asString();
        int email2Len = strlen(email2.c_str());
        //TODO: free jsonArr
        //If our email is longer, just check the domain name on the email
        if ( ( email2Len - lenMindSpring ) > 0 &&
             ( strcmp( email2.c_str() + (email2Len-lenMindSpring-1), "@mindspring.com" ) == 0 ) )  {
            osSQL.clear();
            osSQL.str("");
            osSQL << "UPDATE customer SET c_email_2 = ";
            osSQL << "substring(c_email_2, 1, charindex('@', c_email_2) ) + '@earthlink.com' ";
            osSQL << "WHERE c_id = " << pIn->c_id;
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
            (void) jsonArr;
        } else {
            osSQL.clear();
            osSQL.str("");

            osSQL << "UPDATE customer SET c_email_2 = ";
            osSQL << "substring(c_email_2, 1, charindex('@', c_email_2) ) + '@mindspring.com' ";
            osSQL << "WHERE c_id = " << pIn->c_id;
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
            (void) jsonArr;
        }
    } else if( strcmp( pIn->table_name, "customer_taxrate" ) == 0 ) {
        ostringstream osSQL;
        osSQL << "SELECT cx_tx_id FROM customer_taxrate ";
        osSQL << "WHERE cx_c_id = " << pIn->c_id << " AND ";
        osSQL << "(cx_tx_id LIKE 'US\%' OR cx_tx_id LIKE 'CN\%')";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        string cx_tx_id = jsonArr->at(0)->get("cx_tx_id", "").asString();
        //TODO: free jsonArr
        string upd_cx_tx_id;
        if( strncmp(cx_tx_id.c_str(), "US", 2 ) == 0 ) {
            if( strcmp( cx_tx_id.c_str(), "US1" ) == 0 ) {
                upd_cx_tx_id = "US2";
            } else if( strcmp( cx_tx_id.c_str(), "US2" ) == 0 ) {
                upd_cx_tx_id = "US3";
            } else if( strcmp( cx_tx_id.c_str(), "US3" ) == 0 ) {
                upd_cx_tx_id = "US4";
            } else if( strcmp( cx_tx_id.c_str(), "US4" ) == 0 ) {
                upd_cx_tx_id = "US5";
            } else if( strcmp( cx_tx_id.c_str(), "US5" ) == 0 ) {
                upd_cx_tx_id = "US1";
            }
        } else {
            if( strcmp( cx_tx_id.c_str(), "CN1" ) == 0 ) {
                upd_cx_tx_id = "CN2";
            } else if( strcmp( cx_tx_id.c_str(), "US2" ) == 0 ) {
                upd_cx_tx_id = "CN3";
            } else if( strcmp( cx_tx_id.c_str(), "CN3" ) == 0 ) {
                upd_cx_tx_id = "CN4";
            } else if( strcmp( cx_tx_id.c_str(), "CN4" ) == 0 ) {
                upd_cx_tx_id = "CN1";
            }

        }
        osSQL.clear();
        osSQL.str("");
        osSQL << "UPDATE customer_taxrate SET cx_tx_id = '";
        osSQL << upd_cx_tx_id << "' WHERE cx_c_id = ";
        osSQL << pIn->c_id << " AND cx_tx_id = '" << cx_tx_id << "'";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
        (void) jsonArr;
    } else if( strcmp( pIn->table_name, "daily_market" )  == 0 ) {
        //TODO: I'm not sure about the typing in here, this query might
        //not run
        ostringstream osSQL;
        osSQL << "UPDATE daily_market SET dm_vol = dm_vol + " << pIn->vol_incr;
        osSQL << " WHERE dm_s_symb = '" << pIn->symbol;
        osSQL << "' AND substring(convert(char(8), dm_date, 3), 1, 2) = ";
        osSQL << "'" << pIn->day_of_month << "'";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
    } else if( strcmp( pIn->table_name, "exchange" ) == 0 ) {
        ostringstream osSQL;
        osSQL << "SELECT count(*) as cnt FROM exchange WHERE ex_desc LIKE ";
        osSQL << "'\%LAST UPDATED%'";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        long rowcount = jsonArr->at(0)->get("cnt", "").asInt64();
        //TODO: free jsonArr
        osSQL.clear();
        osSQL.str("");
        if( rowcount == 0 ) {
            //TODO: convert getdatetime() to appropriate MySQL call
            osSQL << "UPDATE exchange SET ex_desc = ex_desc + ";
            osSQL << "' LAST UPDATED ' + getdatetime()";
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
        } else {
            //TODO: fix this query on MySQL
            osSQL << "UPDATE exchange SET ex_desc = substring(";
            osSQL << "ex_desc, 1, len(ex_desc)-len(getdatetime())) + getdatetime()";
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
        }
    } else if( strcmp( pIn->table_name, "financial" ) == 0) {
        ostringstream osSQL;
        osSQL << "SELECT count(*) as cnt FROM financial ";
        osSQL << "WHERE fi_co_id = " << pIn->co_id << " ";
        osSQL << "AND substring(convert(char(8), fi_qtr_start_date, ";
        osSQL << "2) 7, 2) = '01'";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        long rowcount = jsonArr->at(0)->get("cnt", "").asInt64();
        osSQL.clear();
        osSQL.str("");
        //TODO: free jsonArr
        if( rowcount > 0 ) {
            osSQL << "UPDATE financial SET fi_qtr_start_date = ";
            osSQL << "fi_qtr_start_date + 1 day WHERE ";
            osSQL << "fi_co_id = " << pIn->co_id;
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
        } else {
            osSQL << "UPDATE financial SET fi_qtr_start_date = ";
            osSQL << "fi_qtr_start_date - 1 day WHERE ";
            osSQL << "fi_co_id = " << pIn->co_id;
            //TODO: free jsonArr;
        }
    } else if( strcmp( pIn->table_name, "news_item" ) == 0 ) {
        ostringstream osSQL;
        osSQL << "UPDATE news_item SET ni_dts = ni_dts + 1 day ";
        osSQL << "WHERE ni_id = ( SELECT nx_ni_id FROM news_xref ";
        osSQL << "nx_co_id = " << pIn->co_id;
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
    } else if( strcmp( pIn->table_name, "security" )  == 0 ) {
        ostringstream osSQL;
        osSQL << "UPDATE security SET s_exch_date = ";
        osSQL << "s_exch_date + 1 day WHERE s_symb = ";
        osSQL << "'" << pIn->symbol << "'";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
    } else if( strcmp( pIn->table_name, "taxrate" ) == 0 ) {
        ostringstream osSQL;
        osSQL << "SELECT tx_name FROM taxrate WHERE tx_id = " << pIn->tx_id;
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        string tx_name = jsonArr->at(0)->get("tx_name", "").asString();
        size_t pos = tx_name.find(" Tax ");
        osSQL.clear();
        osSQL.str("");
        //TODO: free jsonArr
        osSQL << "UPDATE taxrate SET tx_name = ";
        if( pos == string::npos ) {
            osSQL << "REPLACE( tx_name, ' tax ', ' Tax ' ) ";
        } else {
            osSQL << "REPLACE( tx_name, ' Tax ', ' tax ' ) ";
        }
        osSQL << "WHERE tx_id = " << pIn->tx_id;
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
    } else if( strcmp( pIn->table_name, "watch_item" ) ) {
        //Count number of symbols
        ostringstream osSQL;
        osSQL << "SELECT count(*) as cnt FROM watch_item, watch_list ";
        osSQL << "WHERE wl_c_id = " << pIn->c_id << " AND ";
        osSQL << "wi_wl_id = wl_id";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        long cnt = jsonArr->at(0)->get("cnt", "").asInt64();
        cnt = (cnt+1)/2;
        osSQL.clear(); //reset
        osSQL.str("");

        //TODO: free jsonArr

        //Get middle symbol
        osSQL << "SELECT wi_s_symb, wi_wl_id FROM watch_item, watch_list WHERE ";
        osSQL << "wl_c_id = " << pIn->c_id  << " AND ";
        osSQL << "wi_wl_id = wl_id ORDER BY wi_s_symb ASC";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        string symb = jsonArr->at(cnt)->get("wi_s_symb", "").asString();
        long wl_id = jsonArr->at(cnt)->get("wi_wl_id", "").asInt64();

        osSQL.clear(); //reset
        osSQL.str("");
        //TODO: free jsonArr

        //Get new symbol
        osSQL << "SELECT s_symb FROM security WHERE ";
        osSQL << "s_symb  > '" << symb << "' AND ";
        osSQL << "s_symb NOT IN ( SELECT wi_s_symb FROM ";
        osSQL << "watch_item, watch_list WHERE wl_c_id = ";
        osSQL << pIn->c_id << " AND wi_wl_id = wl_id ) ";
        osSQL << "ORDER BY s_symb ASC LIMIT 1";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        string newsymb = jsonArr->at(0)->get("s_symb", "").asString();
        osSQL.clear(); //reset
        osSQL.str("");
        // TODO: free jsonArr

        osSQL << "UPDATE watch_item SET wi_s_symb = '";
        osSQL << newsymb << "' WHERE wi_wl_id = ";
        osSQL << wl_id << " AND wi_s_symb = '";
        osSQL << symb << "'";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
    }
}

void TxnRestDB::execute( const TMarketFeedFrame1Input *pIn,
                         TMarketFeedFrame1Output *pOut,
                         CSendToMarketInterface *pMarketExchange ) {

    ostringstream osSymbol, osPrice, osQty;
    std::vector<TTradeRequest> vec;

    //All these getdatetime()s are supposed to have the same value.
    //I'm not sure how much this matters to the spec
    long rows_updated = 0;
    long rows_sent = 0;
    for( unsigned int i = 0; i < max_feed_len; i++ ) { 
        ostringstream osSQL;
        osSQL << "UPDATE last_trade SET lt_price = ";
        osSQL << pIn->Entries[i].price_quote << ", ";
        osSQL << "lt_vol = lt_vol + " << pIn->Entries[i].trade_qty;
        osSQL << ", lt_dts = getdatetime() WHERE lt_s_symb = '";
        osSQL << pIn->Entries[i].symbol << "'";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

        osSQL << "SELECT count(*) as rows_modified FROM ";
        osSQL << "last_trade WHERE lt_s_symb = '";
        osSQL << pIn->Entries[i].symbol << "'";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr

        rows_updated += jsonArr->at(0)->get( "rows_modified", "" ).asInt64();

        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT tr_t_id, tr_bid_price, tr_tt_id, ";
        osSQL << "tr_qty FROM trade_request WHERE tr_s_symb = '";
        osSQL << pIn->Entries[i].symbol << "' AND ( (tr_tt_id = '";
        osSQL << pIn->StatusAndTradeType.type_stop_loss << "' AND ";
        osSQL << "tr_bid_price >= " << pIn->Entries[i].price_quote << ") OR ";
        osSQL << "(tr_tt_id = '" << pIn->StatusAndTradeType.type_limit_sell;
        osSQL << "' AND tr_bid_price <= " << pIn->Entries[i].price_quote << ") OR ";
        osSQL << "(tr_tt_id = '" << pIn->StatusAndTradeType.type_limit_buy;
        osSQL << "' AND tr_bid_price >= " << pIn->Entries[i].price_quote;
        osSQL << ") )";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
        osSQL.clear();
        osSQL.str("");

        for( unsigned j = 0; j < jsonArr->size(); j++ ) {
            osSQL << "UPDATE trade SET t_dts = getdatetime(), ";
            osSQL << "t_st_id = '" << pIn->StatusAndTradeType.status_submitted;
            osSQL << "' WHERE t_id = ";
            osSQL << jsonArr->at(j)->get("tr_t_id", "").asInt64();

            std::vector<Json::Value *> *arr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free arr
            osSQL << "DELETE FROM trade_request WHERE tr_t_id = ";
            osSQL << jsonArr->at(j)->get( "tr_t_id", "").asInt64();
            osSQL << " AND tr_bid_price = ";
            osSQL << jsonArr->at(j)->get( "tr_bid_price", "" ).asFloat();
            osSQL << " AND tr_tt_id = '";
            osSQL << jsonArr->at(j)->get( "tr_tt_id", "" ).asString();
            osSQL << "' AND tr_qty = ";
            osSQL << jsonArr->at(j)->get( "tr_qty", "").asInt();
            arr = sendQuery( 1, osSQL.str().c_str() );
            osSQL.clear();
            osSQL.str("");

            //TODO: free arr

            osSQL << "INSERT INTO trade_history VALUES ( ";
            osSQL << jsonArr->at(j)->get( "tr_t_id", "").asInt64();
            osSQL << "," << jsonArr->at(j)->get("tr_bid_price", "").asFloat();
            osSQL << ", getdatetime()";
            osSQL << ", '" << pIn->StatusAndTradeType.status_submitted;
            osSQL << "' )";
            arr = sendQuery( 1, osSQL.str().c_str() );
            osSQL.clear();
            osSQL.str("");
            //TODO: free arr

            TTradeRequest req;
            req.price_quote = jsonArr->at(j)->get( "tr_bid_price", "" ).asFloat();
            req.trade_id = jsonArr->at(j)->get( "tr_id", "" ).asInt64();
            req.trade_qty = jsonArr->at(j)->get( "tr_qty", "" ).asInt();
            strncpy( req.symbol, pIn->Entries[i].symbol, cSYMBOL_len );
            req.symbol[cSYMBOL_len] = '\0';
            strncpy(req.trade_type_id, jsonArr->at(j)->get( "tr_tt_id", "").asCString(), cTT_ID_len);
            req.trade_type_id[cTT_ID_len] = '\0';

            vec.push_back( req );
        }
        bool bSent;
        for( unsigned j = 0; j < vec.size(); j++ ) {
            //Send to market
            bSent = pMarketExchange->SendToMarketFromFrame( vec.at(j) );
        }
        rows_sent += vec.size();

    }
    pOut->send_len = rows_sent; //Is this right?
    pOut->num_updated = rows_updated; //This is an upper bound, not the exact value
}

void TxnRestDB::execute( const TMarketWatchFrame1Input *pIn,
                         TMarketWatchFrame1Output *pOut ) {
    ostringstream osSQL;
    if( pIn->c_id != 0 ) {
        osSQL << "SELECT wi_s_symb as symb FROM watch_item, watch_list ";
        osSQL << "WHERE wi_wl_id = wl_id AND wl_c_id = " << pIn->c_id;
    } else if(  strlen(pIn->industry_name) > 0 ) { 
        osSQL << "SELECT s_symb as symb FROM industry, company, security ";
        osSQL << "WHERE in_name = '" << pIn->industry_name << "' AND ";
        osSQL << "co_in_id = in_id AND co_id BETWEEN " << pIn->starting_co_id << " ";
        osSQL << "AND " << pIn->ending_co_id << " AND s_co_id = co_id";
    } else if( pIn->acct_id != 0 ) {
        osSQL << "SELECT hs_s_symb FROM holding_summary WHERE hs_ca_id = ";
        osSQL << pIn->acct_id;
    }
    std::vector<Json::Value *> *cursor = sendQuery( 1, osSQL.str().c_str() );

    float old_mkt_cap = 0;
    float new_mkt_cap = 0;
    float pct_change = 0;
    for( unsigned j = 0; j < cursor->size(); j++ ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT lt_price FROM last_trade WHERE lt_s_symb = '";
        osSQL << cursor->at(j)->get("symb", "").asString() << "'";
        std::vector<Json::Value *> *lt_res = sendQuery( 1, osSQL.str().c_str() );
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT s_num_out FROM security WHERE s_symb = '";
        osSQL << cursor->at(j)->get("symb","").asString() << "'";
        std::vector<Json::Value *> *sec_res = sendQuery( 1, osSQL.str().c_str() );
        osSQL.clear();
        osSQL.str("");
        //TODO: verify this is the right date format for mysql comparisons
        osSQL << "SELECT dm_close FROM daily_market WHERE dm_s_symb ='";
        osSQL << cursor->at(j)->get("symb", "").asString() << "' ";
        osSQL << "AND dm_date = '" << pIn->start_day.year << "-";
        osSQL << pIn->start_day.month << "-" << pIn->start_day.day << "'";
        std::vector<Json::Value *> *dm_res = sendQuery( 1, osSQL.str().c_str() );

        old_mkt_cap += sec_res->at(0)->get("s_num_out","").asFloat() * dm_res->at(0)->get("dm_close", "").asFloat();
        new_mkt_cap += sec_res->at(0)->get("s_num_out","").asFloat() * lt_res->at(0)->get("lt_price", "").asFloat();
    }
    if( old_mkt_cap != 0 ) {
        pct_change = 100 * (new_mkt_cap/old_mkt_cap-1);
    } else {
        pct_change = 0;
    }
    pOut->pct_change = pct_change;

}

void TxnRestDB::execute( const TSecurityDetailFrame1Input *pIn,
                         TSecurityDetailFrame1Output *pOut ) {
    ostringstream osSQL;

    //Unrolled sproc
    //TODO: might be missing some out fields...

    //TODO: almost certainly typos in here, not sure about join interations with json, expect errors
    osSQL << "SELECT s_name, co_id, co_name, co_sp_rate, co_ceo, co_desc, co_open_date, co_st_id, ca.ad_line1, ";
    osSQL << "ca.ad_line2, zca.zc_town, zca.zc_div, ca.ad_zc_code, ca.ad_ctry, s_num_out, s_start_date, s_exch_date, ";
    osSQL << "s_pe, s_52wk_high, s_52wk_high_date, s_52wk_low, s_52wk_low_date, s_dividend, s_yield, zea.zc_div, ";
    osSQL << "ea.ad_ctry, ea.ad_line1, ea.ad_line2, zea.zc_town, ea.ad_zc_code, ex_close, ex_desc, ex_name, ex_num_symb, ";
    osSQL << "ex_open FROM security, company, address ca, address ea, zip_code zca, zip_code zea, exchange  WHERE ";
    osSQL << "s_symb = '" << pIn->symbol << "' AND co_id = s_co_id AND ca.ad_id = co_ad_id AND ea.ad_id = ex_ad_id AND ";
    osSQL << "ex_id = s_ex_id AND ca.ad_zc_code = zca.zc_code AND ea.ad_zc_code = zea.zc_code";
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT co_name, in_name FROM company_competitor, company, industry WHERE ";
    osSQL << "cp_co_id = " << jsonArr->at(0)->get("co_id", "").asInt64() << " AND ";
    osSQL << "co_id = cp_comp_co_id AND in_id = cp_in_id LIMIT 3";
    std::vector<Json::Value *> *namesArr = sendQuery( 1, osSQL.str().c_str() );

    osSQL.str("");
    osSQL << "SELECT fi_year, fi_qtr, fi_qtr_start_date, fi_revenue, fi_net_earn, fi_basic_eps, fi_dilut_eps, "; 
    osSQL << "fi_margin, fi_inventory, fi_assets, fi_liability, fi_out_basic, fi_out_dilut FROM financial WHERE ";
    osSQL << "fi_co_id = " << jsonArr->at(0)->get("co_id", "").asInt64() << " ORDER BY fi_year ASC, fi_qtr LIMIT 120";
    std::vector<Json::Value *> *fiArr = sendQuery( 1, osSQL.str().c_str() );
    long fin_len = fiArr->size();

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT dm_date, dm_close, dm_high, dm_low, dm_vol FROM daily_market ";
    osSQL << "dm_s_symb = '"  << pIn->symbol << "' AND dm_date >= " << jsonArr->at(0)->get("s_start_date", "").asString();
    osSQL << " ORDER BY dm_date ASC LIMIT " << pIn->max_rows_to_return;  
    std::vector<Json::Value *> *dmArr = sendQuery( 1, osSQL.str().c_str() );
    long day_len = dmArr->size();
    
    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT lt_price, lt_open_price, lt_vol FROM last_trade WHERE lt_s_symb = '";
    osSQL << pIn->symbol << "'";
    std::vector<Json::Value *> *ltArr = sendQuery( 1, osSQL.str().c_str() );

    std::vector<Json::Value *> *newsArr;
    if( pIn->access_lob_flag ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT ni_item, ni_dts, ni_source, ni_author, '' as ni_headline, '' as ni_summary FROM news_xref, news_item "; 
        osSQL << "ni_id = nx_ni_id AND nx_co_id = " << jsonArr->at(0)->get( "co_id", "" ).asInt64() << " LIMIT 2";
        newsArr = sendQuery( 1, osSQL.str().c_str() );
    } else {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT  '' as ni_item, ni_dts, ni_source, ni_author, ni_headline, ni_summary FROM news_xref, news_item "; 
        osSQL << "ni_id = nx_ni_id AND nx_co_id = " << jsonArr->at(0)->get( "co_id", "" ).asInt64() <<  " LIMIT 2";
        newsArr = sendQuery( 1, osSQL.str().c_str() );
    }
    long news_len = newsArr->size();

    pOut->fin_len = fin_len;
    pOut->day_len = day_len;
    pOut->news_len = news_len;

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

    strncpy(pOut->ceo_name, jsonArr->at(0)->get("co_ceo", "").asCString(), cCEO_NAME_len);
    pOut->ceo_name[cCEO_NAME_len] = '\0';
    strncpy(pOut->co_ad_cty, jsonArr->at(0)->get("ca.ad_ctry", "").asCString(), cAD_CTRY_len);
    pOut->co_ad_cty[cAD_CTRY_len] = '\0';
    strncpy(pOut->co_ad_div, jsonArr->at(0)->get("zca.zc_div", "").asCString(), cAD_DIV_len);
    pOut->co_ad_div[cAD_DIV_len] = '\0';
    strncpy(pOut->co_ad_line1, jsonArr->at(0)->get("ca.ad_line1", "").asCString(), cAD_LINE_len);
    pOut->co_ad_line1[cAD_LINE_len] = '\0';
    strncpy(pOut->co_ad_line2, jsonArr->at(0)->get("ca.ad_line2", "").asCString(), cAD_LINE_len);
    pOut->co_ad_line2[cAD_LINE_len] = '\0';
    strncpy(pOut->co_ad_town, jsonArr->at(0)->get("zca.zc_town", "").asCString(), cAD_TOWN_len);
    pOut->co_ad_town[cAD_TOWN_len] = '\0';
    strncpy(pOut->co_ad_zip, jsonArr->at(0)->get("ca.ad_zc_code", "").asCString(), cAD_ZIP_len);
    pOut->co_ad_zip[cAD_ZIP_len] = '\0';
    strncpy(pOut->co_desc, jsonArr->at(0)->get("co_desc", "").asCString(), cCO_DESC_len);
    pOut->co_desc[cCO_DESC_len] = '\0';
    strncpy(pOut->co_name, jsonArr->at(0)->get("co_name", "").asCString(), cCO_NAME_len);
    pOut->co_name[cCO_NAME_len] = '\0';
    strncpy(pOut->co_st_id, jsonArr->at(0)->get("co_st_id", "").asCString(), cST_ID_len);
    pOut->co_st_id[cST_ID_len] = '\0';

    for( unsigned j = 0; j < namesArr->size(); j++ ) {
        strncpy(pOut->cp_co_name[j], namesArr->at(j)->get("co_name", "").asCString(), cCO_NAME_len);
        strncpy(pOut->cp_in_name[j], namesArr->at(j)->get("in_name", "").asCString(), cIN_NAME_len);
        pOut->cp_co_name[j][cCO_NAME_len] = '\0';
        pOut->cp_in_name[j][cIN_NAME_len] = '\0';
    }


    for( unsigned j = 0; j < dmArr->size(); j++ ) {
        sscanf(dmArr->at(j)->get("dm_date", "").asCString(), "%hd-%hd-%hd",
                &pOut->day[j].date.year,
                &pOut->day[j].date.month,
                &pOut->day[j].date.day);
        pOut->day[j].close = dmArr->at(j)->get("dm_close", "").asFloat();
        pOut->day[j].high = dmArr->at(j)->get("dm_high", "").asFloat();
        pOut->day[j].low = dmArr->at(j)->get("dm_low", "").asFloat();
        pOut->day[j].vol = dmArr->at(j)->get("dm_vol", "").asFloat();
    }

    pOut->divid = jsonArr->at(0)->get("s_dividend","").asFloat();

    strncpy(pOut->ex_ad_cty, jsonArr->at(0)->get("ea.ad_ctry","").asCString(), cAD_CTRY_len);
    pOut->ex_ad_cty[cAD_CTRY_len] = '\0';
    strncpy(pOut->ex_ad_div, jsonArr->at(0)->get("zea.zc_div", "").asCString(), cAD_DIV_len);
    pOut->ex_ad_div[cAD_DIV_len] = '\0';
    strncpy(pOut->ex_ad_line1, jsonArr->at(0)->get("ea.ad_line1", "").asCString(), cAD_LINE_len);
    pOut->ex_ad_line1[cAD_LINE_len] = '\0';
    strncpy(pOut->ex_ad_line2, jsonArr->at(0)->get("ea.ad_line2", "").asCString(), cAD_LINE_len);
    pOut->ex_ad_line2[cAD_LINE_len] = '\0';
    strncpy(pOut->ex_ad_town, jsonArr->at(0)->get("zea.zc_town", "").asCString(), cAD_TOWN_len);
    pOut->ex_ad_town[cAD_TOWN_len]  = '\0';
    strncpy(pOut->ex_ad_zip, jsonArr->at(0)->get("ea.ad_zc_zip", "").asCString(), cAD_ZIP_len);
    pOut->ex_ad_zip[cAD_ZIP_len] = '\0';
    pOut->ex_close = jsonArr->at(0)->get("ex_close", "").asFloat();
    sscanf(jsonArr->at(0)->get("s_exch_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->ex_date.year,
            &pOut->ex_date.month,
            &pOut->ex_date.day);
    strncpy(pOut->ex_desc, jsonArr->at(0)->get("ex_desc", "").asCString(), cEX_DESC_len);
    pOut->ex_desc[cEX_DESC_len] = '\0';
    strncpy(pOut->ex_name, jsonArr->at(0)->get("ex_name", "").asCString(), cEX_NAME_len);
    pOut->ex_name[cEX_NAME_len] = '\0';
    pOut->ex_num_symb = jsonArr->at(0)->get("ex_num_symb", "").asInt();
    pOut->ex_open = jsonArr->at(0)->get("ex_open", "").asFloat();

    for( unsigned i = 0; i < fiArr->size(); i++ ) {
        pOut->fin[i].year = fiArr->at(i)->get( "fi_year", "" ).asInt();
        pOut->fin[i].qtr = fiArr->at(i)->get( "fi_qtr", "").asInt();
        sscanf( fiArr->at(i)->get( "fi_qtr_start_date", "").asCString(), "%hd-%hd-%hd",
                &pOut->fin[i].start_date.year,
                &pOut->fin[i].start_date.month,
                &pOut->fin[i].start_date.day);
        pOut->fin[i].rev = fiArr->at(i)->get("fi_revenue","").asFloat(); 
        pOut->fin[i].net_earn = fiArr->at(i)->get("fi_net_earn","").asFloat();
        pOut->fin[i].basic_eps = fiArr->at(i)->get("fi_basic_eps", "").asFloat();
        pOut->fin[i].dilut_eps = fiArr->at(i)->get("fi_dilut_eps", "").asFloat();
        pOut->fin[i].margin = fiArr->at(i)->get("fi_margin", "").asFloat();
        pOut->fin[i].invent = fiArr->at(i)->get("fi_inventory", "").asFloat();
        pOut->fin[i].assets = fiArr->at(i)->get("fi_assets", "").asFloat();
        pOut->fin[i].liab = fiArr->at(i)->get("fi_liability", "").asFloat();
        pOut->fin[i].out_basic = fiArr->at(i)->get("fi_out_basic", "").asFloat();
        pOut->fin[i].out_dilut = fiArr->at(i)->get("fi_out_dilut", "").asFloat();
    }

    pOut->last_open = ltArr->at(0)->get("lt_open_price","").asFloat();
    pOut->last_price = jsonArr->at(0)->get("lt_price", "").asFloat();
    pOut->last_vol = jsonArr->at(0)->get("lt_vol", "").asInt();

    for( unsigned j = 0; j < fiArr->size(); j++ ) {
        strncpy(pOut->news[j].item, newsArr->at(j)->get("ni_item", "").asCString(), cNI_ITEM_len);
        pOut->news[j].item[cNI_ITEM_len] = '\0';
        sscanf(newsArr->at(j)->get( "ni_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->news[j].dts.year,
                &pOut->news[j].dts.month,
                &pOut->news[j].dts.day,
                &pOut->news[j].dts.hour,
                &pOut->news[j].dts.minute,
                &pOut->news[j].dts.second);
        strncpy(pOut->news[j].src, newsArr->at(j)->get("ni_source", "").asCString(), cNI_SOURCE_len);
        pOut->news[j].src[cNI_SOURCE_len] = '\0';
        strncpy(pOut->news[j].auth, newsArr->at(j)->get("ni_author", "").asCString(), cNI_AUTHOR_len);
        pOut->news[j].auth[cNI_AUTHOR_len] = '\0';
        strncpy(pOut->news[j].headline, newsArr->at(j)->get("ni_headline", "").asCString(), cNI_HEADLINE_len);
        pOut->news[j].headline[cNI_HEADLINE_len] = '\0';
        strncpy(pOut->news[j].summary, newsArr->at(j)->get("ni_summary","").asCString(), cNI_SUMMARY_len);
        pOut->news[j].summary[cNI_SUMMARY_len] = '\0';

    }
    sscanf(jsonArr->at(0)->get("co_open_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->open_date.year,
            &pOut->open_date.month,
            &pOut->open_date.day);
    pOut->pe_ratio = jsonArr->at(0)->get("s_pe", "").asFloat();
    strncpy(pOut->s_name, jsonArr->at(0)->get("s_name", "").asCString(), cS_NAME_len);
    pOut->s_name[cS_NAME_len] = '\0';
    pOut->num_out = jsonArr->at(0)->get("s_num_out", "").asInt64();
    strncpy(pOut->sp_rate, jsonArr->at(0)->get("sp_rate", "").asCString(), cSP_RATE_len);
    pOut->sp_rate[cSP_RATE_len] = '\0';
    sscanf(jsonArr->at(0)->get("s_start_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->start_date.year,
            &pOut->start_date.month,
            &pOut->start_date.day);
    pOut->yield = jsonArr->at(0)->get("s_yield", "").asFloat();

    //TODO: free jsonArr
    //TODO: free dmArr
    //TODO: free ltArr
    //TODO: free fiArr
    //TODO: free newsArr
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
