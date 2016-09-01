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
    char *str = (char *) malloc( s.size() * 2 + 1 );

    int j = 0;
    for( int i = 0; i < s.size(); i++ ) { 
        if( s.at(i) == '\'' ) {
            str[j++] == '\\';
        }
        str[j++] = s.at(i);
    }
	str[j] = '\0';
    return str;
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
    cout << "In callback..." << endl;
    cout << "Ptr: " << ptr << endl;
    cout << "Size: " << size << endl;
    cout << "Num Members: " << nmemb << endl;
    //TODO: how does this work with multiple items?
    Json::Reader reader;
    Json::Value *val = new Json::Value();
    reader.parse( ptr, ptr + (size * nmemb), *val );
    std::vector<Json::Value *> *arr = new std::vector<Json::Value *>();
    if( !val->isArray() ) {
        cout << "Not an array..." << endl;
    } else {
        for( unsigned i = 0; i < val->size(); i++ ) {
            arr->push_back( new Json::Value((*val)[i]) );
        }
    }
    std::vector<Json::Value *> **arrPtr= (std::vector<Json::Value *> **) userdata;
    *arrPtr = arr;
    //Why does this return the size written?
    return size*nmemb;
}

std::vector<Json::Value *> *TxnRestDB::sendQuery( int clientId, string query ){
    cout << "Sending Query to " << REST_QUERY_URL << endl;
    CURLcode res;
    char url[64];
    char buff[2048];	
    ostringstream os;
    curl = curl_easy_init();
    snprintf( url, 64, REST_QUERY_URL, clientId );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    struct curl_slist *chunk = NULL;
    chunk = curl_slist_append(chunk, "Content-Type: application/json");

    os << "{ \"query\": \"";
    os << query;
    os << "\" }";

    //Have the write callback set this ptr to the allocated data
    std::vector<Json::Value *> *resultArr = NULL;
    strncpy( buff, os.str().c_str(), 2048 );
    cout << "Sending Query: " << buff << endl;
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, buff );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_callback );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *) (&resultArr) );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, chunk );
    res = curl_easy_perform( curl );
    cout << "Query sent!" << endl;
    if( res != CURLE_OK ) {
        cout << "Error sending query:" << res << endl;
        //ERROR, handle this!
    }
    //Pull out results, return
    curl_easy_cleanup( curl );
    curl_slist_free_all( chunk );
    return resultArr;
}

void TxnRestDB::execute( const TBrokerVolumeFrame1Input *pIn,
                         TBrokerVolumeFrame1Output *pOut ) {

    std::vector<Json::Value *> *jsonArr;

    ostringstream osBrokers;
    ostringstream osSQL;

    //Unrolled Transaction
    osSQL << "SELECT b_name, ";
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

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        strncpy( pOut->broker_name[i], jsonArr->at(i)->get("b_name", "").asCString(),
                 cB_NAME_len );
        pOut->broker_name[i][cB_NAME_len] = '\0';
        pOut->volume[i] = jsonArr->at(i)->get("volume", "").asDouble();
    }

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
        osSQL << "SELECT c_id FROM customer WHERE ";
        osSQL << "c_tax_id = '" << pIn->tax_id << "'";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        cust_id = jsonArr->at(0)->get( "c_id", "" ).asInt64();
        pOut->cust_id = jsonArr->at(0)->get( "c_id", "" ).asInt64();
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

    pOut->acct_len = jsonArr->size();
    pOut->cust_id = cust_id;

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

	cout << "Done third block" << endl;
	

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

	cout << "Done fourth block" << endl;

    strncpy(pOut->c_m_name, jsonArr->at(0)->get("c_m_name", "").asCString(), cM_NAME_len);
    pOut->c_m_name[cM_NAME_len] = '\0';
	cout << "c_m_name done" << endl;
    strncpy(pOut->c_st_id, jsonArr->at(0)->get("c_st_id", "").asCString(), cST_ID_len);
    pOut->c_st_id[cST_ID_len] = '\0';
	cout << "c_st_name done" << endl;
    pOut->c_tier = jsonArr->at(0)->get("c_tier", "").asInt64();
	cout << "Done reading stuff" << endl;
    //TODO: free jsonArr
    //Reset stringstream
    osSQL.clear();
    osSQL.str("");

    //Now retrieve account information
    osSQL << "SELECT ca_id, ca_bal, ";
    osSQL << "IFNULL(sum(hs_qty * lt_price), 0) as asset_total ";
    osSQL << "FROM customer_account LEFT OUTER JOIN ";
    osSQL << "holding_summary ON hs_ca_id = ca_id, ";
    osSQL << "last_trade WHERE ca_c_id = " << cust_id;
    osSQL << " AND lt_s_symb = hs_s_symb GROUP BY ";
    osSQL << "ca_id, ca_bal ORDER BY 3 ASC LIMIT 10";

    jsonArr = sendQuery( 1, osSQL.str().c_str() );

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        pOut->acct_id[i] = jsonArr->at(i)->get("ca_id", "").asInt64();
        pOut->cash_bal[i] = jsonArr->at(i)->get("ca_bal", "").asDouble();
        pOut->asset_total[i] = jsonArr->at(i)->get("asset_total", "").asDouble();
    }
   cout << "Done First Frame..." << endl;
}

void TxnRestDB::execute( const TCustomerPositionFrame2Input *pIn,
                         TCustomerPositionFrame2Output *pOut )
{
	cout << "Starting Second Frame" << endl;

    ostringstream osSQL;
    osSQL << "SELECT t_id, t_s_symb, t_qty, st_name, th_dts ";
    osSQL << "FROM ( SELECT t_id as id FROM trade WHERE t_ca_id = " << pIn->acct_id;
    osSQL << " ORDER BY t_dts DESC LIMIT 10 ) as t, trade, trade_history, ";
    osSQL << "status_type WHERE t_id = id AND th_t_id = t_id AND ";
    osSQL << "st_id = th_st_id ORDER BY th_dts DESC LIMIT 30";

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->hist_len = jsonArr->size();

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        pOut->trade_id[i] = jsonArr->at(i)->get("t_id", "").asInt64();
        strncpy( pOut->symbol[i],  jsonArr->at(i)->get("t_s_symb", "").asCString(),
                 cSYMBOL_len );
        pOut->symbol[i][cSYMBOL_len] = '\0';
        pOut->qty[i] = jsonArr->at(i)->get("t_qty", "").asInt();
        strncpy( pOut->trade_status[i], jsonArr->at(i)->get("st_name", "").asCString(),
                 cST_NAME_len );
        pOut->trade_status[i][cST_NAME_len] = '\0';
        time_t t = jsonArr->at(i)->get( "th_dts", "" ).asInt64() / 1000;
        struct tm *ti = localtime( &t );
        char buff[56];
        strftime( buff, 56, "%F %T", ti );

        sscanf( buff, "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->hist_dts[i].year,
                &pOut->hist_dts[i].month,
                &pOut->hist_dts[i].day,
                &pOut->hist_dts[i].hour,
                &pOut->hist_dts[i].minute,
                &pOut->hist_dts[i].second);
    }

	cout << "Done second frame" << endl;
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
    } else if( strcmp( pIn->table_name, "company" ) == 0 ) {
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
            osSQL << "'ABA' WHERE co_id = " << pIn->co_id;
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
            (void) jsonArr;
        } else {
            osSQL.clear();
            osSQL.str("");
            osSQL << "UPDATE company SET co_sp_rate = ";
            osSQL << "'AAA' WHERE co_id = " << pIn->co_id;
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
            //TODO: convert now() to appropriate MySQL call
            osSQL << "UPDATE exchange SET ex_desc = ex_desc + ";
            osSQL << "' LAST UPDATED ' + now()";
            jsonArr = sendQuery( 1, osSQL.str().c_str() );
            //TODO: free jsonArr
        } else {
            //TODO: fix this query on MySQL
            osSQL << "UPDATE exchange SET ex_desc = substring(";
            osSQL << "ex_desc, 1, len(ex_desc)-len(now())) + now()";
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
    } else if( strcmp( pIn->table_name, "watch_item" ) == 0 ) {
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

    //All these now()s are supposed to have the same value.
    //I'm not sure how much this matters to the spec
    long rows_updated = 0;
    long rows_sent = 0;
	cout << "max_feed_len: " << max_feed_len << endl;
    for( unsigned int i = 0; i < max_feed_len; i++ ) { 
	cout << "market_feed iter " << i << endl;
        ostringstream osSQL;
        osSQL << "UPDATE last_trade SET lt_price = ";
        osSQL << pIn->Entries[i].price_quote << ", ";
        osSQL << "lt_vol = lt_vol + " << pIn->Entries[i].trade_qty;
        osSQL << ", lt_dts = now() WHERE lt_s_symb = '";
        osSQL << pIn->Entries[i].symbol << "'";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

	osSQL.clear();
	osSQL.str("");
        osSQL << "SELECT count(*) as rows_modified FROM ";
        osSQL << "last_trade WHERE lt_s_symb = '";
        osSQL << pIn->Entries[i].symbol << "'";
        jsonArr = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free jsonArr
       
	cout << "After count";

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
	cout << "market_feed entering second inner loop" << endl;

        for( unsigned j = 0; j < jsonArr->size(); j++ ) {
		cout << "market_feed second inner loop running... " << j << endl;
            osSQL << "UPDATE trade SET t_dts = now(), ";
            osSQL << "t_st_id = '" << pIn->StatusAndTradeType.status_submitted;
            osSQL << "' WHERE t_id = ";
            osSQL << jsonArr->at(j)->get("tr_t_id", "").asInt64();

            std::vector<Json::Value *> *arr = sendQuery( 1, osSQL.str().c_str() );
		cout << "market_feed second inner loop block 1 done" << endl;
            //TODO: free arr
            osSQL.clear();
            osSQL.str("");
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

		cout << "market_feed second inner loop block 2 done" << endl;

            //TODO: free arr

            	cout << "Inserting into trade history... " << endl;
            osSQL << "INSERT INTO trade_history( th_t_id, th_dts, th_st_id ) VALUES ( ";
            osSQL << jsonArr->at(j)->get( "tr_t_id", "").asInt64();
            osSQL << ", now()";
            osSQL << ", '" << pIn->StatusAndTradeType.status_submitted;
            osSQL << "' )";
            arr = sendQuery( 1, osSQL.str().c_str() );
            osSQL.clear();
            osSQL.str("");
            //TODO: free arr
            //
		cout << "market_feed second inner loop block 3 done" << endl;

            TTradeRequest req;
            req.price_quote = jsonArr->at(j)->get( "tr_bid_price", "" ).asFloat();
		cout << "market_feed price quote" << endl;
            req.trade_id = jsonArr->at(j)->get( "tr_t_id", "" ).asInt64();
		cout << "market_feed trade id" << endl;
            req.trade_qty = jsonArr->at(j)->get( "tr_qty", "" ).asInt();
		cout << "market_feed trade_qty" << endl;
            strncpy( req.symbol, pIn->Entries[i].symbol, cSYMBOL_len );
		cout << "market_feed symbol" << endl;
            req.symbol[cSYMBOL_len] = '\0';
            strncpy(req.trade_type_id, jsonArr->at(j)->get( "tr_tt_id", "").asCString(), cTT_ID_len);
		cout << "market_feed trade_type" << endl;
            req.trade_type_id[cTT_ID_len] = '\0';


            vec.push_back( req );
		cout << "market_feed iteration "<< j << " Done" << endl;
        }
	cout << "market_feed finished inner loop for iter: " << i  << endl;
        bool bSent;
        for( unsigned j = 0; j < vec.size(); j++ ) {
            //Send to market
            bSent = pMarketExchange->SendToMarketFromFrame( vec.at(j) );
        }
	cout << "market_feed done iter " << i << endl;
        rows_sent += vec.size();

    }
    pOut->send_len = rows_sent; //Is this right?
    pOut->num_updated = rows_updated; //This is an upper bound, not the exact value
	cout << "Done market_feed" << endl;
}

void TxnRestDB::execute( const TMarketWatchFrame1Input *pIn,
                         TMarketWatchFrame1Output *pOut ) {
	cout << "Starting market watch..." << endl;
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
        osSQL << "SELECT hs_s_symb as symb FROM holding_summary WHERE hs_ca_id = ";
        osSQL << pIn->acct_id;
    }
    std::vector<Json::Value *> *cursor = sendQuery( 1, osSQL.str().c_str() );

	cout << "Query sent OK" << endl;
    float old_mkt_cap = 0;
    float new_mkt_cap = 0;
    float pct_change = 0;
    for( unsigned j = 0; j < cursor->size(); j++ ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT lt_price FROM last_trade WHERE lt_s_symb = '";
        osSQL << cursor->at(j)->get("symb", "").asString() << "'";
	cout << "Read symb..." << endl;;
        std::vector<Json::Value *> *lt_res = sendQuery( 1, osSQL.str().c_str() );
	cout << "Sent lt query..." << endl;
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT s_num_out FROM security WHERE s_symb = '";
        osSQL << cursor->at(j)->get("symb","").asString() << "'";
        std::vector<Json::Value *> *sec_res = sendQuery( 1, osSQL.str().c_str() );
	cout << "Sent out sec query..." << endl;
        osSQL.clear();
        osSQL.str("");
        //TODO: verify this is the right date format for mysql comparisons
        osSQL << "SELECT dm_close FROM daily_market WHERE dm_s_symb ='";
        osSQL << cursor->at(j)->get("symb", "").asString() << "' ";
        osSQL << "AND dm_date = '" << pIn->start_day.year << "-";
        osSQL << pIn->start_day.month << "-" << pIn->start_day.day << "'";
        std::vector<Json::Value *> *dm_res = sendQuery( 1, osSQL.str().c_str() );
	cout << "dm query sent!" << endl;

        old_mkt_cap += sec_res->at(0)->get("s_num_out","").asFloat() * dm_res->at(0)->get("dm_close", "").asFloat();
        new_mkt_cap += sec_res->at(0)->get("s_num_out","").asFloat() * lt_res->at(0)->get("lt_price", "").asFloat();
	cout << "Done reading s_num_out, dm_close, and lt_price" << endl;
    }
	cout << "Survived first block..." << endl;
    if( old_mkt_cap != 0 ) {
        pct_change = 100 * (new_mkt_cap/old_mkt_cap-1);
    } else {
        pct_change = 0;
    }
    pOut->pct_change = pct_change;
	cout << "Got er done!" << endl;

}

void TxnRestDB::execute( const TSecurityDetailFrame1Input *pIn,
                         TSecurityDetailFrame1Output *pOut ) {
    ostringstream osSQL;

    //Unrolled sproc
    //TODO: might be missing some out fields...

	cout << "starting security detail..." << endl;
    //TODO: almost certainly typos in here, not sure about join interations with json, expect errors
    osSQL << "SELECT s_name, co_id, co_name, co_sp_rate, co_ceo, co_desc, co_open_date, co_st_id, ca.ad_line1, ";
    osSQL << "ca.ad_line2, zca.zc_town, zca.zc_div, ca.ad_zc_code, ca.ad_ctry, s_num_out, s_start_date, s_exch_date, ";
    osSQL << "s_pe, s_52wk_high, s_52wk_high_date, s_52wk_low, s_52wk_low_date, s_dividend, s_yield, zea.zc_div, ";
    osSQL << "ea.ad_ctry, ea.ad_line1, ea.ad_line2, zea.zc_town, ea.ad_zc_code, ex_close, ex_desc, ex_name, ex_num_symb, ";
    osSQL << "ex_open FROM security, company, address ca, address ea, zip_code zca, zip_code zea, exchange  WHERE ";
    osSQL << "s_symb = '" << pIn->symbol << "' AND co_id = s_co_id AND ca.ad_id = co_ad_id AND ea.ad_id = ex_ad_id AND ";
    osSQL << "ex_id = s_ex_id AND ca.ad_zc_code = zca.zc_code AND ea.ad_zc_code = zea.zc_code";
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
	cout << "Query sent and read..." << endl;

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT co_name, in_name FROM company_competitor, company, industry WHERE ";
    osSQL << "cp_co_id = " << jsonArr->at(0)->get("co_id", "").asInt64() << " AND ";
    osSQL << "co_id = cp_comp_co_id AND in_id = cp_in_id LIMIT 3";
    std::vector<Json::Value *> *namesArr = sendQuery( 1, osSQL.str().c_str() );
	cout << "Second query sent!" << endl;

    osSQL.str("");
    osSQL << "SELECT fi_year, fi_qtr, fi_qtr_start_date, fi_revenue, fi_net_earn, fi_basic_eps, fi_dilut_eps, "; 
    osSQL << "fi_margin, fi_inventory, fi_assets, fi_liability, fi_out_basic, fi_out_dilut FROM financial WHERE ";
    osSQL << "fi_co_id = " << jsonArr->at(0)->get("co_id", "").asInt64() << " ORDER BY fi_year ASC, fi_qtr LIMIT 120";
    std::vector<Json::Value *> *fiArr = sendQuery( 1, osSQL.str().c_str() );
    long fin_len = fiArr->size();

	cout << "Third query sent!" << endl;

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT dm_date, dm_close, dm_high, dm_low, dm_vol FROM daily_market WHERE ";
    osSQL << "dm_s_symb = '"  << pIn->symbol << "' AND dm_date >= '" << jsonArr->at(0)->get("s_start_date", "").asString();
    osSQL << "' ORDER BY dm_date ASC LIMIT " << pIn->max_rows_to_return;  
    std::vector<Json::Value *> *dmArr = sendQuery( 1, osSQL.str().c_str() );
    long day_len = dmArr->size();
	
	cout << "Fourth query sent!" << endl;
    
    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT lt_price, lt_open_price, lt_vol FROM last_trade WHERE lt_s_symb = '";
    osSQL << pIn->symbol << "'";
    std::vector<Json::Value *> *ltArr = sendQuery( 1, osSQL.str().c_str() );

	cout << "Fifth query sent!" << endl;

    std::vector<Json::Value *> *newsArr;
    if( pIn->access_lob_flag ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT ni_item, ni_dts, ni_source, ni_author, '' as ni_headline, '' as ni_summary FROM news_xref, news_item WHERE "; 
        osSQL << "ni_id = nx_ni_id AND nx_co_id = " << jsonArr->at(0)->get( "co_id", "" ).asInt64() << " LIMIT 2";
        newsArr = sendQuery( 1, osSQL.str().c_str() );
    } else {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT  '' as ni_item, ni_dts, ni_source, ni_author, ni_headline, ni_summary FROM news_xref, news_item WHERE "; 
        osSQL << "ni_id = nx_ni_id AND nx_co_id = " << jsonArr->at(0)->get( "co_id", "" ).asInt64() <<  " LIMIT 2";
        newsArr = sendQuery( 1, osSQL.str().c_str() );
    }
    long news_len = newsArr->size();
	cout << "sixth query sent" << endl;

    pOut->fin_len = fin_len;
    pOut->day_len = day_len;
    pOut->news_len = news_len;

	cout << "Starting to read data..." << endl;
    pOut->s52_wk_high = jsonArr->at(0)->get("s_52wk_high", "").asFloat();
	cout << "Read high" <<endl;
    sscanf(jsonArr->at(0)->get("s_52wk_high_date","").asCString(), "%hd-%hd-%hd",
            &pOut->s52_wk_high_date.year,
            &pOut->s52_wk_high_date.month,
            &pOut->s52_wk_high_date.day);
	cout << "read high date" << endl;
    pOut->s52_wk_low = jsonArr->at(0)->get("s_52wk_low", "").asFloat();
	cout << "read low" << endl;
    sscanf(jsonArr->at(0)->get("s_52wk_low_date", "").asCString(), "%hd-%hd-%hd",
            &pOut->s52_wk_low_date.year,
            &pOut->s52_wk_low_date.month,
            &pOut->s52_wk_low_date.day);
	cout << "block 1 done" << endl;

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
	cout << "block 2 done" << endl;

    for( unsigned j = 0; j < namesArr->size(); j++ ) {
        strncpy(pOut->cp_co_name[j], namesArr->at(j)->get("co_name", "").asCString(), cCO_NAME_len);
        strncpy(pOut->cp_in_name[j], namesArr->at(j)->get("in_name", "").asCString(), cIN_NAME_len);
        pOut->cp_co_name[j][cCO_NAME_len] = '\0';
        pOut->cp_in_name[j][cIN_NAME_len] = '\0';
    }

	cout << "block 3 done" << endl;


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

	cout << "block 4 done" << endl;

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

	cout << "block 5 done" << endl;

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

	cout << "block 6 done" << endl;

    pOut->last_open = ltArr->at(0)->get("lt_open_price","").asFloat();
    pOut->last_price = ltArr->at(0)->get("lt_price", "").asFloat();
    pOut->last_vol = ltArr->at(0)->get("lt_vol", "").asInt();
	cout << "lts done" << endl;

    for( unsigned j = 0; j < newsArr->size(); j++ ) {
	cout << "Starting loop..." << endl;
        strncpy(pOut->news[j].item, newsArr->at(j)->get("ni_item", "").asCString(), cNI_ITEM_len);
	cout << "copied ni_item" << endl;
        pOut->news[j].item[cNI_ITEM_len] = '\0';
	cout << "ni_dts: " << newsArr->at(j)->get( "ni_dts", "" ).asInt64() << endl;
	time_t t = newsArr->at(j)->get( "ni_dts", "" ).asInt64() / 1000;
	struct tm *ti = localtime( &t );
	char buff[56];
	strftime( buff, 56, "%F %T", ti );
	cout << "strftime: " << buff << endl;
        sscanf( buff, "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->news[j].dts.year,
                &pOut->news[j].dts.month,
                &pOut->news[j].dts.day,
                &pOut->news[j].dts.hour,
                &pOut->news[j].dts.minute,
                &pOut->news[j].dts.second);
	cout << "done with dts" << endl;
        strncpy(pOut->news[j].src, newsArr->at(j)->get("ni_source", "").asCString(), cNI_SOURCE_len);
        pOut->news[j].src[cNI_SOURCE_len] = '\0';
        strncpy(pOut->news[j].auth, newsArr->at(j)->get("ni_author", "").asCString(), cNI_AUTHOR_len);
        pOut->news[j].auth[cNI_AUTHOR_len] = '\0';
	cout << "done with author" << endl;
        strncpy(pOut->news[j].headline, newsArr->at(j)->get("ni_headline", "").asCString(), cNI_HEADLINE_len);
        pOut->news[j].headline[cNI_HEADLINE_len] = '\0';
        strncpy(pOut->news[j].summary, newsArr->at(j)->get("ni_summary","").asCString(), cNI_SUMMARY_len);
        pOut->news[j].summary[cNI_SUMMARY_len] = '\0';
	cout << "done with summary" << endl;

    }
	cout << "block 7 done" << endl;
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

	cout << "done!" << endl;
    //TODO: free jsonArr
    //TODO: free dmArr
    //TODO: free ltArr
    //TODO: free fiArr
    //TODO: free newsArr
}

void TxnRestDB::execute( const TTradeCleanupFrame1Input *pIn ) {
    cout << "*** Beginning Trade Cleanup TxnRestDB ***" << endl;
    ostringstream osSQL;

    osSQL << "SELECT tr_t_id FROM trade_request ";
    osSQL << "ORDER BY tr_t_id";

    cout << "Sending query..." << endl;
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    cout << "Done sending query..." << endl;
    //All of these now()'s are the same in the spec,
    //I'm not sure if it matters tho
    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "INSERT INTO trade_history ( th_t_id, th_dts, th_st_id ) VALUES ";
        osSQL << "( " << jsonArr->at(i)->get("tr_t_id","").asInt64() << ", ";
        osSQL << "now(), '" << pIn->st_submitted_id << "')";
        std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free res

        osSQL.clear();
        osSQL.str("");
        osSQL << "UPDATE trade SET t_st_id = '" << pIn->st_canceled_id << "', ";
        osSQL << "t_dts = now() WHERE t_id = " << jsonArr->at(i)->get("tr_t_id","").asInt64();
        res = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free res

        osSQL.clear();
        osSQL.str("");
        osSQL << "INSERT INTO trade_history ( th_t_id, th_dts, th_st_id ) VALUES ";
        osSQL << "( " << jsonArr->at(i)->get("tr_t_id", "").asInt64() << ", ";
        osSQL << "now(), '" << pIn->st_canceled_id << "')";
        res = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free res
    }
    //TODO: free jsonArr
    osSQL.clear();
    osSQL.str("");
    osSQL << "DELETE FROM trade_request";
    jsonArr = sendQuery( 1, osSQL.str().c_str() );
    //TODO: free jsonArr

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT t_id FROM trade WHERE t_id >= " << pIn->start_trade_id;
    osSQL << " AND t_st_id = '" << pIn->st_submitted_id << "'";
    jsonArr = sendQuery( 1, osSQL.str().c_str() );

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "UPDATE trade SET t_st_id = '" << pIn->st_canceled_id << "', ";
        osSQL << "t_dts = now() WHERE t_id = " << jsonArr->at(i)->get("t_id", "").asInt64();
        std::vector<Json::Value *> *res  = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free res

        osSQL.clear();
        osSQL.str("");
        osSQL << "INSERT INTO trade_history ( th_t_id, th_dts, th_st_id ) VALUES ";
        osSQL << "( " << jsonArr->at(i)->get("t_id", "").asInt64() << ", ";
        osSQL << "now(), '" << pIn->st_canceled_id << "')";
        res  = sendQuery( 1, osSQL.str().c_str() );
        //TODO: free res
    }
    //TODO: Free jsonArr

}

void TxnRestDB::execute( const TTradeLookupFrame1Input *pIn,
                         TTradeLookupFrame1Output *pOut ) {
	cout << "trade lookup start..." << endl;
    pOut->num_found = 0;
    for( int i = 0; i < pIn->max_trades; i++ ) {
        ostringstream osSQL;
        osSQL << "SELECT t_bid_price, t_exec_name, t_is_cash, ";
        osSQL << "tt_is_mrkt, t_trade_price FROM trade, trade_type ";
        osSQL << "WHERE t_id = " << pIn->trade_id[i] << " AND ";
        osSQL << "t_tt_id = tt_id";
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

        pOut->trade_info[i].bid_price = jsonArr->at(0)->get("t_bid_price", "").asDouble();
        strncpy( pOut->trade_info[i].exec_name, jsonArr->at(0)->get("t_exec_name", "").asCString(), cEX_NAME_len );
        pOut->trade_info[i].is_cash = jsonArr->at(0)->get("t_is_cash", "").asBool();
        pOut->trade_info[i].is_market = jsonArr->at(0)->get( "tt_is_mrkt", "").asBool();
        pOut->trade_info[i].trade_price = jsonArr->at(0)->get("t_trade_price", "").asDouble();
        pOut->num_found += jsonArr->size();

        osSQL.clear();
        osSQL.str("");

        osSQL << "SELECT se_amt, se_cash_due_date, se_cash_type ";
        osSQL << "FROM settlement WHERE se_t_id = " << pIn->trade_id[i];
        std::vector<Json::Value *> *seArr = sendQuery( 1, osSQL.str().c_str() );

        osSQL.clear();
        osSQL.str("");
        if( pOut->trade_info[i].is_cash ) {
            osSQL << "SELECT ct_amt, ct_dts, ct_name FROM cash_transaction ";
            osSQL << "WHERE ct_t_id = " << pIn->trade_id[i];
            std::vector<Json::Value *> *ctArr = sendQuery( 1, osSQL.str().c_str() );
	cout << "Frame 1 DTS sent, reading objects...";
            pOut->trade_info[i].cash_transaction_amount = ctArr->at(0)->get("ct_amt", "").asDouble();
            time_t t = ctArr->at(0)->get( "ct_dts", "" ).asInt64() / 1000;
            struct tm *ti = localtime( &t );
            char buff[56];
	    memset( buff, '\0', sizeof(buff) );
            strftime( buff, 56, "%F %T", ti );
	    cout << "Interpreted time: " << buff << endl;

            sscanf(buff, "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].cash_transaction_dts.year,
                    &pOut->trade_info[i].cash_transaction_dts.month,
                    &pOut->trade_info[i].cash_transaction_dts.day,
                    &pOut->trade_info[i].cash_transaction_dts.hour,
                    &pOut->trade_info[i].cash_transaction_dts.minute,
                    &pOut->trade_info[i].cash_transaction_dts.second);
            strncpy(pOut->trade_info[i].cash_transaction_name, ctArr->at(0)->get("ct_name", "").asCString(), cCT_NAME_len);
            pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';

            osSQL.clear();
            osSQL.str("");
            osSQL << "SELECT th_dts, th_st_id FROM trade_history ";
            osSQL << "WHERE th_t_id = " << pIn->trade_id[i] << " ";
            osSQL << "ORDER BY th_dts LIMIT 3";
            std::vector<Json::Value *> *thArr = sendQuery( 1, osSQL.str().c_str() );
            for( unsigned j = 0; j < thArr->size(); j++ ) {
                memset( buff, '\0', sizeof(buff) );
                t = thArr->at(j)->get("th_dts", "").asInt64();
                ti = localtime( &t );
                strftime( buff, 56, "%F %T", ti );
                sscanf(buff, "%hd-%hd-%hd %hd:%hd:%hd",
                        &pOut->trade_info[i].trade_history_dts[j].year,
                        &pOut->trade_info[i].trade_history_dts[j].month,
                        &pOut->trade_info[i].trade_history_dts[j].day,
                        &pOut->trade_info[i].trade_history_dts[j].hour,
                        &pOut->trade_info[i].trade_history_dts[j].minute,
                        &pOut->trade_info[i].trade_history_dts[j].second);
                strncpy(pOut->trade_info[i].trade_history_status_id[j],
                        thArr->at(j)->get("th_st_id", "").asCString(), cTH_ST_ID_len);
                pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] =
                    '\0';
            }
        }
    }
	cout << "First frame done..." << endl;
}

void TxnRestDB::execute( const TTradeLookupFrame2Input *pIn,
                         TTradeLookupFrame2Output *pOut ) {
	cout << "Second frame srtarting..." << endl;
    ostringstream osSQL;
    osSQL << "SELECT t_bid_price, t_exec_name, t_is_cash, ";
    osSQL << "t_id, t_trade_price FROM trade WHERE ";
    osSQL << "t_ca_id = " << pIn->acct_id << " AND ";
    osSQL << "t_dts >= '" << pIn->start_trade_dts.year << "-";
    osSQL << pIn->start_trade_dts.month << "-" << pIn->start_trade_dts.day;
    osSQL << " " << pIn->start_trade_dts.hour << ":" << pIn->start_trade_dts.minute;
    osSQL << ":" << pIn->start_trade_dts.second << "' AND ";
    osSQL << "t_dts <= '" << pIn->end_trade_dts.year << "-";
    osSQL << pIn->end_trade_dts.month << "-" << pIn->end_trade_dts.day;
    osSQL << " " << pIn->end_trade_dts.hour << ":" << pIn->end_trade_dts.minute;
    osSQL << ":" << pIn->end_trade_dts.second << "' ORDER BY t_dts ASC ";
    osSQL << "LIMIT " << pIn->max_trades;

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        pOut->trade_info[i].bid_price = jsonArr->at(i)->get("t_bid_price", "").asDouble();
        strncpy( pOut->trade_info[i].exec_name, jsonArr->at(i)->get("t_exec_name", "").asCString(), cEXEC_NAME_len );
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        pOut->trade_info[i].is_cash = jsonArr->at(i)->get("t_is_cash", "").asBool();
        pOut->trade_info[i].trade_id = jsonArr->at(i)->get("t_id", "").asInt64();
        pOut->trade_info[i].trade_price = jsonArr->at(i)->get("t_trade_price", "").asDouble();
    }

    pOut->num_found = jsonArr->size();
    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT se_amt, se_cash_due_date, se_cash_type ";
        osSQL << "FROM settlement WHERE se_t_id = ";
        osSQL << jsonArr->at(i)->get("t_id", "").asInt64();
        std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

        pOut->trade_info[i].settlement_amount = res->at(0)->get("se_amt", "").asDouble();
        sscanf(res->at(0)->get("se_cash_due_date", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        strncpy(pOut->trade_info[i].settlement_cash_type,
                res->at(0)->get("se_cash_type", "").asCString(),
                cSE_CASH_TYPE_len);
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';

        if( pOut->trade_info[i].is_cash ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "SELECT ct_amt, ct_dts, ct_name FROM cash_transaction ";
            osSQL << "WHERE ct_t_id = " << jsonArr->at(i)->get("t_id", "").asInt64();
            std::vector<Json::Value *> *ctArr = sendQuery(1, osSQL.str().c_str() );
		cout << "Cash transaction done!" << endl;
            pOut->trade_info[i].cash_transaction_amount = ctArr->at(0)->get("ct_amt", "").asDouble();
		cout << "Done amount" << endl;

            time_t t = ctArr->at(0)->get( "ct_dts", "" ).asInt64() / 1000;
            struct tm *ti = localtime( &t );
            char buff[56];
	    memset( buff, '\0', sizeof(buff) );
            strftime( buff, 56, "%F %T", ti );
	    cout << "Interpreted time: " << buff << endl;

            sscanf( buff, "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second);
            strncpy(pOut->trade_info[i].cash_transaction_name,
                    ctArr->at(0)->get("ct_name","").asCString(),
                cCT_NAME_len);
            pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';

        }
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT th_dts, th_st_id FROM trade_history WHERE ";
        osSQL << "th_t_id = " << jsonArr->at(0)->get("t_id", "").asInt64();
        osSQL << " ORDER BY th_dts LIMIT 3";
        std::vector<Json::Value *> *thArr = sendQuery( 1, osSQL.str().c_str() );

        for( unsigned j = 0; j < thArr->size(); j++ ) {
            time_t t = thArr->at(j)->get( "th_dts", "" ).asInt64() / 1000;
            struct tm *ti = localtime( &t );
            char buff[56];
	    memset( buff, '\0', sizeof(buff) );
            strftime( buff, 56, "%F %T", ti );
	    cout << "Interpreted time: " << buff << endl;

            sscanf( buff, "%hd-%hd-%hd %hd:%hd:%hd",
              &pOut->trade_info[i].trade_history_dts[j].year,
              &pOut->trade_info[i].trade_history_dts[j].month,
              &pOut->trade_info[i].trade_history_dts[j].day,
              &pOut->trade_info[i].trade_history_dts[j].hour,
              &pOut->trade_info[i].trade_history_dts[j].minute,
              &pOut->trade_info[i].trade_history_dts[j].second);
            strncpy( pOut->trade_info[i].trade_history_status_id[j],
                     thArr->at(j)->get("th_st_id", "").asCString(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] = '\0';

        }

    }
	cout << "Done executing frame 2!" << endl;
}

void TxnRestDB::execute( const TTradeLookupFrame3Input *pIn,
                         TTradeLookupFrame3Output *pOut ) {

	cout << "Frame 3 started..." << endl;
    ostringstream osSQL;
    osSQL << "SELECT t_ca_id, t_exec_name, t_is_cash, t_trade_price, ";
    osSQL << "t_qty, t_dts, t_id, t_tt_id FROM trade WHERE t_s_symb = '";
    osSQL << pIn->symbol << "' AND t_dts >= '";
    osSQL << pIn->start_trade_dts.year << "-" << pIn->start_trade_dts.month << "-";
    osSQL << pIn->start_trade_dts.day << " " << pIn->start_trade_dts.hour;
    osSQL << ":" << pIn->start_trade_dts.minute << ":" << pIn->start_trade_dts.second << "' AND ";
    osSQL << "t_dts <= '" << pIn->end_trade_dts.year << "-" << pIn->end_trade_dts.month << "-";
    osSQL << pIn->end_trade_dts.day << " " << pIn->end_trade_dts.hour << ":" << pIn->end_trade_dts.minute;
    osSQL << ":" << pIn->end_trade_dts.second << "' AND t_ca_id <= " << pIn->max_acct_id << " ORDER BY ";
    osSQL << "t_dts ASC LIMIT " << pIn->max_trades;
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        pOut->trade_info[i].acct_id = jsonArr->at(i)->get("t_ca_id", "").asInt64();
        strncpy( pOut->trade_info[i].exec_name, jsonArr->at(i)->get("t_exec_name", "").asCString(), cEXEC_NAME_len );
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        pOut->trade_info[i].is_cash = jsonArr->at(i)->get("t_is_cash", "").asBool();
        pOut->trade_info[i].price = jsonArr->at(i)->get("t_trade_price", "").asDouble();
        pOut->trade_info[i].quantity = jsonArr->at(i)->get("t_qty", "").asInt();

	time_t t = jsonArr->at(i)->get( "t_dts", "" ).asInt64() / 1000;
	struct tm *ti = localtime( &t );
	char buff[56];
	memset( buff, '\0', sizeof(buff) );
	strftime( buff, 56, "%F %T", ti );
	cout << "Interpreted time: " << buff << endl;

        sscanf( buff, "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_dts.year,
                &pOut->trade_info[i].trade_dts.month,
                &pOut->trade_info[i].trade_dts.day,
                &pOut->trade_info[i].trade_dts.hour,
                &pOut->trade_info[i].trade_dts.minute,
                &pOut->trade_info[i].trade_dts.second );
        pOut->trade_info[i].trade_id = jsonArr->at(i)->get("t_id", "").asInt64();
        strncpy( pOut->trade_info[i].trade_type, jsonArr->at(i)->get("t_tt_id", "").asCString(), cTT_ID_len );
        pOut->trade_info[i].trade_type[cTT_ID_len] = '\0';
    }

	cout << "Block 1 done!" << endl;

    osSQL.clear();
    osSQL.str("");

    pOut->num_found = jsonArr->size();

    for( int i = 0; i < pOut->num_found; i++ ) {
        osSQL << "SELECT se_amt, se_cash_due_date, se_cash_type FROM settlement ";   
        osSQL << "WHERE se_t_id = " << jsonArr->at(i)->get("t_id", "").asInt64();
        std::vector<Json::Value *> *seArr = sendQuery( 1, osSQL.str().c_str() );

        pOut->trade_info[i].settlement_amount = seArr->at(0)->get("se_amt", "").asDouble();
        sscanf(seArr->at(0)->get("se_cash_due_date", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        strncpy( pOut->trade_info[i].settlement_cash_type, seArr->at(0)->get("se_cash_type", "").asCString(),
                 cSE_CASH_TYPE_len );
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';

        if( jsonArr->at(i)->get("t_is_cash", "").asBool() ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "SELECT ct_amt, ct_dts, ct_name FROM cash_transaction WHERE ";
            osSQL << "ct_t_id = " << jsonArr->at(i)->get("t_id", "").asInt64();
            std::vector<Json::Value *> *ctArr = sendQuery( 1, osSQL.str().c_str() );
	    time_t t = ctArr->at(0)->get( "ct_dts", "" ).asInt64() / 1000;
	    struct tm *ti = localtime( &t );
	    char buff[56];
	    memset( buff, '\0', sizeof(buff) );
	    strftime( buff, 56, "%F %T", ti );
	    cout << "Interpreted time: " << buff << endl;

            pOut->trade_info[i].cash_transaction_amount = ctArr->at(0)->get("ct_amt", "").asDouble();
            sscanf( buff, "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second );
            strncpy( pOut->trade_info[i].cash_transaction_name, ctArr->at(0)->get("ct_name", "").asCString(),
                     cCT_NAME_len );
            pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';

        }

	cout << "Block 2 Done!" << endl;
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT th_dts, th_st_id FROM trade_history WHERE th_t_id = ";
        osSQL << jsonArr->at(i)->get("t_id", "").asInt64() << " ORDER BY ";
        osSQL << "th_dts ASC LIMIT 3";

        std::vector<Json::Value *> *thArr = sendQuery( 1, osSQL.str().c_str() );
        for( unsigned j = 0; j < thArr->size(); j++ ) {
		time_t t = thArr->at(j)->get( "th_dts", "" ).asInt64() / 1000;
		struct tm *ti = localtime( &t );
		char buff[56];
		memset( buff, '\0', sizeof(buff) );
		strftime( buff, 56, "%F %T", ti );
		cout << "Interpreted time: " << buff << endl;


            sscanf( buff, "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].trade_history_dts[j].year,
                    &pOut->trade_info[i].trade_history_dts[j].month,
                    &pOut->trade_info[i].trade_history_dts[j].day,
                    &pOut->trade_info[i].trade_history_dts[j].hour,
                    &pOut->trade_info[i].trade_history_dts[j].minute,
                    &pOut->trade_info[i].trade_history_dts[j].second);
            strncpy( pOut->trade_info[i].trade_history_status_id[j],
                     thArr->at(j)->get("th_st_id", "").asCString(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] = '\0';
        }
	osSQL.clear();
	osSQL.str("");
	cout << "Done!" << endl;
    }
}

void TxnRestDB::execute( const TTradeLookupFrame4Input *pIn,
                         TTradeLookupFrame4Output *pOut ) {

    ostringstream osSQL;

    osSQL << "SELECT t_id FROM trade WHERE t_ca_id  = ";
    osSQL << pIn->acct_id << " AND t_dts >= '";
    osSQL << pIn->trade_dts.year << "-" << pIn->trade_dts.month << "-";
    osSQL << pIn->trade_dts.day << " " << pIn->trade_dts.hour << ":";
    osSQL << ":" << pIn->trade_dts.minute << ":" << pIn->trade_dts.second << "' ";
    osSQL << "ORDER BY t_dts ASC LIMIT 1";
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->num_trades_found = jsonArr->size();
    pOut->trade_id = jsonArr->at(0)->get("t_id", "").asInt64();

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT hh_h_t_id, hh_t_id, hh_before_qty, hh_after_qty ";
    osSQL << "FROM holding_history WHERE hh_h_t_id IN ( SELECT hh_h_t_id ";
    osSQL << "FROM holding_history WHERE hh_t_id = " << pOut->trade_id;
    osSQL << ") LIMIT 20";
    std::vector<Json::Value *> *hhArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->num_found = hhArr->size();

    for( unsigned i = 0; i < hhArr->size(); i++ ) {
        pOut->trade_info[i].holding_history_id = hhArr->at(i)->get("hh_h_t_id", "").asInt64();
        pOut->trade_info[i].holding_history_trade_id = hhArr->at(i)->get("hh_t_id", "").asInt64();
        pOut->trade_info[i].quantity_before = hhArr->at(i)->get("hh_before_qty", "").asDouble();
        pOut->trade_info[i].quantity_after = hhArr->at(i)->get("hh_before_qty", "").asDouble();
    }

}

void TxnRestDB::execute( const TTradeOrderFrame1Input *pIn,
                         TTradeOrderFrame1Output *pOut ) {
	cout << "Starting trade order" << endl;
    ostringstream osSQL;
    osSQL << "SELECT ca_name, ca_b_id, ca_c_id, ca_tax_st FROM ";
    osSQL << "customer_account WHERE ca_id = " << pIn->acct_id;

    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->num_found = jsonArr->size();
    cout << "Number of existing trades: " << jsonArr->size();
    strncpy(pOut->acct_name, jsonArr->at(0)->get("ca_name", "").asCString(), cCA_NAME_len);
    pOut->acct_name[cCA_NAME_len] ='\0';
    pOut->broker_id = jsonArr->at(0)->get("ca_b_id", "").asInt64();
    pOut->cust_id = jsonArr->at(0)->get("ca_c_id", "").asInt64();
    pOut->tax_status = jsonArr->at(0)->get("ca_tax_st", "").asInt();
    cout << "acct_name: " << pOut->acct_name << ", broker_id: " << pOut->broker_id;

    osSQL.clear();
    osSQL.str("");

    osSQL << "SELECT c_f_name, c_l_name, c_tier, c_tax_id FROM ";
    osSQL << "customer WHERE c_id = " << jsonArr->at(0)->get("ca_c_id", "").asInt64();
    std::vector<Json::Value *> *cArr = sendQuery( 1, osSQL.str().c_str() );
    strncpy(pOut->cust_f_name, cArr->at(0)->get("c_f_name", "").asCString(), cF_NAME_len);
    pOut->cust_f_name[cF_NAME_len] = '\0';
    strncpy(pOut->cust_l_name, cArr->at(0)->get("c_l_name", "").asCString(), cL_NAME_len);
    pOut->cust_l_name[cL_NAME_len] = '\0';
    pOut->cust_tier = cArr->at(0)->get("c_tier", "").asInt64();
    strncpy(pOut->tax_id, cArr->at(0)->get("tax_id", "").asCString(), cTAX_ID_len);
    pOut->tax_id[cTAX_ID_len] = '\0';

    osSQL.clear();
    osSQL.str("");

    osSQL << "SELECT b_name FROM broker WHERE b_id = " << jsonArr->at(0)->get("ca_b_id", "").asInt64();
    std::vector<Json::Value *> *bArr = sendQuery( 1, osSQL.str().c_str() );
    strncpy(pOut->broker_name, bArr->at(0)->get("b_name", "").asCString(), cB_NAME_len);
    pOut->broker_name[cB_NAME_len]  ='\0';
	cout << "Done block one" << endl;
}

void TxnRestDB::execute( const TTradeOrderFrame2Input *pIn,
                         TTradeOrderFrame2Output *pOut ) {
	cout << "Starting TO frame 2" << endl;
    ostringstream osSQL;
    char *tmpstr;
    tmpstr = escape(pIn->exec_f_name);

    osSQL << "SELECT ap_acl FROM account_permission WHERE ";
    osSQL << "ap_ca_id = " << pIn->acct_id << " AND ap_f_name = '";
    osSQL << tmpstr << "' AND ap_l_name = '";
    free( tmpstr );
    tmpstr = escape(pIn->exec_l_name);
    osSQL << tmpstr << "' AND ap_tax_id = '" << pIn->exec_tax_id << "'";
    free( tmpstr );
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    //0,0 should be i_bid_price
    //test for null
    if ( strncmp("null", jsonArr->at(0)->get("ap_acl", "").asCString(), 4 ) != 0 ) {
        strncpy(pOut->ap_acl, jsonArr->at(0)->get("ap_acl", "").asCString(), cACL_len);
        pOut->ap_acl[cACL_len] = '\0';
    } else {
        pOut->ap_acl[0] = '\0';
    }
	cout << "Done TO block 2" << endl;
}

void TxnRestDB::execute( const TTradeOrderFrame3Input *pIn,
                         TTradeOrderFrame3Output *pOut ) {
	cout << "Starting TO block 3" << endl;
    ostringstream osSQL;
    char *tmpstr;
    char ex_id[10];
    tmpstr = escape(pIn->co_name);
    long co_id;

    if( strlen(pIn->symbol) == 0 ) {
        osSQL << "SELECT co_id FROM company WHERE co_name = '";
        osSQL << tmpstr << "'";
        free(tmpstr);
        std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
        co_id = jsonArr->at(0)->get("co_id", "").asInt64();

        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT s_ex_id, s_name, s_symb FROM security ";
        osSQL << "WHERE s_co_id = " << co_id << " AND ";
        osSQL << "s_issue = '" << pIn->issue << "'";
        std::vector<Json::Value *> *sArr = sendQuery( 1, osSQL.str().c_str() );
        strncpy( ex_id, sArr->at(0)->get("s_ex_id", "").asCString(), cEX_ID_len );
        ex_id[cEX_ID_len] = '\0';
        strncpy( pOut->s_name, sArr->at(0)->get("s_name", "").asCString(), cS_NAME_len );
        pOut->s_name[cS_NAME_len] = '\0';
        strncpy( pOut->symbol, sArr->at(0)->get("s_symb", "").asCString(), cSYMBOL_len );
        pOut->symbol[cSYMBOL_len] = '\0';
    } else {
        osSQL << "SELECT s_co_id, s_ex_id, s_name FROM security WHERE ";
        osSQL << "s_symb = '" << pIn->symbol << "'";
        std::vector<Json::Value *> *sArr = sendQuery( 1, osSQL.str().c_str() );
        co_id = sArr->at(0)->get("s_co_id", "").asInt64();
        strncpy( ex_id, sArr->at(0)->get("s_ex_id", "").asCString(), cEX_ID_len );
        ex_id[cEX_ID_len] = '\0';
        strncpy( pOut->s_name, sArr->at(0)->get("s_name", "").asCString(), cS_NAME_len );
        pOut->s_name[cS_NAME_len] = '\0';
        strncpy( pOut->symbol, pIn->symbol, cSYMBOL_len );
        pOut->symbol[cSYMBOL_len] = '\0';

        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT co_name FROM company WHERE co_id = ";
        osSQL << co_id;
        std::vector<Json::Value *> *coArr = sendQuery( 1, osSQL.str().c_str() );
        strncpy( pOut->co_name, coArr->at(0)->get("co_name", "").asCString(), cCO_NAME_len );
        pOut->co_name[cCO_NAME_len] = '\0';
    } //symbol

	cout << "Done first block" << endl;
    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT lt_price FROM last_trade WHERE lt_s_symb = '";
    osSQL << pOut->symbol << "'";
    std::vector<Json::Value *> *ltArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->market_price = ltArr->at(0)->get("lt_price", "").asDouble();

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT tt_is_mrkt, tt_is_sell FROM trade_type ";
    osSQL << "WHERE tt_id = '" << pIn->trade_type_id << "'";
    std::vector<Json::Value *> *ttArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->type_is_market = ttArr->at(0)->get("tt_is_mrkt", "").asBool();
    pOut->type_is_sell = ttArr->at(0)->get("tt_is_sell", "").asBool();

    if( pOut->type_is_market ) {
        pOut->requested_price = pOut->market_price;
    }

    pOut->buy_value = 0;
    pOut->sell_value = 0;
    long needed_qty = pIn->trade_qty;
    long hs_qty = 0;


		osSQL << "Done block" << endl;
    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT hs_qty FROM holding_summary WHERE ";
    osSQL << "hs_ca_id = " << pIn->acct_id << " AND hs_s_symb = '";
    osSQL << pOut->symbol << "'";
    std::vector<Json::Value *> *hsArr = sendQuery( 1, osSQL.str().c_str() );

    if( hsArr->size() > 0 ) {
        hs_qty = hsArr->at(0)->get("hs_qty", "").asInt64();
    }

    if( pOut->type_is_sell ) { //is_sell
        if( hs_qty > 0 ) {
            std::vector<Json::Value *> *hold_list;
            if( pIn->is_lifo ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "SELECT h_qty, h_price FROM holding WHERE ";
                osSQL << "h_ca_id = " << pIn->acct_id << " AND h_s_symb = '";
                osSQL << pOut->symbol << "' ORDER BY h_dts DESC";
                hold_list = sendQuery( 1, osSQL.str().c_str() );
            } else {
                osSQL.clear();
                osSQL.str("");
                osSQL << "SELECT h_qty, h_price FROM holding WHERE ";
                osSQL << "h_ca_id = " << pIn->acct_id << " AND h_s_symb = '";
                osSQL << pOut->symbol << "' ORDER BY h_dts ASC";
                hold_list = sendQuery( 1, osSQL.str().c_str() );
            }
            for( unsigned i = 0; i < hold_list->size(); i++ ) {
                if( needed_qty == 0 ) {
                    break;
                }
                long hold_qty = hold_list->at(i)->get("h_qty", "").asInt64();
                long hold_price = hold_list->at(i)->get("h_price", "").asDouble();
                if( hold_qty > needed_qty ) {
                    pOut->buy_value += needed_qty * hold_price;
                    pOut->sell_value += needed_qty * pOut->requested_price;
                    needed_qty = 0;
                } else {
                    pOut->buy_value += hold_qty * hold_price;
                    pOut->sell_value += hold_qty * pOut->requested_price;
                    needed_qty = needed_qty - hold_qty;
                }
            }
        } //hs_qty
    }  else { //is_sell
        if( hs_qty < 0 ) {
            std::vector<Json::Value *> *hold_list;
            if( pIn->is_lifo ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "SELECT h_qty, h_price FROM holding WHERE ";
                osSQL << "h_ca_id = " << pIn->acct_id << " AND h_s_symb = '";
                osSQL << pOut->symbol << "' ORDER BY h_dts DESC";
                hold_list = sendQuery( 1, osSQL.str().c_str() );
            } else {
                osSQL.clear();
                osSQL.str("");
                osSQL << "SELECT h_qty, h_price FROM holding WHERE ";
                osSQL << "h_ca_id = " << pIn->acct_id << " AND h_s_symb = '";
                osSQL << pOut->symbol << "' ORDER BY h_dts ASC";
                hold_list = sendQuery( 1, osSQL.str().c_str() );
            }
            for( unsigned i = 0; i < hold_list->size(); i++ ) {
                if( needed_qty == 0 ) {
                    break;
                }
                long hold_qty = hold_list->at(i)->get("h_qty", "").asInt64();
                long hold_price = hold_list->at(i)->get("h_price", "").asDouble();
                if( hold_qty + needed_qty < 0 ) {
                    pOut->sell_value += needed_qty * hold_price;
                    pOut->buy_value += needed_qty * pOut->requested_price;
                    needed_qty = 0;
                } else {
                    hold_qty = -hold_qty;
                    pOut->sell_value += hold_qty * hold_price;
                    pOut->buy_value += hold_qty * pOut->requested_price;
                    needed_qty = needed_qty - hold_qty;
                }
            }
        } //hs_qty
    } //is_buy

    pOut->tax_amount = 0;
    if( (pOut->sell_value > pOut->buy_value) &&
            ( (pIn->tax_status == 1) || (pIn->tax_status == 2 ) ) ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT sum(tx_rate) as tx FROM taxrate WHERE tx_id in ( ";
        osSQL << "SELECT cx_tx_id FROM customer_taxrate WHERE cx_c_id = ";
        osSQL << pIn->cust_id << ")";
        std::vector<Json::Value *> *tArr = sendQuery( 1, osSQL.str().c_str() );
        pOut->tax_amount = (pOut->sell_value - pOut->buy_value) * tArr->at(0)->get("tx", "").asDouble();
    }
    osSQL << "Done second last block" << endl;

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT cr_rate FROM commission_rate WHERE cr_c_tier = ";
    osSQL << pIn->cust_tier << " AND cr_tt_id = '" << pIn->trade_type_id;
    osSQL << "' AND cr_ex_id = '" << ex_id << "' AND cr_from_qty <= ";
    osSQL << pIn->trade_qty << " AND cr_to_qty >= " << pIn->trade_qty;
    std::vector<Json::Value *> *crArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->comm_rate = crArr->at(0)->get("cr_rate", "").asDouble();

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT ch_chrg FROM charge WHERE ch_c_tier = " << pIn->cust_tier << " ";
    osSQL << "AND ch_tt_id = '" << pIn->trade_type_id << "'";
    std::vector<Json::Value *> *chArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->charge_amount = chArr->at(0)->get("ch_chrg", "").asDouble();

    if( pIn->type_is_margin ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT ca_bal FROM customer_account WHERE ca_id = " << pIn->acct_id;
        std::vector<Json::Value *> *caArr = sendQuery( 1, osSQL.str().c_str() );

        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT sum(hs_qty * lt_price) as hold_assets FROM holding_summary, last_trade ";
        osSQL << "WHERE hs_ca_id = " << pIn->acct_id << " AND lt_s_symb = hs_s_symb";
        std::vector<Json::Value *> *hltArr = sendQuery( 1, osSQL.str().c_str() );

        if( hltArr->size() > 0 ) {
            pOut->acct_assets = hltArr->at(0)->get("hold_assets", "").asDouble() +
                caArr->at(0)->get("ca_bal", "").asDouble();
        } else {
            pOut->acct_assets = caArr->at(0)->get("ca_bal", "").asDouble();
        }
    }
    cout << "Going to set status_id" << endl;

    if( pOut->type_is_market ) {
        strncpy(pOut->status_id, pIn->st_submitted_id, cTH_ST_ID_len);
        pOut->status_id[cTH_ST_ID_len] = '\0';
    } else {
        strncpy(pOut->status_id, pIn->st_pending_id, cTH_ST_ID_len);
        pOut->status_id[cTH_ST_ID_len] = '\0';
    }
    cout << "Set status_id: " << pOut->status_id << endl;
    cout << "Done TO frame 3" << endl;
}

void TxnRestDB::execute( const TTradeOrderFrame4Input *pIn,
                         TTradeOrderFrame4Output *pOut) {
	cout << "Starting TO frame 4" << endl;
	cout << "Frame 4 status_id: " << pIn->status_id << endl;
    ostringstream osSQL;
    char *tmpstr;
    tmpstr = escape( pIn->exec_name );

    //N.B. Unlike PGSQL, MySQL does not have sequences. This table uses auto_increment on t_id
    osSQL << "INSERT INTO trade ( t_dts, t_st_id, t_tt_id, t_is_cash, ";
    osSQL << "t_s_symb, t_qty, t_bid_price, t_ca_id, t_exec_name, t_trade_price, ";
    osSQL << "t_chrg, t_comm, t_tax, t_lifo ) VALUES ( now(), '";
    osSQL << pIn->status_id << "', '" << pIn->trade_type_id << "', " << pIn->is_cash;
    osSQL << ", '" << pIn->symbol << "', " << pIn->trade_qty << ", " << pIn->requested_price;
    osSQL << ", " << pIn->acct_id << ", '" << tmpstr << "', NULL, " << pIn->charge_amount << ", " << pIn->comm_amount;
    osSQL << ", 0, " << pIn->is_lifo << ")";
    free( tmpstr );
    std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

    //Retrieve the written t_id
    //N.B. There is technically a race condition here, but we assume these conditions are
    //enough to dodge most of them
    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT t_id FROM trade WHERE t_st_id = '" << pIn->status_id << "' AND ";
    osSQL << "t_tt_id = '" << pIn->trade_type_id << "' AND t_is_cash = " << pIn->is_cash;
    osSQL << " AND t_chrg = " << pIn->charge_amount << " AND t_comm = " << pIn->comm_amount;
    osSQL << " ORDER BY t_dts DESC";
    std::vector<Json::Value *> *tArr = sendQuery( 1, osSQL.str().c_str() );
    long trade_id = tArr->at(0)->get("t_id", "").asInt64();
	cout << "Half way through frame 4" << endl;

    if( !pIn->type_is_market ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "INSERT INTO trade_request ( tr_t_id, tr_tt_id, tr_s_symb, tr_qty, ";
        osSQL << "tr_bid_price, tr_b_id ) VALUES ( " << trade_id << ", '" << pIn->trade_type_id << "', '";
        osSQL << pIn->symbol << "', " << pIn->trade_qty << ", " << pIn->requested_price << ", " << pIn->broker_id << ")";
        res = sendQuery( 1, osSQL.str().c_str() );
    }

    osSQL.clear();
    osSQL.str("");
    osSQL << "INSERT INTO trade_history ( th_t_id, th_dts, th_st_id ) VALUES ( ";
    osSQL << trade_id << ", now(), '" << pIn->status_id << "')";
    res = sendQuery( 1, osSQL.str().c_str() );
    pOut->trade_id = trade_id;
	cout << "Done TO frame 4" << endl;
}

void TxnRestDB::execute( const TTradeResultFrame1Input *pIn,
                         TTradeResultFrame1Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT t_ca_id, t_tt_id, t_s_symb, t_qty, t_chrg, t_lifo, t_is_cash ";
    osSQL << "FROM trade WHERE t_id = " << pIn->trade_id;
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->acct_id = jsonArr->at(0)->get( "t_ca_id" , "" ).asInt64();
    strncpy(pOut->type_id, jsonArr->at(0)->get("t_tt_id", "").asCString(), cTT_ID_len );
    pOut->type_id[cTT_ID_len] = '\0';
    strncpy(pOut->symbol, jsonArr->at(0)->get("t_s_symb", "").asCString(), cSYMBOL_len );
    pOut->symbol[cSYMBOL_len] = '\0';
    pOut->trade_qty = jsonArr->at(0)->get( "t_qty", "" ).asInt();
    pOut->charge = jsonArr->at(0)->get( "t_chrg", "" ).asDouble();
    pOut->is_lifo = jsonArr->at(0)->get( "t_lifo", "" ).asInt();
    pOut->trade_is_cash = jsonArr->at(0)->get( "t_is_cash", "").asInt();

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT tt_name, tt_is_sell, tt_is_mrkt FROM trade_type WHERE ";
    osSQL << "tt_id = '" << jsonArr->at(0)->get("t_tt_id", "").asString() << "'";
    std::vector<Json::Value *> *ttArr = sendQuery( 1, osSQL.str().c_str() );
    strncpy(pOut->type_name, ttArr->at(0)->get("tt_name", "").asCString(), cTT_NAME_len);
    pOut->type_name[cTT_NAME_len] = '\0';
    pOut->type_is_market = ttArr->at(0)->get( "tt_is_mrkt", "" ).asInt();
    pOut->type_is_sell = ttArr->at(0)->get("tt_is_sell", "").asInt();

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT hs_qty FROM holding_summary WHERE hs_ca_id = ";
    osSQL << pOut->acct_id << " AND hs_s_symb = '" << pOut->symbol << "'";
    std::vector<Json::Value *> *hsArr = sendQuery( 1, osSQL.str().c_str() );

    if( hsArr->size() == 0 ) {
        pOut->hs_qty = 0;
    }
    pOut->hs_qty = hsArr->at(0)->get("hs_qty", "").asInt();
}

void TxnRestDB::execute( const TTradeResultFrame2Input *pIn,
                             TTradeResultFrame2Output *pOut ) {
    pOut->buy_value = 0;
    pOut->sell_value = 0;
    int needed_qty = pIn->trade_qty;
    ostringstream osSQL;

    osSQL << "SELECT ca_b_id, ca_c_id, ca_tax_st FROM customer_account ";
    osSQL << "WHERE ca_id = " << pIn->acct_id;
    std::vector<Json::Value *> *caArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->broker_id = caArr->at(0)->get("ca_b_id", "").asInt64();
    pOut->cust_id = caArr->at(0)->get("ca_c_id", "").asInt64();
    pOut->tax_status = caArr->at(0)->get("tax_status", "").asInt64();

    if( pIn->type_is_sell ) {
        if( pIn->hs_qty == 0 ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "INSERT INTO holding_summary ( hs_ca_id, hs_s_symb ";
            osSQL << "hs_qty ) VALUES ( " << pIn->acct_id << ", '";
            osSQL << pIn->symbol << "', -" << pIn->trade_qty << ")";
            std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
        } else {
            if( pIn->hs_qty != pIn->trade_qty ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "UPDATE holding_summary SET hs_qty = " << (pIn->hs_qty - pIn->trade_qty);
                osSQL << " WHERE hs_ca_id = " << pIn->acct_id << " AND hs_s_symb = '";
                osSQL << pIn->symbol << "'";
                std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
            }
        }
        if( pIn->hs_qty > 0 ) {
            std::vector<Json::Value *> *hArr;
            if( pIn->is_lifo ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "SELECT h_t_id, h_qty, h_price FROM holding ";
                osSQL << "WHERE h_ca_id = " << pIn->acct_id << " AND hs_s_symb = '";
                osSQL << pIn->symbol << "' ORDER BY h_dts DESC";
                hArr = sendQuery( 1, osSQL.str().c_str() );
            } else { //is_lifo
                osSQL.clear();
                osSQL.str("");
                osSQL << "SELECT h_t_id, h_qty, h_price FROM holding ";
                osSQL << "WHERE h_ca_id = " << pIn->acct_id << " AND hs_s_symb = '";
                osSQL << pIn->symbol << "' ORDER BY h_dts ASC";
                hArr = sendQuery( 1, osSQL.str().c_str() );
            } //!is_lifo
            for( unsigned i = 0; i < hArr->size(); i++ ) {
                if( needed_qty == 0 ) {
                    break;
                } //break
                long hold_id = hArr->at(i)->get("h_t_id", "").asInt64();
                long hold_qty = hArr->at(i)->get("h_qty", "").asInt();
                double hold_price = hArr->at(i)->get("h_price", "").asDouble();
                if( hold_qty > needed_qty ) {
                    osSQL.clear();
                    osSQL.str("");
                    osSQL << "INSERT INTO holding_history ( hh_h_t_id, hh_t_id, ";
                    osSQL << "hh_before_qty, hh_after_qty ) VALUES ( " << hold_id << ", ";
                    osSQL << pIn->trade_id << ", " << hold_qty << ", " << (hold_qty - needed_qty) << ")";
                    std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

                    osSQL.clear();
                    osSQL.str("");
                    osSQL << "UPDATE holding SET h_qty = " << hold_qty - needed_qty << " WHERE ";
                    osSQL << "h_t_id = " << hArr->at(i)->get("h_t_id", "").asInt64() << " AND h_ca_id = ";
                    osSQL << pIn->acct_id << " AND hs_s_symb ='" << pIn->symbol << "'";
                    res = sendQuery( 1, osSQL.str().c_str() );

                    pOut->buy_value += needed_qty * hold_price;
                    pOut->sell_value += needed_qty * pIn->trade_price;
                    needed_qty = 0;
                } else { //hold_qty > needed_qty
                    osSQL.clear();
                    osSQL.str("");
                    osSQL << "INSERT INTO holding_history ( hh_h_t_id, hh_t_id, ";
                    osSQL << "hh_before_qty, hh_after_qty ) VALUES ( " << hold_id << ", ";
                    osSQL << pIn->trade_id << ", " << hold_qty << ", 0 )";
                    std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

                    osSQL.clear();
                    osSQL.str("");
                    osSQL << "DELETE FROM holding WHERE ";
                    osSQL << "h_t_id = " << hArr->at(i)->get("h_t_id", "").asInt64() << " AND h_ca_id = ";
                    osSQL << pIn->acct_id << " AND hs_s_symb ='" << pIn->symbol << "'";
                    res = sendQuery( 1, osSQL.str().c_str() );
                    pOut->buy_value += hold_qty * hold_price;
                    pOut->sell_value += hold_qty * pIn->trade_price;
                    needed_qty = needed_qty - hold_qty;
                } //hold_qty <= needed_qty
            } //hArr for loop
        } //hs_qty > 0
        if( needed_qty > 0 ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "INSERT INTO holding_history ( hh_h_t_id, hh_t_id, hh_before_qty, ";
            osSQL << "hh_after_qty ) VALUES ( " << pIn->trade_id << ", " << pIn->trade_id << ", ";
            osSQL << "0, -" << needed_qty << ")";
            std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

            osSQL.clear();
            osSQL.str("");
            osSQL << "INSERT INTO holding ( h_t_id, h_ca_id, h_s_symb, h_dts, h_price, h_qty ) ";
            osSQL << "VALUES ( " << pIn->trade_id << ", " << pIn->acct_id << ", '";
            osSQL << pIn->symbol << "', now(), " << pIn->trade_price << ", -" << pIn->trade_qty << ")";
            res = sendQuery( 1, osSQL.str().c_str() );
        } else { //needed_qty > 0
            if( pIn->hs_qty == pIn->trade_qty ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "DELETE FROM holding_summary WHERE hs_ca_id = " << pIn->acct_id << " AND ";
                osSQL << "hs_s_symb = '" << pIn->symbol << "'";
                std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
            } //if
        } //needed_qty <= 0
    } else { //type_is_sell
        //BUY
        if( pIn->hs_qty == 0 ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "INSERT INTO holding_summary( hs_ca_id, hs_s_symb, hs_qty ) VALUES (";
            osSQL << pIn->acct_id << ", '" << pIn->symbol << "', " << pIn->trade_qty << ")";
            std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
        } else { //hs_qty == 0
            if( -pIn->hs_qty != pIn->trade_qty ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "UPDATE holding_summary SET hs_qty = " << (pIn->hs_qty + pIn->trade_qty) << ", ";
                osSQL << "WHERE hs_ca_id = " << pIn->acct_id << " AND hs_s_symb = '" << pIn->symbol << "'";
                std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
            } //if

            //Short cover
            if( pIn->hs_qty < 0 ) {
                std::vector<Json::Value *> *hArr;
                if( pIn->is_lifo ) {
                    osSQL.clear();
                    osSQL.str("");
                    osSQL << "SELECT h_t_id, h_qty, h_price FROM holding ";
                    osSQL << "WHERE h_ca_id = " << pIn->acct_id << " AND hs_s_symb = '";
                    osSQL << pIn->symbol << "' ORDER BY h_dts DESC";
                    hArr = sendQuery( 1, osSQL.str().c_str() );
                } else { //is_lifo
                    osSQL.clear();
                    osSQL.str("");
                    osSQL << "SELECT h_t_id, h_qty, h_price FROM holding ";
                    osSQL << "WHERE h_ca_id = " << pIn->acct_id << " AND hs_s_symb = '";
                    osSQL << pIn->symbol << "' ORDER BY h_dts ASC";
                    hArr = sendQuery( 1, osSQL.str().c_str() );
                } //!is_lifo
                for( unsigned i = 0; i < hArr->size(); i++ ) {
                    if( needed_qty == 0 ) {
                        break;
                    } //break
                    long hold_id = hArr->at(i)->get("h_t_id", "").asInt64();
                    long hold_qty = hArr->at(i)->get("h_qty", "").asInt64();
                    double hold_price = hArr->at(i)->get("h_price", "").asDouble();
                    if( hold_qty + needed_qty < 0 ) {
                        osSQL.clear();
                        osSQL.str("");
                        osSQL << "INSERT INTO holding_history (hh_h_t_id, hh_t_id, ";
                        osSQL << "hh_before_qty, hh_after_qty) VALUES ( " << hold_id << ", ";
                        osSQL << pIn->trade_id << ", " << hold_qty << ", " << (hold_qty + needed_qty) << ")";
                        std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

                        osSQL.clear();
                        osSQL.str("");
                        osSQL << "UPDATE holding SET h_qty = " << hold_qty + needed_qty << " WHERE ";
                        osSQL << "h_t_id = " << hArr->at(i)->get("h_t_id", "").asInt64() << " AND h_ca_id = ";
                        osSQL << pIn->acct_id << " AND hs_s_symb ='" << pIn->symbol << "'";
                        res = sendQuery( 1, osSQL.str().c_str() );

                        pOut->sell_value += needed_qty * hold_price;
                        pOut->buy_value += needed_qty * pIn->trade_price;
                        needed_qty = 0;
                    } else { //hold_qty + needed_qty < 0
                        osSQL.clear();
                        osSQL.str("");
                        osSQL << "INSERT INTO holding_history (hh_h_t_id, hh_t_id, ";
                        osSQL << "hh_before_qty, hh_after_qty) VALUES ( " << hold_id << ", ";
                        osSQL << pIn->trade_id << ", " << hold_qty << ", " << 0 << ")";
                        std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

                        osSQL.clear();
                        osSQL.str("");
                        osSQL << "DELETE FROM holding WHERE ";
                        osSQL << "h_t_id = " << hArr->at(i)->get("h_t_id", "").asInt64() << " AND h_ca_id = ";
                        osSQL << pIn->acct_id << " AND hs_s_symb ='" << pIn->symbol << "'";
                        res = sendQuery( 1, osSQL.str().c_str() );

                        hold_qty = -hold_qty;
                        pOut->sell_value += hold_qty * hold_price;
                        pOut->buy_value += hold_qty * pIn->trade_price;
                        needed_qty = needed_qty - hold_qty;
                    } //else
                } //for
            } //short cover

            if( needed_qty > 0 ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "INSERT INTO holding_history (hh_h_t_id, hh_t_id, ";
                osSQL << "hh_before_qty, hh_after_qty) VALUES ( " << pIn->trade_id << ", ";
                osSQL << pIn->trade_id << ", 0, " << needed_qty << ")";
                std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

                osSQL.clear();
                osSQL.str("");
                osSQL << "INSERT INTO holding ( h_t_id, h_ca_id, h_s_symb, h_dts, h_price, h_qty ) VALUES (";
                osSQL << pIn->trade_id << ", " << pIn->acct_id << ", '" << pIn->symbol << "', now(), ";
                osSQL << pIn->trade_price << ", " << needed_qty << ")";
                res = sendQuery( 1, osSQL.str().c_str() );
            } else {
                if( -pIn->hs_qty == pIn->trade_qty ) {
                    osSQL.clear();
                    osSQL.str("");
                    osSQL << "DELETE FROM holding_summary WHERE hs_ca_id = " << pIn->acct_id << ", hs_s_symb = '";
                    osSQL << pIn->symbol << "'";
                    std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
                } //if
            } //else
        } //hs_qty != 0
    } //hs_qty <= 0
}

void TxnRestDB::execute( const TTradeResultFrame3Input *pIn,
                         TTradeResultFrame3Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT sum(tx_rate) as tx FROM taxrate WHERE tx_id IN ( ";
    osSQL << "SELECT cx_tx_id FROM customer_taxrate WHERE cx_c_id = " << pIn->cust_id << " )";
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->tax_amount = jsonArr->at(0)->get("tx", "").asDouble() * (pIn->sell_value - pIn->buy_value);

    osSQL.clear();
    osSQL.str("");
    osSQL << "UPDATE trade SET t_tax = " << pOut->tax_amount << " WHERE t_id = " << pIn->trade_id;
    std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
}

void TxnRestDB::execute( const TTradeResultFrame4Input *pIn,
                         TTradeResultFrame4Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT s_ex_id, s_name FROM security WHERE s_symb = '" << pIn->symbol;
    osSQL << "'";
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    strncpy(pOut->s_name, jsonArr->at(0)->get("s_name", "").asCString(), cS_NAME_len);
    pOut->s_name[cS_NAME_len] = '\0';

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT c_tier FROM customer WHERE c_id = " << pIn->cust_id;
    std::vector<Json::Value *> *cArr = sendQuery( 1, osSQL.str().c_str() );

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT cr_rate FROM commission_rate WHERE cr_c_tier = ";
    osSQL << cArr->at(0)->get("c_tier", "").asInt() << "AND  cr_tt_id = '";
    osSQL << pIn->symbol << "' AND cr_ex_id = '" << jsonArr->at(0)->get("s_ex_id", "").asString();
    osSQL << "' AND cr_from_qty <= " << pIn->trade_qty << " AND cr_to_qty >= " << pIn->trade_qty;
    osSQL << " LIMIT 1";
    std::vector<Json::Value *> *crArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->comm_rate = crArr->at(0)->get("cr_rate", "").asDouble();
}

void TxnRestDB::execute( const TTradeResultFrame5Input *pIn ) {
    ostringstream osSQL;
    osSQL << "UPDATE trade SET t_comm = " << pIn->comm_amount << ", ";
    osSQL << "t_dts = '" << pIn->trade_dts.year << "-" << pIn->trade_dts.month;
    osSQL << "-" << pIn->trade_dts.day << " " << pIn->trade_dts.hour << ":";
    osSQL << pIn->trade_dts.minute << ":" << pIn->trade_dts.second << "', t_st_id = '";
    osSQL << pIn->st_completed_id << "', t_trade_price = " << pIn->trade_price << " WHERE ";
    osSQL << "t_id = " << pIn->trade_id;
    std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

    osSQL.clear();
    osSQL.str("");
    osSQL << "INSERT INTO trade_history ( th_t_id, th_dts, th_st_id ) VALUES ( ";
    osSQL << pIn->trade_id << ", '" << pIn->trade_dts.year << "-" << pIn->trade_dts.month;
    osSQL << "-" << pIn->trade_dts.day << " " << pIn->trade_dts.hour << ":";
    osSQL << pIn->trade_dts.minute << ":" << pIn->trade_dts.second << "', '";
    osSQL << pIn->st_completed_id << "')";
    res = sendQuery( 1, osSQL.str().c_str() );

    osSQL.clear();
    osSQL.str("");
    osSQL << "UPDATE broker SET b_comm_total = b_comm_total + " << pIn->comm_amount << ", ";
    osSQL << "b_num_trades = b_num_trades + 1 WHERE b_id = " << pIn->broker_id;
    res = sendQuery( 1, osSQL.str().c_str() );
}

void TxnRestDB::execute( const TTradeResultFrame6Input *pIn,
                         TTradeResultFrame6Output *pOut ) {
    ostringstream osSQL;
    char cash_type[40];
    if( pIn->trade_is_cash ) {
        strcpy(cash_type, "Cash Account");
    } else {
        strcpy(cash_type, "Margin");
    }
    osSQL << "INSERT INTO settlement( se_t_id, se_cash_type, se_cash_due_date, se_amt ) VALUES ( ";
    osSQL << pIn->trade_id << ", '" << cash_type << "', '" << pIn->due_date.year << "-";
    osSQL << pIn->due_date.month << "-" << pIn->due_date.day << " " << pIn->due_date.hour << ":";
    osSQL << pIn->due_date.minute << ":" << pIn->due_date.second << "', " << pIn->se_amount;
    std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );

    if( pIn->trade_is_cash ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "UPDATE customer_account SET ca_bal = ca_bal + " << pIn->se_amount << " ";
        osSQL << "WHERE ca_id = " << pIn->acct_id;
        res = sendQuery( 1, osSQL.str().c_str() );

        osSQL.clear();
        osSQL.str("");
        char *tmpstr;
        tmpstr = escape( pIn->s_name );
        osSQL << "INSERT INTO cash_transaction ( ct_dts, ct_t_id, ct_amt, ct_name ) VALUES ( '";
        osSQL << pIn->trade_dts.year << "-" << pIn->trade_dts.month << "-" << pIn->trade_dts.day << " ";
        osSQL << pIn->trade_dts.hour << ":" << pIn->trade_dts.minute << ":" << pIn->trade_dts.second << "', ";
        osSQL << pIn->trade_id << ", " << pIn->se_amount << ", '" << pIn->type_name << " " + pIn->trade_qty;
        osSQL << " shares of " << + tmpstr << "' )";
        res = sendQuery( 1, osSQL.str().c_str() );
        free( tmpstr );
    }

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT ca_bal FROM customer_account WHERE ca_id = " << pIn->acct_id;
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );
    pOut->acct_bal = jsonArr->at(0)->get("ca_bal", "").asDouble();
}

void TxnRestDB::execute( const TTradeStatusFrame1Input *pIn,
                         TTradeStatusFrame1Output *pOut ) {
	cout << "Trade_Status frame 1 starting..." << endl;
    ostringstream osSQL;
    osSQL << "SELECT t_id, t_dts, st_name, tt_name, t_s_symb, t_qty, t_exec_name, t_chrg, ";
    osSQL << "s_name, ex_name FROM trade, status_type, trade_type, security, exchange WHERE ";
    osSQL << "t_ca_id = " << pIn->acct_id << " AND st_id = t_st_id AND tt_id = t_tt_id AND ";
    osSQL << "s_symb = t_s_symb AND ex_id = s_ex_id ORDER BY t_dts DESC LIMIT 50";
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->size();
    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        pOut->trade_id[i] = jsonArr->at(i)->get("t_id", "").asInt64();
        time_t t = jsonArr->at(i)->get( "t_dts", "" ).asInt64() / 1000;
        struct tm *ti = localtime( &t );
        char buff[56];
        strftime( buff, 56, "%F %T", ti );
 
        sscanf(buff, "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_dts[i].year,
                &pOut->trade_dts[i].month,
                &pOut->trade_dts[i].day,
                &pOut->trade_dts[i].hour,
                &pOut->trade_dts[i].minute,
                &pOut->trade_dts[i].second);
        strncpy(pOut->status_name[i], jsonArr->at(i)->get("st_name", "").asCString(), cST_NAME_len);
        pOut->status_name[i][cST_NAME_len] = '\0';
        strncpy(pOut->type_name[i], jsonArr->at(i)->get("tt_name", "").asCString(), cTT_NAME_len);
        pOut->type_name[i][cTT_NAME_len] = '\0';
        strncpy(pOut->symbol[i], jsonArr->at(i)->get("t_s_symb", "").asCString(), cSYMBOL_len);
        pOut->symbol[i][cSYMBOL_len] = '\0';
        pOut->trade_qty[i] = jsonArr->at(i)->get("t_qty", "").asInt64();
        strncpy(pOut->exec_name[i], jsonArr->at(i)->get("t_exec_name", "").asCString(), cEXEC_NAME_len);
        pOut->exec_name[i][cEXEC_NAME_len] = '\0';
        pOut->charge[i] = jsonArr->at(i)->get("t_chrg", "").asDouble();
        strncpy(pOut->s_name[i], jsonArr->at(i)->get("s_name", "").asCString(), cS_NAME_len);
        pOut->s_name[i][cS_NAME_len] = '\0';
        strncpy(pOut->ex_name[i], jsonArr->at(i)->get("ex_name", "").asCString(), cEX_NAME_len);
        pOut->ex_name[i][cEX_NAME_len] = '\0';
    }

    osSQL.clear();
    osSQL.str("");
    osSQL << "SELECT c_l_name, c_f_name, b_name FROM customer_account, customer, broker ";
    osSQL << "WHERE ca_id = " << pIn->acct_id << " AND c_id = ca_c_id AND b_id = ca_b_id";
    std::vector<Json::Value *> *cArr = sendQuery( 1, osSQL.str().c_str() );
    strncpy(pOut->cust_f_name, cArr->at(0)->get( "c_f_name", "" ).asCString(), cF_NAME_len);
    pOut->cust_f_name[cF_NAME_len] = '\0';
    strncpy(pOut->cust_l_name, cArr->at(0)->get( "c_l_name", "" ).asCString(), cL_NAME_len);
    pOut->cust_l_name[cL_NAME_len] = '\0';
    strncpy(pOut->broker_name, jsonArr->at(0)->get( "b_name", "" ).asCString(), cB_NAME_len);
    pOut->broker_name[cB_NAME_len] = '\0';
	cout << "Trade_Status Frame 1 Done" << endl;
}

void TxnRestDB::execute( const TTradeUpdateFrame1Input *pIn,
                         TTradeUpdateFrame1Output *pOut ) {

    ostringstream osSQL;
    pOut->num_found = 0;
    pOut->num_updated = 0;

    for( int i = 0; i < pIn->max_trades; i++ ) {
        if( pOut->num_updated > pIn->max_updates ) {
            osSQL << "SELECT t_exec_name FROM trade WHERE ";
            osSQL << "t_id = " << pIn->trade_id[i];
            std::vector<Json::Value *> *tArr = sendQuery( 1, osSQL.str().c_str() );

            pOut->num_found++;
            string exec_name = tArr->at(0)->get("t_exec_name", "").asString();
            osSQL.clear();
            osSQL.str("");
            if( exec_name.find(" X ") != string::npos ) {
                osSQL << "SELECT REPLACE( '" << exec_name << "', ' X ', ' ' ) as rep";
                std::vector<Json::Value *> *rep = sendQuery( 1, osSQL.str().c_str() );
                exec_name = rep->at(0)->get("rep", "").asString();
            } else {
                osSQL << "SELECT REPLACE( '" << exec_name << "', ' ', ' X ' ) as rep";
                std::vector<Json::Value *> *rep = sendQuery( 1, osSQL.str().c_str() );
                exec_name = rep->at(0)->get("rep", "").asString();
            } //else

            osSQL.clear();
            osSQL.str("");
            osSQL << "UPDATE trade SET t_exec_name = '" << exec_name << "' WHERE t_id = ";
            osSQL << pIn->trade_id[i];
            std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
            pOut->num_updated++;
            osSQL.clear();
            osSQL.str();
        } //if
        osSQL << "SELECT t_bid_price, t_exec_name, t_is_cash, tt_is_mrkt, t_trade_price ";
        osSQL << "FROM trade, trade_type WHERE t_id = " << pIn->trade_id[i] << " AND t_tt_id = tt_id";
        std::vector<Json::Value *> *tArr = sendQuery( 1, osSQL.str().c_str() );
        pOut->trade_info[i].bid_price = tArr->at(0)->get("t_bid_price", "").asDouble();
        strncpy( pOut->trade_info[i].exec_name, tArr->at(0)->get("t_exec_name", "").asCString(), cEXEC_NAME_len );
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        pOut->trade_info[i].is_cash = tArr->at(0)->get("t_is_cash", "").asInt();
        pOut->trade_info[i].is_market = tArr->at(0)->get("tt_is_mrkt", "").asInt();
        pOut->trade_info[i].trade_price = tArr->at(0)->get("t_trade_price", "").asDouble();

        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT se_amt, se_cash_due_date, se_cash_type FROM settlement WHERE se_t_id = ";
        osSQL << pIn->trade_id[i];
        std::vector<Json::Value *> *seArr = sendQuery( 1, osSQL.str().c_str() );
        pOut->trade_info[i].settlement_amount = seArr->at(0)->get("se_amt", "").asDouble();
        sscanf(seArr->at(0)->get("se_cash_due_date", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        strncpy( pOut->trade_info[i].settlement_cash_type, seArr->at(0)->get("se_cash_type", "").asCString(),
                 cSE_CASH_TYPE_len );
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';

        osSQL.clear();
        osSQL.str("");
        if( pOut->trade_info[i].is_cash ) {
            osSQL << "SELECT ct_amt, ct_dts, ct_name FROM cash_transaction WHERE ct_t_id = ";
            osSQL << pIn->trade_id[i];
            std::vector<Json::Value *> *ctArr = sendQuery( 1, osSQL.str().c_str() );
            pOut->trade_info[i].cash_transaction_amount = ctArr->at(0)->get("ct_amt", "").asDouble();
            sscanf(ctArr->at(0)->get("ct_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].cash_transaction_dts.year,
                &pOut->trade_info[i].cash_transaction_dts.month,
                &pOut->trade_info[i].cash_transaction_dts.day,
                &pOut->trade_info[i].cash_transaction_dts.hour,
                &pOut->trade_info[i].cash_transaction_dts.minute,
                &pOut->trade_info[i].cash_transaction_dts.second);
            strncpy( pOut->trade_info[i].cash_transaction_name, ctArr->at(0)->get("ct_name", "").asCString(),
                     cCT_NAME_len );
            pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
            osSQL.clear();
            osSQL.str("");
        } //if

        osSQL << "SELECT th_dts, th_st_id FROM trade_history WHERE th_t_id = ";
        osSQL << pIn->trade_id[i] << " ORDER BY th_dts LIMIT 3";
        std::vector<Json::Value *> *thArr = sendQuery( 1, osSQL.str().c_str() );
        for( unsigned j = 0; j < thArr->size(); j++ ) {
            sscanf(thArr->at(j)->get("th_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[j].year,
                &pOut->trade_info[i].trade_history_dts[j].month,
                &pOut->trade_info[i].trade_history_dts[j].day,
                &pOut->trade_info[i].trade_history_dts[j].hour,
                &pOut->trade_info[i].trade_history_dts[j].minute,
                &pOut->trade_info[i].trade_history_dts[j].second);
            strncpy(pOut->trade_info[i].trade_history_status_id[j],
                    thArr->at(j)->get("th_st_id", "").asCString(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] = '\0';

        } //for
        osSQL.clear();
        osSQL.str("");
    } //for
}

void TxnRestDB::execute( const TTradeUpdateFrame2Input *pIn,
                         TTradeUpdateFrame2Output *pOut ) {
    ostringstream osSQL;
    osSQL << "SELECT t_bid_price, t_exec_name, t_is_cash, t_id, t_trade_price ";
    osSQL << "FROM trade WHERE t_ca_id = " << pIn->acct_id << " AND t_dts >= '";
    osSQL << pIn->start_trade_dts.year << "-" << pIn->start_trade_dts.month << "-";
    osSQL << pIn->start_trade_dts.day << " " << pIn->start_trade_dts.hour << ":";
    osSQL << pIn->start_trade_dts.minute << ":" << pIn->start_trade_dts.second << "' AND ";
    osSQL << "t_dts <= '" << pIn->end_trade_dts.year << "-" << pIn->end_trade_dts.month << "-";
    osSQL << pIn->end_trade_dts.day << " " << pIn->end_trade_dts.hour << ":" << pIn->end_trade_dts.minute;
    osSQL << ":" << pIn->end_trade_dts.second << "' ORDER BY t_dts ASC LIMIT " << pIn->max_trades;
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->size();
    pOut->num_updated = 0;
    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        pOut->trade_info[i].bid_price = jsonArr->at(0)->get("t_bid_price", "").asDouble();
        strncpy( pOut->trade_info[i].exec_name, jsonArr->at(0)->get("t_exec_name", "").asCString(),
                 cEXEC_NAME_len );
        pOut->trade_info[i].exec_name[cEXEC_NAME_len] = '\0';
        pOut->trade_info[i].is_cash = jsonArr->at(0)->get("t_is_cash", "").asInt();
        pOut->trade_info[i].trade_id = jsonArr->at(0)->get("t_id", "").asInt64();
        pOut->trade_info[i].trade_price = jsonArr->at(0)->get("t_trade_price", "").asDouble();
    } //for

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        if( pOut->num_updated < pIn->max_updates ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "SELECT se_cash_type FROM settlement WHERE se_t_id = " << pOut->trade_info[i].trade_id;
            std::vector<Json::Value *> *seArr = sendQuery( 1, osSQL.str().c_str() );
            char buff[56];
            if( pOut->trade_info[i].is_cash ) {
                if( strcmp( seArr->at(0)->get("se_cash_type", "").asCString(), "Cash Account" ) == 0 ) {
                    strcpy( buff, "Cash" );
                } else {
                    strcpy( buff, "Cash Account" );
                }
            } else {
                if( strcmp( seArr->at(0)->get("se_cash_type", "").asCString(), "Margin Account" ) ==0 ) {
                    strcpy( buff, "Margin" );
                } else {
                    strcpy( buff, "Margin Account" );
                }
            } //else
            osSQL.clear();
            osSQL.str("");
            osSQL << "UPDATE settlement SET se_cash_type = '" << buff << "' WHERE se_t_id = ";
            osSQL << pOut->trade_info[i].trade_id;
            pOut->num_updated++;
        } //if

        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT se_amt, se_cash_due_date, se_cash_type FROM settlement WHERE se_t_id = ";
        osSQL << pOut->trade_info[i].trade_id;
        std::vector<Json::Value *> *seArr = sendQuery( 1, osSQL.str().c_str() );
        pOut->trade_info[i].settlement_amount = seArr->at(0)->get("se_amt", "").asDouble();
        sscanf(seArr->at(0)->get("se_cash_due_date", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second);
        strncpy(pOut->trade_info[i].settlement_cash_type, seArr->at(0)->get("se_cash_type", "").asCString(),
                 cSE_CASH_TYPE_len );
        pOut->trade_info[i].settlement_cash_type[cSE_CASH_TYPE_len] = '\0';

        if( pOut->trade_info[i].is_cash ) {
            osSQL.clear();
            osSQL.str("");
            osSQL << "SELECT ct_amt, ct_dts, ct_name FROM cash_transaction WHERE ct_t_id = ";
            osSQL << pOut->trade_info[i].trade_id;
            std::vector<Json::Value *> *ctArr = sendQuery( 1, osSQL.str().c_str() );
            pOut->trade_info[i].cash_transaction_amount = ctArr->at(0)->get("ct_amt", "").asDouble();
            sscanf(ctArr->at(0)->get("ct_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].cash_transaction_dts.year,
                    &pOut->trade_info[i].cash_transaction_dts.month,
                    &pOut->trade_info[i].cash_transaction_dts.day,
                    &pOut->trade_info[i].cash_transaction_dts.hour,
                    &pOut->trade_info[i].cash_transaction_dts.minute,
                    &pOut->trade_info[i].cash_transaction_dts.second);
            strncpy( pOut->trade_info[i].cash_transaction_name, ctArr->at(0)->get("ct_name", "").asCString(),
                    cCT_NAME_len );
            pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
        }
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT th_dts, th_st_id FROM trade_history WHERE th_t_id = ";
        osSQL << pOut->trade_info[i].trade_id << " ORDER BY th_dts LIMIT 3";
        std::vector<Json::Value *> *thArr = sendQuery( 1, osSQL.str().c_str() );
        for( unsigned j = 0; j < thArr->size(); j++ ) {
            sscanf(thArr->at(j)->get("th_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[j].year,
                &pOut->trade_info[i].trade_history_dts[j].month,
                &pOut->trade_info[i].trade_history_dts[j].day,
                &pOut->trade_info[i].trade_history_dts[j].hour,
                &pOut->trade_info[i].trade_history_dts[j].minute,
                &pOut->trade_info[i].trade_history_dts[j].second);
            strncpy(pOut->trade_info[i].trade_history_status_id[j],
                    thArr->at(j)->get("th_st_id", "").asCString(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] = '\0';
        }
    }//for
}

void TxnRestDB::execute( const TTradeUpdateFrame3Input *pIn,
                         TTradeUpdateFrame3Output *pOut ) {

    ostringstream osSQL;
    osSQL << "SELECT t_ca_id, t_exec_name, t_is_cash, t_trade_price, ";
    osSQL << "t_qty, s_name, t_dts, t_id, t_tt_id, tt_name FROM ";
    osSQL << "trade, trade_type, security WHERE t_s_symb = '";
    osSQL << pIn->symbol << "' AND t_dts >= '" << pIn->start_trade_dts.year;
    osSQL << "-" << pIn->start_trade_dts.month << "-" << pIn->start_trade_dts.day;
    osSQL << " " << pIn->start_trade_dts.hour << ":" << pIn->start_trade_dts.minute;
    osSQL << ":" << pIn->start_trade_dts.second << "' AND t_dts <= '";
    osSQL << pIn->end_trade_dts.year << "-" << pIn->end_trade_dts.month << "-";
    osSQL << pIn->end_trade_dts.day << " " << pIn->end_trade_dts.hour << ":";
    osSQL << pIn->end_trade_dts.minute << ":" << pIn->end_trade_dts.second;
    osSQL << "' AND tt_id = t_tt_id AND s_symb = t_s_symb AND t_ca_id <= ";
    osSQL << pIn->max_acct_id << " ORDER BY t_dts ASC";
    std::vector<Json::Value *> *jsonArr = sendQuery( 1, osSQL.str().c_str() );

    pOut->num_found = jsonArr->size();
    pOut->num_updated = 0;

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        pOut->trade_info[i].acct_id = jsonArr->at(i)->get("t_ca_id", "").asInt();
        strncpy( pOut->trade_info[i].exec_name, jsonArr->at(i)->get("t_exec_name", "").asCString(),
                 cEXEC_NAME_len );
        pOut->trade_info[i].is_cash = jsonArr->at(i)->get("t_is_cash", "").asInt();
        pOut->trade_info[i].quantity = jsonArr->at(i)->get("t_is_qty" ,"").asInt64();
        strncpy( pOut->trade_info[i].s_name, jsonArr->at(i)->get("s_name", "").asCString(),
                 cS_NAME_len );
        pOut->trade_info[i].s_name[cS_NAME_len] = '\0';
        sscanf( jsonArr->at(i)->get("t_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_dts.year,
                &pOut->trade_info[i].trade_dts.month,
                &pOut->trade_info[i].trade_dts.day,
                &pOut->trade_info[i].trade_dts.hour,
                &pOut->trade_info[i].trade_dts.minute,
                &pOut->trade_info[i].trade_dts.second );
        pOut->trade_info[i].trade_id = jsonArr->at(i)->get("t_id", "").asInt64();
        strncpy( pOut->trade_info[i].trade_type, jsonArr->at(i)->get("t_tt_id", "").asCString(),
                 cTT_ID_len );
        pOut->trade_info[i].trade_type[cTT_ID_len] = '\0';
        strncpy( pOut->trade_info[i].type_name, jsonArr->at(i)->get("tt_name", "").asCString(),
                 cTT_NAME_len );
        pOut->trade_info[i].type_name[cTT_NAME_len] = '\0';
    }

    for( unsigned i = 0; i < jsonArr->size(); i++ ) {
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT se_amt, se_cash_due_date, se_cash_type FROM settlement se_t_id = ";
        osSQL << pOut->trade_info[i].trade_id;
        std::vector<Json::Value *> *seArr = sendQuery( 1, osSQL.str().c_str() );
        pOut->trade_info[i].settlement_amount = seArr->at(0)->get("se_amt", "").asDouble();
        sscanf( seArr->at(i)->get("se_cash_due_date", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].settlement_cash_due_date.year,
                &pOut->trade_info[i].settlement_cash_due_date.month,
                &pOut->trade_info[i].settlement_cash_due_date.day,
                &pOut->trade_info[i].settlement_cash_due_date.hour,
                &pOut->trade_info[i].settlement_cash_due_date.minute,
                &pOut->trade_info[i].settlement_cash_due_date.second );
        strncpy( pOut->trade_info[i].settlement_cash_type, seArr->at(i)->get("se_cash_type", "").asCString(),
                 cSE_CASH_TYPE_len );
        if( pOut->trade_info[i].is_cash ) {
            if( pOut->num_updated < pIn->max_updates ) {
                osSQL.clear();
                osSQL.str("");
                osSQL << "SELECT ct_name FROM cash_transaction WHERE ct_t_id = ";
                osSQL << pOut->trade_info[i].trade_id;
                std::vector<Json::Value *> *ctArr = sendQuery( 1, osSQL.str().c_str() );
                string ct_name = ctArr->at(0)->get("ct_name", "").asString();

                ostringstream osname;
                if( ct_name.find( " shares of " ) != string::npos ) {
                    osname << pOut->trade_info[i].type_name << " " << pOut->trade_info[i].quantity;
                    osname << " Shares of " <<  pOut->trade_info[i].s_name;
                } else {
                    osname << pOut->trade_info[i].type_name << " " << pOut->trade_info[i].quantity;
                    osname << " Shares of " <<  pOut->trade_info[i].s_name;
                }
                ct_name = osname.str();

                osSQL.clear();
                osSQL.str("");
                osSQL << "UPDATE cash_transaction SET ct_name = " << ct_name << " WHERE ";
                osSQL << "ct_t_id = " << pOut->trade_info[i].trade_id;
                std::vector<Json::Value *> *res = sendQuery( 1, osSQL.str().c_str() );
                pOut->num_updated++;
            }

            osSQL.clear();
            osSQL.str("");
            osSQL << "SELECT ct_amt, ct_dts, ct_name FROM cash_transaction WHERE ";
            osSQL << "ct_t_id = " << pOut->trade_info[i].trade_id;
            std::vector<Json::Value *> *ctArr = sendQuery( 1, osSQL.str().c_str() );
            pOut->trade_info[i].cash_transaction_amount = ctArr->at(0)->get("ct_amt", "").asDouble();
            sscanf(ctArr->at(0)->get("ct_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                    &pOut->trade_info[i].cash_transaction_dts.year,
                    &pOut->trade_info[i].cash_transaction_dts.month,
                    &pOut->trade_info[i].cash_transaction_dts.day,
                    &pOut->trade_info[i].cash_transaction_dts.hour,
                    &pOut->trade_info[i].cash_transaction_dts.minute,
                    &pOut->trade_info[i].cash_transaction_dts.second);
            strncpy( pOut->trade_info[i].cash_transaction_name, ctArr->at(0)->get("ct_name", "").asCString(),
                     cCT_NAME_len );
            pOut->trade_info[i].cash_transaction_name[cCT_NAME_len] = '\0';
        } //is_cash
        osSQL.clear();
        osSQL.str("");
        osSQL << "SELECT th_dts, th_st_id FROM trade_history WHERE th_t_id = ";
        osSQL << pOut->trade_info[i].trade_id << " ORDER BY th_dts ASC LIMIT 3";
        std::vector<Json::Value *> *thArr = sendQuery( 1, osSQL.str().c_str() );
        for( unsigned j = 0; j < thArr->size(); j++ ) {
            sscanf(thArr->at(j)->get("th_dts", "").asCString(), "%hd-%hd-%hd %hd:%hd:%hd",
                &pOut->trade_info[i].trade_history_dts[j].year,
                &pOut->trade_info[i].trade_history_dts[j].month,
                &pOut->trade_info[i].trade_history_dts[j].day,
                &pOut->trade_info[i].trade_history_dts[j].hour,
                &pOut->trade_info[i].trade_history_dts[j].minute,
                &pOut->trade_info[i].trade_history_dts[j].second);
            strncpy(pOut->trade_info[i].trade_history_status_id[j],
                    thArr->at(j)->get("th_st_id", "").asCString(), cTH_ST_ID_len);
            pOut->trade_info[i].trade_history_status_id[j][cTH_ST_ID_len] = '\0';
        } //for j
    } //for i
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
