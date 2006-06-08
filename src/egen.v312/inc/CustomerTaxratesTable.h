/*
*	(c) Copyright 2002-2003, Microsoft Corporation
*	Provided to the TPC under license.
*	Written by Sergey Vasilevskiy.
*/


/*
*	Class representing the Customer Taxrates table.
*/
#ifndef CUSTOMER_TAXRATES_TABLE_H
#define CUSTOMER_TAXRATES_TABLE_H

#include "EGenTables_stdafx.h"

namespace TPCE
{

const int iTaxRatesPerCust = 2;	//number of tax rates per customer
const int iMaxDivOrCtryName = 6;

// Number of RNG calls to skip for one row in order
// to not use any of the random values from the previous row.
const int iRNGSkipOneRowCustomerTaxrate = 5;	// real max count in v3.5: 2

typedef struct CUSTOMER_TAXRATE_ROWS
{
	CUSTOMER_TAXRATE_ROW	m_row[iTaxRatesPerCust];	//multiple tax rates rows per customer
} *PCUSTOMER_TAXRATE_ROWS;

class CCustomerTaxratesTable : public TableTemplate<CUSTOMER_TAXRATE_ROWS>
{
	CCustomerTable	m_cust;
	CAddressTable	m_addr;
	CInputFileNoWeight<TTaxRateInputRow>	*m_division_rates;
	CInputFileNoWeight<TTaxRateInputRow>	*m_country_rates;

	/*
	*	Reset the state for the next load unit
	*/
	void InitNextLoadUnit()
	{
		m_rnd.SetSeed(m_rnd.RndNthElement(RNGSeedTableDefault, 
										  m_cust.GetCurrentC_ID() * iRNGSkipOneRowCustomerTaxrate));
	}

	//generate the tax row deterministically for a given customer and country or division code
	TTaxRateInputRow	GetTaxRow(TIdent C_ID, int iCode, bool bCtry)
	{
		RNGSEED							OldSeed;
		int								iThreshold;
		const vector<TTaxRateInputRow>	*pRates;

		OldSeed = m_rnd.GetSeed();

		m_rnd.SetSeed( m_rnd.RndNthElement( RNGSeedBaseTaxRateRow, C_ID ));

		if (bCtry)	//return country tax row?
			pRates = m_country_rates->GetRecord(iCode-1);
		else		//return division tax row
			pRates = m_division_rates->GetRecord(iCode-1);

		iThreshold = m_rnd.RndIntRange(0, (int)pRates->size()-1);

		m_rnd.SetSeed( OldSeed );
		
		return (*pRates)[iThreshold];
	}

public:	
		CCustomerTaxratesTable(CInputFiles inputFiles, TIdent iCustomerCount, TIdent iStartFromCustomer)
			: TableTemplate<CUSTOMER_TAXRATE_ROWS>(),
			m_cust(inputFiles, iCustomerCount, iStartFromCustomer),
			m_addr(inputFiles, iCustomerCount, iStartFromCustomer, true),
			m_division_rates(inputFiles.TaxRatesDivision),
			m_country_rates(inputFiles.TaxRatesCountry)
		{
		};

		/*
		*	Generates all column values for the next row.
		*/
		bool GenerateNextRecord()
		{			
			int iDivCode, iCtryCode;			

			if (m_cust.GetCurrentC_ID() % iDefaultLoadUnitSize == 0)
			{
				InitNextLoadUnit();
			}

			++m_iLastRowNumber;

			m_cust.GenerateNextC_ID();	//next customer id
			m_addr.GenerateNextAD_ID();	//next address id (to get the one for this customer)
			m_addr.GetDivisionAndCountryCodes(iDivCode, iCtryCode);
			//Fill the country tax rate row
			m_row.m_row[0].CX_C_ID = m_cust.GetCurrentC_ID();	//fill the customer id
			//Select the country rate			
			strncpy(m_row.m_row[0].CX_TX_ID, GetCountryTaxRow(m_cust.GetCurrentC_ID(), iCtryCode).TAX_ID, 
					sizeof(m_row.m_row[0].CX_TX_ID));

			//Fill the division tax rate row
			m_row.m_row[1].CX_C_ID = m_cust.GetCurrentC_ID();	//fill the customer id
			//Select the division rate			
			strncpy(m_row.m_row[1].CX_TX_ID, GetDivisionTaxRow(m_cust.GetCurrentC_ID(), iDivCode).TAX_ID,
					sizeof(m_row.m_row[0].CX_TX_ID));

			m_bMoreRecords = m_cust.MoreRecords();

			return (MoreRecords());
		}		

		PCUSTOMER_TAXRATE_ROW	GetRow(UINT i) {
			if (i<iTaxRatesPerCust)
				return &m_row.m_row[i];
			else
				return NULL;
		}

		int GetTaxRatesCount() {return iTaxRatesPerCust;}	//tax rates per customer		

		//generate country tax row for a given customer
		TTaxRateInputRow	GetCountryTaxRow(TIdent C_ID, int iCtryCode)
		{
			return GetTaxRow(C_ID, iCtryCode, true);
		}

		//generate division tax row for a given customer
		TTaxRateInputRow	GetDivisionTaxRow(TIdent C_ID, int iDivCode)
		{
			return GetTaxRow(C_ID, iDivCode, false);
		}
};

}	// namespace TPCE

#endif //CUSTOMER_TAXRATES_TABLE_H
