/*
*	(c) Copyright 2002-2003, Microsoft Corporation
*	Provided to the TPC under license.
*	Written by Sergey Vasilevskiy.
*/

/*
*	This file contains functions that read rows from different input files.
*/

#include "../inc/EGenTables_stdafx.h"

using namespace TPCE;

/*
*	Function to read customer account names from the input stream.
*/
void TAccountNameInputRow::Load(istream &file)
{	
	// need to eat end-of-line or it will be read into NAME
	file>>ws;
	file.get(NAME, sizeof(NAME), '\n');	
}

/*
*	Function to read phone row from the input stream.
*	Needed to construct phones list.
*/
void TAreaCodeInputRow::Load(istream &file)
{
	file>>AREA_CODE;
}

void TCompanyInputRow::Load(istream &file)
{
	file>>CO_ID;
	file>>CO_ST_ID>>ws;
	file.get(CO_NAME, sizeof(CO_NAME), '\t');
	file>>CO_IN_ID>>ws;
	file.get(CO_DESC, sizeof(CO_DESC), '\n');
}

/*
*	Function to read CompanyCompetitor row from the input stream.
*/
void TCompanyCompetitorInputRow::Load(istream &file)
{
	file>>ws;
	file>>CP_CO_ID;
	file>>ws;
	file>>CP_COMP_CO_ID;
	file>>ws;
	file.get(CP_IN_ID, sizeof(CP_IN_ID), '\n');	
}

/*
*	Function to read Company SP Rate row from the input stream.
*/
void TCompanySPRateInputRow::Load(istream &file)
{
	file>>ws;
	file.get( CO_SP_RATE, sizeof( CO_SP_RATE ), '\n' );
}

/*
*	Function to read first/last name row from the input stream.
*	Needed to construct phones list.
*/
void TFirstNameInputRow::Load(istream &file)
{
	file>>FIRST_NAME;	//one field only
}

void TLastNameInputRow::Load(istream &file)
{
	file>>LAST_NAME;	//one field only
}

void TNewsInputRow::Load(istream &file)
{
	file>>WORD;	//one field only
}

void TSecurityInputRow::Load(istream &file)
{
	file>>S_ID;
	file>>S_ST_ID>>ws;	
	file>>S_SYMB;
	file>>S_ISSUE;
	file>>S_EX_ID;
	file>>S_CO_ID;
}

/*
*	Function to read one row from the input stream.
*/
void TStreetNameInputRow::Load(istream &file)
{	
	file>>ws;
	file.get(STREET, sizeof(STREET)-1, '\n');	//read up to the delimiter
}

/*
*	Function to read one row from the input stream.
*/
void TStreetSuffixInputRow::Load(istream &file)
{	
	file>>ws;
	file.get(SUFFIX, sizeof(SUFFIX)-1, '\n');	//read up to the delimiter
}

/*
*	Function to read row from the input stream.
*/
void TTaxRateInputRow::Load(istream &file)
{
	file>>TAX_ID;
	file>>ws;	//advance past whitespace to the next field
	file.get(TAX_NAME, sizeof(TAX_NAME), '\t');
	//now read the actual taxrate
	file>>TAX_RATE;	
}

/*
*	Function to read one row from the input stream.
*/
void TZipCodeInputRow::Load(istream &file)
{	
			file>>ws;
			file>>iDivisionTaxKey;
			file>>ws;
			file.get( ZC_CODE, sizeof( ZC_CODE ), '\t' );
			file>>ws;
			file.get( ZC_TOWN, sizeof( ZC_TOWN ), '\t' );	//read up to the delimiter
			file>>ws;
			file.get( ZC_DIV, sizeof( ZC_DIV ), '\n' );
}

/***********************************************************************************
*
* Tables that are fully represented by flat files (no additional processing needed).
*
************************************************************************************/

/*
*	CHARGE
*/
void CHARGE_ROW::Load(istream &file)
{
	file>>CH_TT_ID;
	file>>CH_C_TIER;
	file>>CH_CHRG;
}

/*
*	COMMISSION_RATE
*/
void COMMISSION_RATE_ROW::Load(istream &file)
{
	file>>CR_C_TIER;
	file>>CR_TT_ID;
	file>>CR_EX_ID;
	file>>CR_FROM_QTY;
	file>>CR_TO_QTY;
	file>>CR_RATE;
}

/*
*	EXCHANGE
*/
void EXCHANGE_ROW::Load(istream &file)
{
	file>>ws;
	file.get(EX_ID, sizeof(EX_ID), '\t');	//read and skip past the next tab
	file>>ws;
	file.get(EX_NAME, sizeof(EX_NAME), '\t');	//read up to the delimiter
	file>>ws;
	file>>EX_NUM_SYMB;
	file>>ws;
	file>>EX_OPEN;
	file>>ws;
	file>>EX_CLOSE;
	file>>ws;
	file.get(EX_DESC, sizeof(EX_DESC), '\t');	//read up to the delimiter
	file>>ws;
	file>>EX_AD_ID;
}

/*
*	INDUSTRY
*/
void INDUSTRY_ROW::Load(istream &file)
{
	file>>IN_ID>>ws;
	file.get(IN_NAME, sizeof(IN_NAME), '\t');	//read up to the delimiter
	file>>ws;
	file>>IN_SC_ID;
}

/*
*	SECTOR
*/
void SECTOR_ROW::Load(istream &file)
{
	file>>SC_ID>>ws;	//read and skip past the next tab
	file.get(SC_NAME, sizeof(SC_NAME), '\n');	//read up to the delimiter	
}

/*
*	STATUS_TYPE
*/
void STATUS_TYPE_ROW::Load(istream &file)
{
	file>>ws;
	file>>ST_ID;
	file>>ws;
	file.get(ST_NAME, sizeof(ST_NAME), '\n');	
}

/*
*	TRADE_TYPE
*/
void TRADE_TYPE_ROW::Load(istream &file)
{
	file>>TT_ID>>ws;
	file.get(TT_NAME, sizeof(TT_NAME), '\t');
	file>>ws;
	file>>TT_IS_SELL;
	file>>TT_IS_MRKT;
}

/*
*	ZIP_CODE
*/
void ZIP_CODE_ROW::Load(istream &file)
{
	file.get(ZC_TOWN, sizeof(ZC_TOWN)-1, '\t');	//read up to the delimiter
	file>>ZC_DIV;
	file>>ZC_CODE;
	file>>ws;
}
