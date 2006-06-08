/*
*	(c) Copyright 2002-2004, Microsoft Corporation
*	Provided to the TPC under license.
*	Written by Sergey Vasilevskiy.
*/

/*
*	Class that generates trades and holdings for a given set
*	of customers. It maintains the proper holding information
*	for every customer while the trades are being generated.
*
*	TRADE, TRADE_HISTORY, CASH_TRANSACTION, SETTLEMENT, 
*	HOLDING, HOLDING_HISTORY rows are generated by this class.
*
*/

#ifndef TRADE_GEN_H
#define TRADE_GEN_H

#include "EGenTables_stdafx.h"

namespace TPCE
{

// Maximum number of HOLDING_HISTORY rows that can be output
// for one completed trade.
// Determined by the maximum number of holdings that a trade
// can modify. The maximum number would be a trade with
// the biggest possible quantity modifying holdings each having
// the smallest possible quantity.
//
const int	iMaxHoldingHistoryRowsPerTrade = 800 / 100;

// Incomplete trade information generated
// at Trade Order time.
//
typedef struct TTradeInfo
{
	TIdent			iTradeId;
	eTradeTypeID	eTradeType;		//integer representation of the TRADE row T_TT_ID
	eStatusTypeID	eTradeStatus;	//integer representation of the TRADE row T_ST_ID
	double			PendingTime;	// seconds from StartTime; only for limit orders
	double			SubmissionTime;	// seconds from StartTime
	double			CompletionTime;	// seconds from StartTime
	TIdent			iSymbolIndex;	// stock symbol index in the input flat file
	int				iSymbolIndexInAccount;	// stock symbol index in the account basket
	int				iTradeQty;	// number of shares in the trade
	CMoney			fBidPrice;	// bid price for market orders or limit price for limit ones
	CMoney			fTradePrice;// price that the trade completed at
	TIdent			iCustomer;	// customer executing this trade	
	eCustomerTier	iCustomerTier; // customer tier for the customer executing this trade
	TIdent			iCustomerAccount; // customer account in which the trade executes	
	int				iIsLifo;	// needed to update holdings
} *PTradeInfo;

// Information about completed trade that is generated once
// for performance
typedef struct TAdditionalTradeInfo
{	
	//	Current value of trade's positions that are being closed
	//
	CMoney							fBuyValue;
	//	Value of trade's positions when they were opened
	//	
	CMoney							fSellValue;

	//	Broker id of the account for the current completed trade
	//
	TIdent							iCurrentBrokerId;

	eTaxStatus						eAccountTaxStatus;

	// These fields are needed for correcteness (pricing consistency).
	// They need to be kept in CMoney and only converted to double
	// before copying them into table row structures.
	//
	CMoney							Commission;
	CMoney							Charge;
	CMoney							Tax;
	CMoney							SettlementAmount;
} *PAdditionalTradeInfo;

// Customer holding information
// to be able to generate HOLDING table after all trades
// have been generated.
//
typedef struct THoldingInfo
{
	TIdent			iTradeId;
	int				iTradeQty;
	CMoney			fTradePrice;
	CDateTime		BuyDTS;
	TIdent			iSymbolIndex;	// stock symbol index in the input flat file - stored for performance
} *PHoldingInfo;

// Trade-related table rows.
// HOLDING row is ommitted because it is contained in
// a separate variable.
//
typedef struct TTradeRow
{		
	TRADE_ROW				m_Trade;			// for the Trade table
	TRADE_REQUEST_ROW		m_TradeRequest;		// for the Trade Requests table	
	TRADE_HISTORY_ROW		m_TradeHistory[3];	// for the Trade History table
	SETTLEMENT_ROW			m_Settlement;		// for the Settlement table
	CASH_TRANSACTION_ROW	m_CashTransaction;	// for the Cash Transaction table
	HOLDING_HISTORY_ROW		m_HoldingHistory[iMaxHoldingHistoryRowsPerTrade];
} *PTradeRow;

// Container to store holdings
//
typedef list<THoldingInfo> THoldingList;
typedef THoldingList TCustomerHoldingArray[iMaxSecuritiesPerAccount];

class CTradeGen
{
	//	RNG class for generation of row data
	//
	CRandom							m_rnd;

	// Class used to get address information for a customer
	// to properly calculate tax on a trade
	//
	CAddressTable					m_AddressTable;

	//	Class used to select a random customer for whom to perform a trade.
	//
	CCustomerSelection				m_CustomerSelection;

	//	Class used to get CUSTOMER table information for a specific customer
	//
	CCustomerTable					m_CustomerTable;	

	// Class used to calculate T_TAX for the TRADE table
	//
	CCustomerTaxratesTable			m_CustTaxrateTable;

	// Class used to get customer account information.
	//
	CCustomerAccountsAndPermissionsTable	m_CustomerAccountTable;

	// Class used in determining the basket of securities for an account
	//
	CHoldingsAndTradesTable			m_HoldingTable;

	// Class used to generate BROKER table (with consistent YTD columns)
	//
	CBrokersTable					m_BrokerTable;

	// Class used to get S_NAME for cash transaction descriptions
	//
	CSecurityTable					m_SecurityTable;

	// Class to get the first and last names of a customer
	//
	CPerson							m_Person;

	//	Input files for character data generation.
	//
	CCompanyFile*					m_CompanyFile;
	CSecurityFile*					m_SecurityFile;
	TChargeFile*					m_ChargeFile;	//CHARGE table from the flat file
	TCommissionRateFile*			m_CommissionRateFile;	//COMMISSION_RATE table from the flat file
	TStatusTypeFile*				m_StatusTypeFile;	// STATUS_TYPE table from the flat file
	TTradeTypeFile*					m_TradeTypeFile;	// TRADE_TYPE table from the flat file

	//	Total number of securities for which to generate trades
	//
	TIdent							m_iSecCount;

	//	The first customer to generate for this class instance
	//
	TIdent							m_iStartFromCustomer;
	//	First account of the StartFromCustomer
	//
	TIdent							m_iStartFromAccount;
	// Number of customers for this class instance
	//
	TIdent							m_iCustomerCount;	
	// Total number of customers in the database
	//
	TIdent							m_iTotalCustomers;
	//	Number of customers in one load unit
	//
	int								m_iLoadUnitSize;
	//	Number of accounts for customers in one load unit
	//
	int								m_iLoadUnitAccountCount;
	//	Number of customers for 1 tpsE
	//
	int								m_iScaleFactor;
	//	Number of hours of initial trades to generate
	//
	int								m_iHoursOfInitialTrades;

	//	Average number of seconds between two consecutive trades
	//
	double							m_fMeanTimeBetweenTrades;

	// Mean delay between Pending and Submission times
	// for an immediatelly triggered (in-the-money) limit order.
	//
	double							m_fMeanInTheMoneySubmissionDelay;

	// Time at which to start trade timestamps (time 0 or StartTime).
	// Not changed during the class lifetime.
	//
	// This is the submission (or pending) time of the first trade.
	//
	CDateTime						m_StartTime;

	// Current Trade Order time in the simulated
	// time sequence (seconds from StartTime). 

	// When this time is further than
	// the priority queue's front, incomplete trades
	// are removed from the priority queue and completed.
	//
	// If this time is before the priority queue front
	// time, new incomplete trades are placed on the queue
	// and this time is incremented.
	//
	double							m_CurrentSimulatedTime;

	// Priority queue that contains incomplete trades
	// ordered by their completion time. The queue's
	// front contains trade with the earliest completion
	// time.
	//
	// Template default is to return the biggest value,
	// but we need the smallest (earliest) one. This is
	// why we specify greater<> for the template comparison
	// parameter instead of the default less<>
	priority_queue< TTradeInfo, 
					vector<TTradeInfo>, 
					greater<TTradeInfo> >		m_CurrentTrades;

	// Number of trades completed up to now.
	// Does not include aborted trades.
	//
	TIdent							m_iCurrentCompletedTrades;

	// Number of total trades needed to generate.
	// Does not include aborted trades.
	//
	TIdent							m_iTotalTrades;

	// Number of trades initiated up to now. Includes aborted
	// trades. 
	// Needed to calculate when to abort a trade at Trade Order time.
	//
	TIdent							m_iCurrentInitiatedTrades;

	// Number of trades in an 8-hour workday.
	// Needed to know when to move trading time to the next day.
	//
	int								m_iTradesPerWorkDay;


	// 3-dimensional array of double-linked lists each containing
	// one customer holding information.
	//
	// The array is indexed as follows:
	// [AccountId][SecurityIndexWithinAccount]
	//
	// There is no need to index on customer id since the account
	// id is unique across the universe of all customers
	//
	TCustomerHoldingArray*			m_pCustomerHoldings;

	// Structure to contain incomplete, but essential
	// trade information generated at Trade Order time.
	//
	TTradeInfo						m_NewTrade;

	// Structure to contain trade non-essential information
	// frequently used at Trade Result time.
	//
	TAdditionalTradeInfo			m_CompletedTradeInfo;

	// Structure to contain current trade and holding
	// table rows.
	// Filled in GenerateNextTrade() for trade-related
	// tables and in GenerateNextHolding for 
	// holding-related tables.
	//
	TTradeRow						m_TradeRow;
	HOLDING_ROW						m_HoldingRow;

	// Structure to contain HOLDING_SUMMARY rows.
	// Filled in GenerateNextHoldingSummaryRow.
	HOLDING_SUMMARY_ROW				m_HoldingSummaryRow;

	// Number of Trade History rows for the current trade in m_TradeRow.
	//
	int								m_iTradeHistoryRowCount;

	// Number of Cash Transaction rows for the current trade in m_TradeRow.
	//
	int								m_iCashTransactionRowCount;

	// Number of Settlement rows for the current trade in m_TradeRow.
	//
	int								m_iSettlementRowCount;

	// Number of Holding History rows. May be more than one
	// if the trade modifies more than one holding.
	//
	int								m_iHoldingHistoryRowCount;

	// Security price emulation
	CMEESecurity					m_Security;

	//	Account, security index, and security holding
	//	to use in GenerateNextHolding() to return the next holding.	
	//	
	int								m_iCurrentAccountForHolding;
	int								m_iCurrentSecurityForHolding;	// index within the account (not input file)
	list<THoldingInfo>::iterator	m_pCurrentSecurityHolding;

	//	Account index and security index, 
	//  used in GenerateNextHoldingSummaryRecord().
	//	
	int								m_iCurrentAccountForHoldingSummary;		//index
	int								m_iCurrentSecurityForHoldingSummary;	//index

	//	Trade ID for the last generated trade.
	//	Positioned at the correct trade id at start.
	//
	TIdent							m_iCurrentTradeId;		

	//	Current load unit number
	//
	int								m_iCurrentLoadUnit;
	

	///////////////////////////////////////////////////
	// Functions
	///////////////////////////////////////////////////

	// Generate enough trade information to put into
	// the priority queue.
	//
	void GenerateNewTrade();

	// Take the incoplete trade information and 
	// generate all the trade-related rows in internal
	// row structures.
	//
	void GenerateCompleteTrade();

	// Generate a random delay (in seconds) between 
	// two consecutive trades.
	//
	inline double GenerateDelayBetweenTrades();

	//	Return the list of holdings to modify
	//	by the most recently generated complete trade	
	//
	inline THoldingList*	GetHoldingListForCurrentTrade();

	//	Return position before the first or the last element
	//	in the holding list (depending on IsLifo flag)
	//
	list<THoldingInfo>::iterator PositionAtHoldingList(
														THoldingList*	pHoldingList,
														int				IsLifo);
	// Update holding information for the customer and trade
	// contained in the internal trade row structure.
	// Set internal buy and sell values.
	//
	void UpdateHoldings();

	// Position internal iterator at the next non-empty holding.
	// Update internal customer/account/security counters as required.
	//
	// Return whether a non-empty holding exists.
	//
	bool FindNextHolding();
	
	// Position internal indexes to next non-empty list of holdings
	// Return whether non-empty holding list exists.
	//
	bool FindNextHoldingList();
	

	// Generate a new trade id
	//
	TIdent GenerateNextTradeId();

	// Generate a random trade type
	//
	eTradeTypeID GenerateTradeType();

	// Generate some common fields for the completed trade.
	// Those fields are used more than once so they are stored
	// in a separate structure.
	//
	void GenerateCompletedTradeInfo();

	// Generate TRADE row
	//
	void GenerateTradeRow();

	void GenerateTradeCharge();

	void GenerateTradeCommission();

	void GenerateTradeTax();

	// Generate settlement amount for SE_AMT and CT_AMT
	//
	void GenerateSettlementAmount();

	// Generate TRADE_HISTORY rows
	//
	void GenerateTradeHistoryRow();	

	// Generate CASH_TRANSACTION row
	//
	void GenerateCashTransactionRow();	

	// Generate SETTLEMENT row
	//
	void GenerateSettlementRow();

	// Generate HOLDING_HISTORY row
	//
	void GenerateHoldingHistoryRow(TIdent iHoldingTradeID, // trade id of the original trade
								   TIdent iTradeTradeID,	// trade id of the modifying trade
								   int iBeforeQty,		// holding qty now
								   int iAfterQty);		// holding qty after modification
	
	// Helper function to get the current customer id
	TIdent GetCurrentCustID() { return m_NewTrade.iCustomer; }
	// Helper function to get the current customer tier
	int GetCurrentCustTier() { return m_NewTrade.iCustomerTier; }
	// Helper function to get the current account id
	TIdent GetCurrentAccID() { return m_NewTrade.iCustomerAccount; }	
	// Helper function to get the current trade id
	TIdent GetCurrentTradeID() { return m_NewTrade.iTradeId; }
	// Helper function to get the current trade bid price
	CMoney GetCurrentBidPrice() { return m_NewTrade.fBidPrice; }
	// Helper function to get the current trade execution price
	CMoney GetCurrentTradePrice() { return m_NewTrade.fTradePrice; }
	// Helper function to get the current trade quantity
	int GetCurrentTradeQty() { return m_NewTrade.iTradeQty; }
	// Helper function to get the current trade type
	eTradeTypeID GetCurrentTradeType() { return m_NewTrade.eTradeType; }
	// Helper function to get the current trade status
	eStatusTypeID GetCurrentTradeStatus() { return m_NewTrade.eTradeStatus; }
	// Helper function to get whether the current trade is cash
	int GetCurrentTradeIsCash() { return m_TradeRow.m_Trade.T_IS_CASH; }
	// Helper function to get the current security symbol index
	TIdent GetCurrentSecurityIndex() { return m_NewTrade.iSymbolIndex; }	
	// Helper function to get the current security index in the account's basket (1-25)
	int GetCurrentSecurityAccountIndex() { return m_NewTrade.iSymbolIndexInAccount; }
	// Helper function to get the current trade pending time
	CDateTime GetCurrentTradePendingTime() { 
		CDateTime	ReturnTime = m_StartTime;
		int			iDays, iMs;

		// submit days separatelly to avoid int32 overflow in ms after 25 days

		iDays = (int)(m_NewTrade.PendingTime / (24 * 3600));
		iMs = (int)((m_NewTrade.PendingTime - iDays * 24 * 3600) * 1000);
		iDays = ((iDays / 5) * 7) + (iDays % 5);
		ReturnTime.Add(iDays, iMs);	// add days and msec

		return ReturnTime;
	}
	// Helper function to get the current trade submission time
	CDateTime GetCurrentTradeSubmissionTime() { 
		CDateTime	ReturnTime = m_StartTime;
		int			iDays, iMs;

		// submit days separatelly to avoid int32 overflow in ms after 25 days

		iDays = (int)(m_NewTrade.SubmissionTime / (24 * 3600));
		iMs = (int)((m_NewTrade.SubmissionTime - iDays * 24 * 3600) * 1000);
		iDays = ((iDays / 5) * 7) + (iDays % 5);
		ReturnTime.Add(iDays, iMs);	// add days and msec
		
		return ReturnTime;
	}
	// Helper function to get the current trade completion time
	CDateTime GetCurrentTradeCompletionTime() { 
		CDateTime	ReturnTime = m_StartTime;

		int			iDays, iMs;

		// submit days separatelly to avoid int32 overflow in ms after 25 days

		iDays = (int)(m_NewTrade.CompletionTime / (24 * 3600));
		iMs = (int)((m_NewTrade.CompletionTime - iDays * 24 * 3600) * 1000);
		iDays = ((iDays / 5) * 7) + (iDays % 5);
		ReturnTime.Add(iDays, iMs);	// add days and msec
		
		return ReturnTime;		
	}	
	// Helper function to get the current trade's is_lifo flag
	int GetCurrentTradeIsLifo() { return m_NewTrade.iIsLifo; }
	// Helper function to get the current trade's sell value
	CMoney GetCurrentTradeSellValue() { return m_CompletedTradeInfo.fSellValue; }
	// Helper function to get the current trade's buy value
	CMoney GetCurrentTradeBuyValue() { return m_CompletedTradeInfo.fBuyValue; }
	// Helper function to get the current account broker id
	TIdent GetCurrentBrokerId() { return m_CompletedTradeInfo.iCurrentBrokerId; }
	// Helper function to get the current account tax status
	eTaxStatus GetCurrentTaxStatus() { return m_CompletedTradeInfo.eAccountTaxStatus; }
	// Helper function to get the current trade's charge amount
	CMoney GetCurrentTradeCharge() { return m_CompletedTradeInfo.Charge; }
	// Helper function to get the current trade's commission amount
	CMoney GetCurrentTradeCommission() { return m_CompletedTradeInfo.Commission; }
	// Helper function to get the current trade's tax amount
	CMoney GetCurrentTradeTax() { return m_CompletedTradeInfo.Tax; }
	// Helper function to get the current trade's settlement amount
	CMoney GetCurrentSettlementAmount() { return m_CompletedTradeInfo.SettlementAmount; }

public:
	// Constructor
	//
	CTradeGen(
		CInputFiles			inputFiles,
		TIdent				iCustomerCount,
		TIdent				iStartFromCustomer,		
		TIdent				iTotalCustomers,
		int					iLoadUnitSize,
		int					iScaleFactor,
		int					iHoursOfInitialTrades
		);

	// Destructor
	//
	~CTradeGen();	

	// Generate one Trade Result and return the
	// resulting trade. This function will generate
	// a new incomplete trade (Trade Order) if needed
	// and put it onto the priority queue.
	// It will also update the holding information
	// as needed.
	//
	// Returns whether there is another trade to return.
	//
	bool GenerateNextTrade();

	// Generate next HOLDING_SUMMARY record.
	// Returns whether there is another HOLDING_SUMMARY record to return.
	//
	bool GenerateNextHoldingSummaryRow();

	// Generate next holding row.
	// This function will check internal state and
	// throw an exception if not all trades
	// have been generated.
	//
	// Returns whether there is another HOLDING row to return.
	//
	bool GenerateNextHolding();

	//	Initialize next load unit and prepare it for 
	//	GenerateNextTrade/GenerateNextHolding calls.
	//	The first load unit doesn't have to be initialized.
	//
	//	Return whether the next load unit exists
	//
	bool InitNextLoadUnit();

	// Accessors for internal row structures.
	//
	const PTRADE_ROW			GetTradeRow() { return &m_TradeRow.m_Trade; }
	int							GetTradeHistoryRowCount() { return m_iTradeHistoryRowCount; }
	const PTRADE_HISTORY_ROW	GetTradeHistoryRow(int i) { return &m_TradeRow.m_TradeHistory[i]; }
	int							GetSettlementRowCount() { return m_iSettlementRowCount; }
	const PSETTLEMENT_ROW		GetSettlementRow() { return &m_TradeRow.m_Settlement; }
	int							GetCashTransactionRowCount() { return m_iCashTransactionRowCount; }
	const PCASH_TRANSACTION_ROW	GetCashTransactionRow() { return &m_TradeRow.m_CashTransaction; }
	const PHOLDING_ROW			GetHoldingRow() { return &m_HoldingRow; }
	int							GetHoldingHistoryRowCount() { return m_iHoldingHistoryRowCount; }
	const PHOLDING_HISTORY_ROW	GetHoldingHistoryRow(int i) { return &m_TradeRow.m_HoldingHistory[i]; }
	const PHOLDING_SUMMARY_ROW	GetHoldingSummaryRow() { return &m_HoldingSummaryRow; }

	bool						GenerateNextBrokerRecord() { return m_BrokerTable.GenerateNextRecord(); }
	const BROKER_ROW*			GetBrokerRow() { return m_BrokerTable.GetRow(); }
};

}	// namespace TPCE

#endif // TRADE_GEN_H
