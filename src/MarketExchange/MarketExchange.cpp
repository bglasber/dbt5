/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright (C) 2006 Rilson Nascimento
 *
 * 30 July 2006
 */

#include <transactions.h>

using namespace TPCE;

// worker thread
void *MarketWorkerThread(void* data)
{
	PMarketThreadParam pThrParam = reinterpret_cast<PMarketThreadParam>(data);

	CSocket sockDrv;
	sockDrv.SetSocketfd( pThrParam->iSockfd );	// client socket

	PTradeRequest pMessage = new TTradeRequest;
	memset(pMessage, 0, sizeof(TTradeRequest));   // zero the structure

	do
	{
		try
		{
			sockDrv.Receive( reinterpret_cast<void*>(pMessage),
					sizeof(TTradeRequest));
	
			// submit trade request
			pThrParam->pMarketExchange->m_pCMEE->SubmitTradeRequest( pMessage );
		}
		catch(CSocketErr *pErr)
		{
			sockDrv.CloseAccSocket();	// close connection

			ostringstream osErr;
			osErr<<endl<<"Trade Request not submitted to Market Exchange"
				<<endl<<"Error: "<<pErr->ErrorText()
				<<" at "<<"MarketExchange::MarketWorkerThread"<<endl;
			pThrParam->pMarketExchange->LogErrorMessage(osErr.str());
			delete pErr;

			// The socket is closed, break and let this thread die.
			break;
		}
	} while (true);

	delete pMessage;
	delete pThrParam;
	return NULL;
}

// entry point for worker thread
void EntryMarketWorkerThread(void* data)
{
	PMarketThreadParam pThrParam = reinterpret_cast<PMarketThreadParam>(data);

	pthread_t threadID; // thread ID
	pthread_attr_t threadAttribute; // thread attribute

	try
	{
		// initialize the attribute object
		int status = pthread_attr_init(&threadAttribute);
		if (status != 0)
		{
			throw new CThreadErr( CThreadErr::ERR_THREAD_ATTR_INIT );
		}
	
		// set the detachstate attribute to detached
		status = pthread_attr_setdetachstate(&threadAttribute,
				PTHREAD_CREATE_DETACHED);
		if (status != 0)
		{
			throw new CThreadErr( CThreadErr::ERR_THREAD_ATTR_DETACH );
		}
	
		// create the thread in the detached state
		status = pthread_create(&threadID, &threadAttribute,
				&MarketWorkerThread, data);
	
		if (status != 0)
		{
			throw new CThreadErr( CThreadErr::ERR_THREAD_CREATE );
		}
	}
	catch(CThreadErr *pErr)
	{
		// close recently accepted connection, to release threads
		close(pThrParam->iSockfd);

		ostringstream osErr;
		osErr<<endl<<"Error: "<<pErr->ErrorText()
		    <<" at "<<"MarketExchange::EntryMarketWorkerThread"<<endl
		    <<"accepted socket connection closed"<<endl;
		pThrParam->pMarketExchange->LogErrorMessage(osErr.str());
		delete pErr;
	}
}

// Constructor
CMarketExchange::CMarketExchange(char* szFileLoc, TIdent iConfiguredCustomerCount, TIdent iActiveCustomerCount,
					int iListenPort, char* szBHaddr, int iBHlistenPort,
					char* outputDirectory)
: m_iListenPort(iListenPort)
{
	char filename[1024];
	sprintf(filename, "%s/MarketExchange.log", outputDirectory);
	m_pLog = new CEGenLogger(eDriverEGenLoader, 0, filename, &m_fmt);

	sprintf(filename, "%s/MarketExchange_Error.log", outputDirectory);
	m_fLog.open(filename, ios::out);
	sprintf(filename, "%s/%s", outputDirectory, MEE_MIX_LOG_NAME);
	m_fMix.open(filename, ios::out);

	// Initialize MEESUT
	m_pCMEESUT = new CMEESUT(szBHaddr, iBHlistenPort, &m_fLog, &m_fMix,
			&m_LogLock, &m_MixLock);
	
	// Initialize MEE
	CInputFiles inputFiles;
	inputFiles.Initialize(eDriverEGenLoader, iConfiguredCustomerCount,
			iActiveCustomerCount, szFileLoc);
	m_pCMEE = new CMEE( 0, m_pCMEESUT, m_pLog, inputFiles, 1 );
	m_pCMEE->SetBaseTime();
}

// Destructor
CMarketExchange::~CMarketExchange()
{
	delete m_pCMEE;
	delete m_pSecurities;
	delete m_pCMEESUT;

	m_fMix.close();
	m_fLog.close();

	delete m_pLog;
}

// Listener
void CMarketExchange::Listener( void )
{
	int acc_socket;
	PMarketThreadParam pThrParam;
	
	m_Socket.Listen( m_iListenPort );

	while(true)
	{
		acc_socket = 0;
		try
		{
			acc_socket = m_Socket.Accept();
	
			// create new parameter structure
			pThrParam = new TMarketThreadParam;
			// zero the structure
			memset(pThrParam, 0, sizeof(TMarketThreadParam));
	
			pThrParam->iSockfd = acc_socket;
			pThrParam->pMarketExchange = this;
	
			// call entry point
			EntryMarketWorkerThread( reinterpret_cast<void*>(pThrParam) );
		}
		catch(CSocketErr *pErr)
		{
			ostringstream osErr;
			osErr<<endl<<"Problem to accept socket connection"
			     <<endl<<"Error: "<<pErr->ErrorText()
			     <<" at "<<"MarketExchange::Listener"<<endl;
			LogErrorMessage(osErr.str());
			delete pErr;
		}
	}

}


// LogErrorMessage
void CMarketExchange::LogErrorMessage( const string sErr )
{
	m_LogLock.lock();
	cout<<sErr;
	m_fLog<<sErr;
	m_fLog.flush();
	m_LogLock.unlock();
}