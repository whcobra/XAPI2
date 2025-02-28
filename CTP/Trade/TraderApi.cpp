﻿#include "stdafx.h"
#include "TraderApi.h"

#include "../include/QueueEnum.h"
#include "../include/QueueHeader.h"

#include "../include/ApiHeader.h"
#include "../include/ApiStruct.h"

#include "../include/toolkit.h"

#include "../QuantBox_Queue/MsgQueue.h"

#include "../TypeConvert.h"



#include <cstring>
#include <assert.h>
#include <cfloat>

#ifdef ENABLE_LICENSE
#include "../License/License.h"
#endif


void* __stdcall Query(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	// 由内部调用，不用检查是否为空
	CTraderApi* pApi = (CTraderApi*)pApi2;
	pApi->QueryInThread(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
	return nullptr;
}

void CTraderApi::QueryInThread(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	int iRet = 0;
	switch (type)
	{
	case E_Init:
		iRet = _Init();
		break;
	case E_ReqAuthenticateField:
		iRet = _ReqAuthenticate(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_ReqUserLoginField:
		iRet = _ReqUserLogin(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case E_SettlementInfoConfirmField:
		iRet = _ReqSettlementInfoConfirm(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;

	case QueryType::QueryType_ReqQryTradingAccount:
		iRet = _ReqQryTradingAccount(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case QueryType::QueryType_ReqQryInvestorPosition :
		iRet = _ReqQryInvestorPosition(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case QueryType::QueryType_ReqQryInstrument :
		iRet = _ReqQryInstrument(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case QueryType::QueryType_ReqQryInvestor :
		iRet = _ReqQryInvestor(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case QueryType::QueryType_ReqQryOrder :
		iRet = _ReqQryOrder(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case QueryType::QueryType_ReqQryTrade:
		iRet = _ReqQryTrade(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case QueryType::QueryType_ReqQryQuote:
		iRet = _ReqQryQuote(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	case QueryType::QueryType_ReqQrySettlementInfo:
		iRet = _ReqQrySettlementInfo(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		break;
	default:
		break;
	}

	if (0 == iRet)
	{
		//返回成功，填加到已发送池
		m_nSleep = 1;
	}
	else
	{
		m_msgQueue_Query->Input_Copy(type, pApi1, pApi2, double1, double2, ptr1, size1, ptr2, size2, ptr3, size3);
		//失败，按4的幂进行延时，但不超过1s
		m_nSleep *= 4;
		m_nSleep %= 1023;
	}
	this_thread::sleep_for(chrono::milliseconds(m_nSleep));
}

void CTraderApi::Register(void* pCallback, void* pClass)
{
	m_pClass = pClass;
	if (m_msgQueue == nullptr)
		return;

	m_msgQueue_Query->Register((void*)Query,this);
	m_msgQueue->Register(pCallback,this);
	if (pCallback)
	{
		m_msgQueue_Query->StartThread();
		m_msgQueue->StartThread();
	}
	else
	{
		m_msgQueue_Query->StopThread();
		m_msgQueue->StopThread();
	}
}

CTraderApi::CTraderApi(void)
{
	m_pApi = nullptr;
	m_lRequestID = 0;
	m_nSleep = 1;

	// 自己维护两个消息队列
	m_msgQueue = new CMsgQueue();
	m_msgQueue_Query = new CMsgQueue();

	m_msgQueue_Query->Register((void*)Query,this);
	m_msgQueue_Query->StartThread();

#ifdef ENABLE_LICENSE
	// 为了这个加密库，居然把编译选项由/MDd改成了/MTd,以后一定要想办法改回去
	m_pLicense = new CLicense();

	char szPath[MAX_PATH] = { 0 };
	m_pLicense->GetDllPathByFunctionName("XRequest", szPath);
	m_pLicense->SetLicensePath(szPath);
	// 这里选的是从文件中加载公钥，可以写死到资源或代码中，这样用户就没有那么容易自己生成公私钥对替换了
	string publicKeyString = m_pLicense->LoadStringFromFile(m_pLicense->m_PublicKeyPath);
	m_pLicense->SetPublicKeyString(publicKeyString.c_str());
#endif
}


CTraderApi::~CTraderApi(void)
{
	Disconnect();

#ifdef ENABLE_LICENSE
	if (m_pLicense == nullptr)
	{
		delete m_pLicense;
		m_pLicense = nullptr;
	}
#endif
}

bool CTraderApi::IsErrorRspInfo(const char* szSource, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));
	if (bRet)
	{
		ErrorField* pField = (ErrorField*)m_msgQueue->new_block(sizeof(ErrorField));

		pField->RawErrorID = pRspInfo->ErrorID;
		strcpy(pField->Text, pRspInfo->ErrorMsg);
		strcpy(pField->Source, szSource);

		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRtnError, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(ErrorField), nullptr, 0, nullptr, 0);
	}
	return bRet;
}

bool CTraderApi::IsErrorRspInfo(CThostFtdcRspInfoField *pRspInfo)
{
	bool bRet = ((pRspInfo) && (pRspInfo->ErrorID != 0));

	return bRet;
}

void CTraderApi::Connect(const string& szPath,
	ServerInfoField* pServerInfo,
	UserInfoField* pUserInfo,
	int count)
{
	m_szPath = szPath;
	memcpy(&m_ServerInfo, pServerInfo, sizeof(ServerInfoField));
	memcpy(&m_UserInfo, pUserInfo, sizeof(UserInfoField));

	m_msgQueue_Query->Input_NoCopy(RequestType::E_Init, m_msgQueue_Query, this, 0, 0,
		nullptr, 0, nullptr, 0, nullptr, 0);
}

int CTraderApi::_Init()
{
#ifdef ENABLE_LICENSE
	int err = m_pLicense->LoadIni();
	if (err != 0)
	{
		// 没有授权文件
		m_pLicense->CreateDefault();
		m_pLicense->AddUser(m_UserInfo.UserID, "*");
		err = m_pLicense->SaveIni();
	}

	if (err == 0)
	{
		// 如果签名信息不存在，当然就是试用
		err = m_pLicense->GetErrorCodeForSign();
	}

	if (err == 0)
	{
		err = m_pLicense->GetErrorCode();
	}

	if (err == 0)
	{
		err = m_pLicense->GetErrorCodeByAccount(m_UserInfo.UserID);
	}

	if (err != 0)
	{
		RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));

		pField->RawErrorID = err;
		strncpy(pField->Text, m_pLicense->GetErrorInfo(), sizeof(Char256Type));

		// >0的错误表示不严重，可以继续
		if (err > 0)
		{
			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Initialized, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
		}
		else if (err < 0)
		{
			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
			return 0;
		}
	}
#endif

	char *pszPath = new char[m_szPath.length() + 1024];
	srand((unsigned int)time(nullptr));
	sprintf(pszPath, "%s/%s/%s/Td/%d/", m_szPath.c_str(), m_ServerInfo.BrokerID, m_UserInfo.UserID, rand());
	makedirs(pszPath);

	m_pApi = CThostFtdcTraderApi::CreateFtdcTraderApi(pszPath);
	delete[] pszPath;

	m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Initialized, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	if (m_pApi)
	{
		m_pApi->RegisterSpi(this);

		//添加地址
		size_t len = strlen(m_ServerInfo.Address) + 1;
		char* buf = new char[len];
		strncpy(buf, m_ServerInfo.Address, len);

		char* token = strtok(buf, _QUANTBOX_SEPS_);
		while (token)
		{
			if (strlen(token)>0)
			{
				m_pApi->RegisterFront(token);
			}
			token = strtok(nullptr, _QUANTBOX_SEPS_);
		}
		delete[] buf;

		//如果空着，反而是从头开始接收了，所以这里要特别处理一下
		if (m_ServerInfo.PublicTopicResumeType < ResumeType::ResumeType_Undefined)
		{
			m_pApi->SubscribePublicTopic((THOST_TE_RESUME_TYPE)m_ServerInfo.PublicTopicResumeType);
		}
		else
		{
			m_pApi->SubscribePublicTopic(THOST_TERT_QUICK);
		}
		
		if (m_ServerInfo.PrivateTopicResumeType < ResumeType::ResumeType_Undefined)
		{
			m_pApi->SubscribePrivateTopic((THOST_TE_RESUME_TYPE)m_ServerInfo.PrivateTopicResumeType);
		}
		else
		{
			m_pApi->SubscribePrivateTopic(THOST_TERT_QUICK);
		}

		//初始化连接
		m_pApi->Init();
		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Connecting, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	}

	return 0;
}

void CTraderApi::OnFrontConnected()
{
	m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Connected, 0, nullptr, 0, nullptr, 0, nullptr, 0);

	//连接成功后自动请求认证或登录
	if (strlen(m_ServerInfo.AuthCode)>0
		&& strlen(m_ServerInfo.UserProductInfo)>0)
	{
		//填了认证码就先认证
		ReqAuthenticate();
	}
	else
	{
		ReqUserLogin();
	}
}

void CTraderApi::OnFrontDisconnected(int nReason)
{
	RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));

	//连接失败返回的信息是拼接而成，主要是为了统一输出
	pField->RawErrorID = nReason;
	GetOnFrontDisconnectedMsg(nReason, pField->Text);

	m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
}

void CTraderApi::ReqAuthenticate()
{
	CThostFtdcReqAuthenticateField* pBody = (CThostFtdcReqAuthenticateField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcReqAuthenticateField));

	strncpy(pBody->BrokerID, m_ServerInfo.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(pBody->UserID, m_UserInfo.UserID, sizeof(TThostFtdcInvestorIDType));
	strncpy(pBody->UserProductInfo, m_ServerInfo.UserProductInfo, sizeof(TThostFtdcProductInfoType));
	strncpy(pBody->AuthCode, m_ServerInfo.AuthCode, sizeof(TThostFtdcAuthCodeType));

	m_msgQueue_Query->Input_NoCopy(RequestType::E_ReqAuthenticateField, m_msgQueue_Query, this, 0, 0,
		pBody, sizeof(CThostFtdcReqAuthenticateField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqAuthenticate(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Authorizing, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	return m_pApi->ReqAuthenticate((CThostFtdcReqAuthenticateField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspAuthenticate(CThostFtdcRspAuthenticateField *pRspAuthenticateField, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo)
		&& pRspAuthenticateField)
	{
		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Authorized, 0, nullptr, 0, nullptr, 0, nullptr, 0);

		ReqUserLogin();
	}
	else
	{
		RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));

		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));

		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::ReqUserLogin()
{
	CThostFtdcReqUserLoginField* pBody = (CThostFtdcReqUserLoginField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcReqUserLoginField));

	strncpy(pBody->BrokerID, m_ServerInfo.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(pBody->UserID, m_UserInfo.UserID, sizeof(TThostFtdcInvestorIDType));
	strncpy(pBody->Password, m_UserInfo.Password, sizeof(TThostFtdcPasswordType));
	strncpy(pBody->UserProductInfo, m_ServerInfo.UserProductInfo, sizeof(TThostFtdcProductInfoType));

	m_msgQueue_Query->Input_NoCopy(RequestType::E_ReqUserLoginField, m_msgQueue_Query, this, 0, 0,
		pBody, sizeof(CThostFtdcReqUserLoginField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqUserLogin(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Logining, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	return m_pApi->ReqUserLogin((CThostFtdcReqUserLoginField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));

	if (!IsErrorRspInfo(pRspInfo)
		&& pRspUserLogin)
	{
		pField->TradingDay = GetDate(pRspUserLogin->TradingDay);
		pField->LoginTime = GetTime(pRspUserLogin->LoginTime);

		sprintf(pField->SessionID, "%d:%d", pRspUserLogin->FrontID, pRspUserLogin->SessionID);

		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Logined, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);

		// 记下登录信息，可能会用到
		memcpy(&m_RspUserLogin, pRspUserLogin, sizeof(CThostFtdcRspUserLoginField));
		m_nMaxOrderRef = atol(pRspUserLogin->MaxOrderRef);
		// 自己发单时ID从1开始，不能从0开始
		m_nMaxOrderRef = m_nMaxOrderRef>1 ? m_nMaxOrderRef : 1;
		ReqSettlementInfoConfirm();
		ReqQueryField body = { 0 };
		ReqQuery(QueryType::QueryType_ReqQryInvestor, &body);

		//TEST:测完要删
		//body.DateStart = 20151111;
		//ReqQuery(QueryType::ReqQrySettlementInfo, &body);
	}
	else
	{
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));

		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::ReqSettlementInfoConfirm()
{
	CThostFtdcSettlementInfoConfirmField* pBody = (CThostFtdcSettlementInfoConfirmField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcSettlementInfoConfirmField));

	strncpy(pBody->BrokerID, m_ServerInfo.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(pBody->InvestorID, m_UserInfo.UserID, sizeof(TThostFtdcInvestorIDType));

	m_msgQueue_Query->Input_NoCopy(RequestType::E_SettlementInfoConfirmField, m_msgQueue_Query, this, 0, 0,
		pBody, sizeof(CThostFtdcSettlementInfoConfirmField), nullptr, 0, nullptr, 0);
}

int CTraderApi::_ReqSettlementInfoConfirm(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Confirming, 0, nullptr, 0, nullptr, 0, nullptr, 0);
	return m_pApi->ReqSettlementInfoConfirm((CThostFtdcSettlementInfoConfirmField*)ptr1, ++m_lRequestID);
}

void CTraderApi::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo(pRspInfo)
		&& pSettlementInfoConfirm)
	{
		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Confirmed, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Done, 0, nullptr, 0, nullptr, 0, nullptr, 0);

		if (m_ServerInfo.PrivateTopicResumeType > ResumeType::ResumeType_Restart
			&& (m_ServerInfo.PrivateTopicResumeType<ResumeType::ResumeType_Undefined))
		{
			//ReqQryOrder();
			////ReqQryTrade();
			//ReqQryQuote();

			ReqQueryField body = { 0 };
			ReqQuery(QueryType::QueryType_ReqQryOrder, &body);
			ReqQuery(QueryType::QueryType_ReqQryQuote, &body);
		}
	}
	else
	{
		RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));

		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));

		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::Disconnect()
{
	if (m_msgQueue_Query)
	{
		m_msgQueue_Query->StopThread();
		m_msgQueue_Query->Register(nullptr,nullptr);
		m_msgQueue_Query->Clear();
		delete m_msgQueue_Query;
		m_msgQueue_Query = nullptr;
	}

	if(m_pApi)
	{
		m_pApi->RegisterSpi(nullptr);
		m_pApi->Release();
		m_pApi = nullptr;

		// 全清理，只留最后一个
		m_msgQueue->Clear();
		m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Disconnected, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		// 主动触发
		m_msgQueue->Process();
	}

	if (m_msgQueue)
	{
		m_msgQueue->StopThread();
		m_msgQueue->Register(nullptr,nullptr);
		m_msgQueue->Clear();
		delete m_msgQueue;
		m_msgQueue = nullptr;
	}

	m_lRequestID = 0;

	Clear();
}

void CTraderApi::Clear()
{
	for (unordered_map<string, OrderField*>::iterator it = m_id_platform_order.begin(); it != m_id_platform_order.end(); ++it)
		delete it->second;
	m_id_platform_order.clear();

	for (unordered_map<string, CThostFtdcOrderField*>::iterator it = m_id_api_order.begin(); it != m_id_api_order.end(); ++it)
		delete it->second;
	m_id_api_order.clear();

	for (unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.begin(); it != m_id_platform_quote.end(); ++it)
		delete it->second;
	m_id_platform_quote.clear();

	for (unordered_map<string, CThostFtdcQuoteField*>::iterator it = m_id_api_quote.begin(); it != m_id_api_quote.end(); ++it)
		delete it->second;
	m_id_api_quote.clear();

	for (unordered_map<string, PositionField*>::iterator it = m_id_platform_position.begin(); it != m_id_platform_position.end(); ++it)
		delete it->second;
	m_id_platform_position.clear();
}

char* CTraderApi::ReqOrderInsert(
	OrderField* pOrder,
	int count,
	char* pszLocalIDBuf)
{
	int OrderRef = -1;
	if (nullptr == m_pApi)
		return nullptr;

	CThostFtdcInputOrderField body = {0};

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));

	body.MinVolume = 1;
	body.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
	body.IsAutoSuspend = 0;
	body.UserForceClose = 0;
	body.IsSwapOrder = 0;

	//合约
	strncpy(body.InstrumentID, pOrder->InstrumentID, sizeof(TThostFtdcInstrumentIDType));
	strncpy(body.ExchangeID, pOrder->ExchangeID, sizeof(TThostFtdcExchangeIDType));
	//买卖
	body.Direction = OrderSide_2_TThostFtdcDirectionType(pOrder->Side);
	//开平
	body.CombOffsetFlag[0] = OpenCloseType_2_TThostFtdcOffsetFlagType(pOrder->OpenClose);
	//投保
	body.CombHedgeFlag[0] = HedgeFlagType_2_TThostFtdcHedgeFlagType(pOrder->HedgeFlag);
	//数量
	body.VolumeTotalOriginal = (int)pOrder->Qty;

	// 对于套利单，是用第一个参数的价格，还是用两个参数的价格差呢？
	body.LimitPrice = pOrder->Price;
	body.StopPrice = pOrder->StopPx;

	// 针对第二个进行处理，如果有第二个参数，认为是交易所套利单
	if (count>1)
	{
		body.CombOffsetFlag[1] = OpenCloseType_2_TThostFtdcOffsetFlagType(pOrder[1].OpenClose);
		body.CombHedgeFlag[1] = HedgeFlagType_2_TThostFtdcHedgeFlagType(pOrder[1].HedgeFlag);
		// 交易所的移仓换月功能，没有实测过
		body.IsSwapOrder = (body.CombOffsetFlag[0] != body.CombOffsetFlag[1]);
	}

	// 市价与限价
	switch (pOrder->Type)
	{
	case OrderType_Market:
	case OrderType_Stop:
	case OrderType_MarketOnClose:
	case OrderType_TrailingStop:
		body.OrderPriceType = THOST_FTDC_OPT_AnyPrice;
		body.TimeCondition = THOST_FTDC_TC_IOC;
		body.LimitPrice = 0;
		break;
	case OrderType_Limit:
	case OrderType_StopLimit:
	case OrderType_TrailingStopLimit:
	default:
		body.OrderPriceType = THOST_FTDC_OPT_LimitPrice;
		body.TimeCondition = THOST_FTDC_TC_GFD;
		break;
	}

	// IOC与FOK
	switch (pOrder->TimeInForce)
	{
	case TimeInForce_IOC:
		body.TimeCondition = THOST_FTDC_TC_IOC;
		body.VolumeCondition = THOST_FTDC_VC_AV;
		break;
	case TimeInForce_FOK:
		body.TimeCondition = THOST_FTDC_TC_IOC;
		body.VolumeCondition = THOST_FTDC_VC_CV;
		//body.MinVolume = body.VolumeTotalOriginal; // 这个地方必须加吗？
		break;
	default:
		body.VolumeCondition = THOST_FTDC_VC_AV;
		break;
	}

	// 条件单
	switch (pOrder->Type)
	{
	case OrderType_Stop:
	case OrderType_TrailingStop:
	case OrderType_StopLimit:
	case OrderType_TrailingStopLimit:
		// 条件单没有测试，先留空
		body.ContingentCondition = THOST_FTDC_CC_Immediately;
		break;
	default:
		body.ContingentCondition = THOST_FTDC_CC_Immediately;
		break;
	}

	int nRet = 0;
	{
		//可能报单太快，m_nMaxOrderRef还没有改变就提交了
		lock_guard<mutex> cl(m_csOrderRef);

		if (OrderRef < 0)
		{
			nRet = m_nMaxOrderRef;
			++m_nMaxOrderRef;
		}
		else
		{
			nRet = OrderRef;
		}
		sprintf(body.OrderRef, "%d", nRet);

		// 测试平台穿越速度，用完后需要注释掉
		//WriteLog("CTP:ReqOrderInsert:%s %d", body.InstrumentID, nRet);
#ifdef ENABLE_LICENSE
		int err = m_pLicense->GetErrorCodeByTrial();
		if (err<0)
		{
			//sprintf(m_orderInsert_Id, "%d", -5);

			//OrderField* pField = (OrderField*)m_msgQueue->new_block(sizeof(OrderField));
			//memcpy(pField, pOrder, sizeof(OrderField));
			//strcpy(pField->ID, m_orderInsert_Id);
			//strcpy(pField->LocalID, pField->ID);
			//m_id_platform_order.insert(pair<string, OrderField*>(pField->LocalID, pField));

			////RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));
			//pField->RawErrorID = err;
			//strncpy(pField->Text, m_pLicense->GetErrorInfo(), sizeof(Char256Type));

			//pField->Status = OrderStatus::OrderStatus_Rejected;
			//pField->ExecType = ExecType::ExecType_Rejected;

			//strncpy((char*)pszLocalIDBuf, m_orderInsert_Id, sizeof(OrderIDType));

			//// 这个问题的关键是先到队列还是先返回
			//m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnOrder, m_msgQueue, m_pClass, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
			
			return nullptr;
		}
#endif


		//不保存到队列，而是直接发送
		int n = m_pApi->ReqOrderInsert(&body, ++m_lRequestID);
		if (n < 0)
		{
			nRet = n;
			sprintf(m_orderInsert_Id, "%d", nRet);
		}
		else
		{
			// 用于各种情况下找到原订单，用于进行响应的通知
			sprintf(m_orderInsert_Id, "%d:%d:%d", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, nRet);

			OrderField* pField = (OrderField*)m_msgQueue->new_block(sizeof(OrderField));
			memcpy(pField, pOrder, sizeof(OrderField));
			strcpy(pField->ID, m_orderInsert_Id);
			strcpy(pField->LocalID, pField->ID);
			m_id_platform_order.insert(pair<string, OrderField*>(pField->LocalID, pField));

		}
		strncpy((char*)pszLocalIDBuf, m_orderInsert_Id, sizeof(OrderIDType));
	}

	return pszLocalIDBuf;
}

void CTraderApi::OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType orderId = { 0 };

	if (pInputOrder)
	{
		sprintf(orderId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputOrder->OrderRef);
	}
	else
	{
		IsErrorRspInfo("OnRspOrderInsert", pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		pField->ExecType = ExecType::ExecType_Rejected;
		pField->Status = OrderStatus::OrderStatus_Rejected;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnOrder, m_msgQueue, m_pClass, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType orderId = { 0 };

	if (pInputOrder)
	{
		sprintf(orderId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputOrder->OrderRef);
	}
	else
	{
		IsErrorRspInfo("OnErrRtnOrderInsert", pRspInfo, 0, true);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		pField->ExecType = ExecType::ExecType_Rejected;
		pField->Status = OrderStatus::OrderStatus_Rejected;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnOrder, m_msgQueue, m_pClass, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRtnTrade(CThostFtdcTradeField *pTrade)
{
	OnTrade(pTrade, 0, true);
}

char* CTraderApi::ReqOrderAction(OrderIDType* szIds, int count, char* pzsRtn)
{
	unordered_map<string, CThostFtdcOrderField*>::iterator it = m_id_api_order.find(szIds[0]);
	if (it == m_id_api_order.end())
	{
		sprintf(pzsRtn, "%d", -100);
	}
	else
	{
		// 找到了订单
		return ReqOrderAction(it->second, count, pzsRtn);
	}

	return pzsRtn;
}

int CTraderApi::ReqOrderAction(OrderField *pOrder, int count, OrderIDType* pOutput)
{
	/*if (nullptr == m_pApi)
		return 0;

	CThostFtdcInputOrderActionField body = { 0 };

	///经纪公司代码
	strncpy(body.BrokerID, pOrder->BrokerID, sizeof(TThostFtdcBrokerIDType));
	///投资者代码
	strncpy(body.InvestorID, pOrder->InvestorID, sizeof(TThostFtdcInvestorIDType));
	///报单引用
	strncpy(body.OrderRef, pOrder->OrderRef, sizeof(TThostFtdcOrderRefType));
	///前置编号
	body.FrontID = pOrder->FrontID;
	///会话编号
	body.SessionID = pOrder->SessionID;
	///交易所代码
	strncpy(body.ExchangeID, pOrder->ExchangeID, sizeof(TThostFtdcExchangeIDType));
	///报单编号
	strncpy(body.OrderSysID, pOrder->OrderSysID, sizeof(TThostFtdcOrderSysIDType));
	///操作标志
	body.ActionFlag = THOST_FTDC_AF_Delete;
	///合约代码
	strncpy(body.InstrumentID, pOrder->InstrumentID, sizeof(TThostFtdcInstrumentIDType));

	int nRet = m_pApi->ReqOrderAction(&body, ++m_lRequestID);
	if (nRet < 0)
	{
		sprintf(m_orderAction_Id, "%d", nRet);
	}
	else
	{
		memset(m_orderAction_Id, 0, sizeof(OrderIDType));
	}
	strncpy((char*)pOutput, m_orderAction_Id, sizeof(OrderIDType));


	return nRet;*/
	return 0;
}

char* CTraderApi::ReqOrderAction(CThostFtdcOrderField *pOrder, int count, char* pzsRtn)
{
	if (nullptr == m_pApi)
		return 0;

	CThostFtdcInputOrderActionField body = {0};

	///经纪公司代码
	strncpy(body.BrokerID, pOrder->BrokerID,sizeof(TThostFtdcBrokerIDType));
	///投资者代码
	strncpy(body.InvestorID, pOrder->InvestorID,sizeof(TThostFtdcInvestorIDType));
	///报单引用
	strncpy(body.OrderRef, pOrder->OrderRef,sizeof(TThostFtdcOrderRefType));
	///前置编号
	body.FrontID = pOrder->FrontID;
	///会话编号
	body.SessionID = pOrder->SessionID;
	///交易所代码
	strncpy(body.ExchangeID,pOrder->ExchangeID,sizeof(TThostFtdcExchangeIDType));
	///报单编号
	strncpy(body.OrderSysID,pOrder->OrderSysID,sizeof(TThostFtdcOrderSysIDType));
	///操作标志
	body.ActionFlag = THOST_FTDC_AF_Delete;
	///合约代码
	strncpy(body.InstrumentID, pOrder->InstrumentID,sizeof(TThostFtdcInstrumentIDType));

	int nRet = m_pApi->ReqOrderAction(&body, ++m_lRequestID);
	if (nRet < 0)
	{
		sprintf(m_orderAction_Id, "%d", nRet);
	}
	else
	{
		memset(m_orderAction_Id, 0, sizeof(OrderIDType));
	}
	strncpy(pzsRtn, m_orderAction_Id, sizeof(OrderIDType));

	return pzsRtn;
}

void CTraderApi::OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType orderId = { 0 };
	if (pInputOrderAction)
	{
		sprintf(orderId, "%d:%d:%s", pInputOrderAction->FrontID, pInputOrderAction->SessionID, pInputOrderAction->OrderRef);
	}
	else
	{
		IsErrorRspInfo("OnRspOrderAction", pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		strcpy(pField->ID, orderId);
		strcpy(pField->LocalID, pField->ID);
		pField->ExecType = ExecType::ExecType_CancelReject;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnOrder, m_msgQueue, m_pClass, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnOrderAction(CThostFtdcOrderActionField *pOrderAction, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType orderId = { 0 };
	if (pOrderAction)
	{
		sprintf(orderId, "%d:%d:%s", pOrderAction->FrontID, pOrderAction->SessionID, pOrderAction->OrderRef);
	}
	else
	{
		IsErrorRspInfo("OnErrRtnOrderAction", pRspInfo, 0, true);
	}

	unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
	if (it == m_id_platform_order.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		OrderField* pField = it->second;
		strcpy(pField->ID, orderId);
		strcpy(pField->LocalID, pField->ID);
		pField->ExecType = ExecType::ExecType_CancelReject;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnOrder, m_msgQueue, m_pClass, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRtnOrder(CThostFtdcOrderField *pOrder)
{
	OnOrder(pOrder, 0, true);
}

char* CTraderApi::ReqQuoteInsert(
	QuoteField* pQuote,
	OrderIDType* pAskRef,
	OrderIDType* pBidRef)
{
	int QuoteRef = -1;
	if (nullptr == m_pApi)
		return nullptr;

	CThostFtdcInputQuoteField body = {0};

	strcpy(body.BrokerID, m_RspUserLogin.BrokerID);
	strcpy(body.InvestorID, m_RspUserLogin.UserID);

	//合约,目前只从订单1中取
	strncpy(body.InstrumentID, pQuote->InstrumentID, sizeof(TThostFtdcInstrumentIDType));
	strncpy(body.ExchangeID, pQuote->ExchangeID, sizeof(TThostFtdcExchangeIDType));
	//开平
	body.AskOffsetFlag = OpenCloseType_2_TThostFtdcOffsetFlagType(pQuote->AskOpenClose);
	body.BidOffsetFlag = OpenCloseType_2_TThostFtdcOffsetFlagType(pQuote->BidOpenClose);
	//投保
	body.AskHedgeFlag = HedgeFlagType_2_TThostFtdcHedgeFlagType(pQuote->AskHedgeFlag);
	body.BidHedgeFlag = HedgeFlagType_2_TThostFtdcHedgeFlagType(pQuote->BidHedgeFlag);

	//价格
	body.AskPrice = pQuote->AskPrice;
	body.BidPrice = pQuote->BidPrice;

	//数量
	body.AskVolume = (int)pQuote->AskQty;
	body.BidVolume = (int)pQuote->BidQty;

	strncpy(body.ForQuoteSysID, pQuote->QuoteReqID, sizeof(TThostFtdcOrderSysIDType));

	int nRet = 0;
	{
		//可能报单太快，m_nMaxOrderRef还没有改变就提交了
		lock_guard<mutex> cl(m_csOrderRef);

		if (QuoteRef < 0)
		{
			nRet = m_nMaxOrderRef;
			sprintf(body.QuoteRef, "%d", m_nMaxOrderRef);
			sprintf(body.AskOrderRef, "%d", m_nMaxOrderRef);
			sprintf(body.BidOrderRef, "%d", ++m_nMaxOrderRef);
			++m_nMaxOrderRef;
		}
		else
		{
			nRet = QuoteRef;
			sprintf(body.QuoteRef, "%d", QuoteRef);
			sprintf(body.AskOrderRef, "%d", QuoteRef);
			sprintf(body.BidOrderRef, "%d", ++QuoteRef);
			++QuoteRef;
		}

		//不保存到队列，而是直接发送
		int n = m_pApi->ReqQuoteInsert(&body, ++m_lRequestID);
		if (n < 0)
		{
			nRet = n;
			return nullptr;
		}
		else
		{
			sprintf(m_orderInsert_Id, "%d:%d:%d", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, nRet);

			QuoteField* pField = (QuoteField*)m_msgQueue->new_block(sizeof(QuoteField));
			memcpy(pField, pQuote, sizeof(QuoteField));
			strcpy(pField->ID, m_orderInsert_Id);
			strcpy(pField->LocalID, pField->ID);
			strcpy(pField->AskID, m_orderInsert_Id);
			sprintf(pField->BidID, "%d:%d:%d", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, nRet + 1);

			m_id_platform_quote.insert(pair<string, QuoteField*>(m_orderInsert_Id, pField));
		}
	}

	return m_orderInsert_Id;
}

void CTraderApi::OnRspQuoteInsert(CThostFtdcInputQuoteField *pInputQuote, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType quoteId = { 0 };

	if (pInputQuote)
	{
		sprintf(quoteId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputQuote->QuoteRef);
	}
	else
	{
		IsErrorRspInfo("OnRspQuoteInsert", pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		pField->ExecType = ExecType::ExecType_Rejected;
		pField->Status = OrderStatus::OrderStatus_Rejected;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnQuote, m_msgQueue, m_pClass, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnQuoteInsert(CThostFtdcInputQuoteField *pInputQuote, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType quoteId = { 0 };

	if (pInputQuote)
	{
		sprintf(quoteId, "%d:%d:%s", m_RspUserLogin.FrontID, m_RspUserLogin.SessionID, pInputQuote->QuoteRef);
	}
	else
	{
		IsErrorRspInfo("",pRspInfo, 0, true);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		pField->ExecType = ExecType::ExecType_Rejected;
		pField->Status = OrderStatus::OrderStatus_Rejected;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnQuote, m_msgQueue, m_pClass, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnRtnQuote(CThostFtdcQuoteField *pQuote)
{
	OnQuote(pQuote, 0, true);
}

int CTraderApi::ReqQuoteAction(const string& szId,OrderIDType* pOutput)
{
	unordered_map<string, CThostFtdcQuoteField*>::iterator it = m_id_api_quote.find(szId);
	if (it == m_id_api_quote.end())
	{
		sprintf((char*)pOutput, "%d", -100);
		return -100;
	}
	else
	{
		// 找到了订单
		ReqQuoteAction(it->second,pOutput);
	}
	return 0;
}

int CTraderApi::ReqQuoteAction(CThostFtdcQuoteField *pQuote, OrderIDType* pOutput)
{
	if (nullptr == m_pApi)
		return 0;

	CThostFtdcInputQuoteActionField body = {0};

	///经纪公司代码
	strcpy(body.BrokerID, pQuote->BrokerID);
	///投资者代码
	strcpy(body.InvestorID, pQuote->InvestorID);
	///报单引用
	strcpy(body.QuoteRef, pQuote->QuoteRef);
	///前置编号
	body.FrontID = pQuote->FrontID;
	///会话编号
	body.SessionID = pQuote->SessionID;
	///交易所代码
	strcpy(body.ExchangeID, pQuote->ExchangeID);
	///报单编号
	strcpy(body.QuoteSysID, pQuote->QuoteSysID);
	///操作标志
	body.ActionFlag = THOST_FTDC_AF_Delete;
	///合约代码
	strcpy(body.InstrumentID, pQuote->InstrumentID);

	int nRet = m_pApi->ReqQuoteAction(&body, ++m_lRequestID);
	if (nRet < 0)
	{
		sprintf(m_orderAction_Id, "%d", nRet);
	}
	else
	{
		memset(m_orderAction_Id, 0, sizeof(OrderIDType));
	}
	strncpy((char*)pOutput, m_orderAction_Id, sizeof(OrderIDType));

	return nRet;
}

void CTraderApi::OnRspQuoteAction(CThostFtdcInputQuoteActionField *pInputQuoteAction, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	OrderIDType quoteId = { 0 };
	if (pInputQuoteAction)
	{
		sprintf(quoteId, "%d:%d:%s", pInputQuoteAction->FrontID, pInputQuoteAction->SessionID, pInputQuoteAction->QuoteRef);
	}
	else
	{
		IsErrorRspInfo("OnRspQuoteAction", pRspInfo, nRequestID, bIsLast);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		strcpy(pField->ID, quoteId);
		strcpy(pField->LocalID, pField->ID);
		//sprintf(pField->AskID, "%d:%d:%s", pInputQuoteAction->FrontID, pInputQuoteAction->SessionID, pInputQuoteAction->);
		//sprintf(pField->BidID, "%d:%d:%s", pInputQuoteAction->FrontID, pInputQuoteAction->SessionID, pInputQuoteAction->QuoteRef);
		pField->ExecType = ExecType::ExecType_CancelReject;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnQuote, m_msgQueue, m_pClass, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

void CTraderApi::OnErrRtnQuoteAction(CThostFtdcQuoteActionField *pQuoteAction, CThostFtdcRspInfoField *pRspInfo)
{
	OrderIDType quoteId = { 0 };

	if (pQuoteAction)
	{
		sprintf(quoteId, "%d:%d:%s", pQuoteAction->FrontID, pQuoteAction->SessionID, pQuoteAction->QuoteRef);
	}
	else
	{
		IsErrorRspInfo("OnRspQuoteAction",pRspInfo, 0, true);
	}

	unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
	if (it == m_id_platform_quote.end())
	{
		// 没找到？不应当，这表示出错了
		//assert(false);
	}
	else
	{
		// 找到了，要更新状态
		// 得使用上次的状态
		QuoteField* pField = it->second;
		strcpy(pField->ID, quoteId);
		strcpy(pField->LocalID, pField->ID);
		pField->ExecType = ExecType::ExecType_CancelReject;
		pField->RawErrorID = pRspInfo->ErrorID;
		strncpy(pField->Text, pRspInfo->ErrorMsg, sizeof(Char256Type));
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnQuote, m_msgQueue, m_pClass, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
	}
}

//void CTraderApi::ReqQryTradingAccount()
//{
//	CThostFtdcQryTradingAccountField* pBody = (CThostFtdcQryTradingAccountField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcQryTradingAccountField));
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));
//
//	m_msgQueue_Query->Input_NoCopy(RequestType::E_QryTradingAccountField, m_msgQueue_Query, this, 0, 0,
//		pBody, sizeof(CThostFtdcQryTradingAccountField), nullptr, 0, nullptr, 0);
//}

int CTraderApi::_ReqQryTradingAccount(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	CThostFtdcQryTradingAccountField body = { 0 };

	strcpy(body.BrokerID, m_RspUserLogin.BrokerID);
	strcpy(body.InvestorID, m_RspUserLogin.UserID);

	return m_pApi->ReqQryTradingAccount(&body, ++m_lRequestID);
}

void CTraderApi::OnRspQryTradingAccount(CThostFtdcTradingAccountField *pTradingAccount, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQryTradingAccount",pRspInfo, nRequestID, bIsLast))
	{
		if (pTradingAccount)
		{
			AccountField* pField = (AccountField*)m_msgQueue->new_block(sizeof(AccountField));

			strcpy(pField->AccountID, pTradingAccount->AccountID);
			pField->PreBalance = pTradingAccount->PreBalance;
			pField->CurrMargin = pTradingAccount->CurrMargin;
			pField->Commission = pTradingAccount->Commission;
			pField->CloseProfit = pTradingAccount->CloseProfit;
			pField->PositionProfit = pTradingAccount->PositionProfit;
			pField->Balance = pTradingAccount->Balance;
			pField->Available = pTradingAccount->Available;

			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQryTradingAccount, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(AccountField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQryTradingAccount, m_msgQueue, m_pClass, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}

//void CTraderApi::ReqQryInvestorPosition(const string& szInstrumentId, const string& szExchange)
//{
//	CThostFtdcQryInvestorPositionField* pBody = (CThostFtdcQryInvestorPositionField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcQryInvestorPositionField));
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));
//	strncpy(pBody->InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));
//
//	m_msgQueue_Query->Input_NoCopy(RequestType::E_QryInvestorPositionField, m_msgQueue_Query, this, 0, 0,
//		pBody, sizeof(CThostFtdcQryInvestorPositionField), nullptr, 0, nullptr, 0);
//}

int CTraderApi::_ReqQryInvestorPosition(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	ReqQueryField* pQuery = (ReqQueryField*)ptr1;

	CThostFtdcQryInvestorPositionField body = { 0 };

	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));
	strncpy(body.InstrumentID, pQuery->InstrumentID, sizeof(TThostFtdcInstrumentIDType));

	return m_pApi->ReqQryInvestorPosition(&body, ++m_lRequestID);
}

// 如果是请求查询，就将数据全部返回
// 如果是后期的成交回报，就只返回更新的记录
// 对于中金所，同时有今昨两天的持仓时，只返回今天的两条多空数据
// 对于上期所，目前没条件测，当成是也只有两条
void CTraderApi::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQryInvestorPosition", pRspInfo, nRequestID, bIsLast))
	{
		if (pInvestorPosition)
		{
			PositionIDType positionId = { 0 };
			sprintf(positionId, "%s:%s:%d:%c",
				pInvestorPosition->ExchangeID,
				pInvestorPosition->InstrumentID,
				TThostFtdcPosiDirectionType_2_PositionSide(pInvestorPosition->PosiDirection),
				pInvestorPosition->HedgeFlag);

			PositionField* pField = nullptr;
			unordered_map<string, PositionField*>::iterator it = m_id_platform_position.find(positionId);
			if (it == m_id_platform_position.end())
			{
				pField = (PositionField*)m_msgQueue->new_block(sizeof(PositionField));

				//sprintf(pField->Symbol, "%s.%s", pInvestorPosition->InstrumentID, pInvestorPosition->ExchangeID);
				strcpy(pField->Symbol, pInvestorPosition->InstrumentID);
				strcpy(pField->InstrumentID, pInvestorPosition->InstrumentID);
				strcpy(pField->ExchangeID, pInvestorPosition->ExchangeID);
				pField->Side = TThostFtdcPosiDirectionType_2_PositionSide(pInvestorPosition->PosiDirection);
				pField->HedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pInvestorPosition->HedgeFlag);

				m_id_platform_position.insert(pair<string, PositionField*>(positionId, pField));
			}
			else
			{
				pField = it->second;
			}

			pField->Position = pInvestorPosition->Position;
			pField->TodayPosition = pInvestorPosition->TodayPosition;
			pField->HistoryPosition = pInvestorPosition->Position - pInvestorPosition->TodayPosition;

			// 等数据收集全了再遍历通知一次，为何要这样做？因为今昨是两条记录，但我记在一个里面
			if (bIsLast)
			{
				int cnt = 0;
				size_t count = m_id_platform_position.size();
				for (unordered_map<string, PositionField*>::iterator iter = m_id_platform_position.begin(); iter != m_id_platform_position.end(); iter++)
				{
					++cnt;
					m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRspQryInvestorPosition, m_msgQueue, m_pClass, cnt == count, 0, iter->second, sizeof(PositionField), nullptr, 0, nullptr, 0);
				}
			}
			//XRespone(ResponeType::OnRspQryInvestorPosition, m_msgQueue, this, bIsLast, 0, pField, sizeof(PositionField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRspQryInvestorPosition, m_msgQueue, m_pClass, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}

void CTraderApi::ReqQuery(QueryType type, ReqQueryField* pQuery)
{
	m_msgQueue_Query->Input_Copy(type, m_msgQueue_Query, this, 0, 0,
		pQuery, sizeof(ReqQueryField), nullptr, 0, nullptr, 0);
}

//void CTraderApi::ReqQryInstrument(ReqQueryField* pQuery)
//{
//	CThostFtdcQryInstrumentField* pBody = (CThostFtdcQryInstrumentField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcQryInstrumentField));
//
//	//strncpy(pBody->InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));
//	//strncpy(pBody->ExchangeID, szExchange.c_str(), sizeof(TThostFtdcExchangeIDType));
//
//	m_msgQueue_Query->Input_NoCopy(RequestType::E_QryInstrumentField, m_msgQueue_Query, this, 0, 0,
//		pBody, sizeof(CThostFtdcQryInstrumentField), nullptr, 0, nullptr, 0);
//}

int CTraderApi::_ReqQryInstrument(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	ReqQueryField* pQuery = (ReqQueryField*)ptr1;

	CThostFtdcQryInstrumentField body = {0};
	strncpy(body.InstrumentID, pQuery->InstrumentID, sizeof(TThostFtdcInstrumentIDType));
	strncpy(body.ExchangeID, pQuery->ExchangeID, sizeof(TThostFtdcExchangeIDType));

	return m_pApi->ReqQryInstrument(&body, ++m_lRequestID);
}

void CTraderApi::OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQryInstrument", pRspInfo, nRequestID, bIsLast))
	{
		if (pInstrument)
		{
			InstrumentField* pField = (InstrumentField*)m_msgQueue->new_block(sizeof(InstrumentField));

			strncpy(pField->InstrumentID, pInstrument->InstrumentID, sizeof(InstrumentIDType));
			strncpy(pField->ExchangeID, pInstrument->ExchangeID, sizeof(ExchangeIDType));

			strncpy(pField->Symbol, pInstrument->InstrumentID, sizeof(SymbolType));
			strncpy(pField->ProductID, pInstrument->ProductID, sizeof(InstrumentIDType));

			strncpy(pField->InstrumentName, pInstrument->InstrumentName, sizeof(InstrumentNameType));
			pField->Type = CThostFtdcInstrumentField_2_InstrumentType(pInstrument);
			pField->VolumeMultiple = pInstrument->VolumeMultiple;
			pField->PriceTick = pInstrument->PriceTick;
			pField->ExpireDate = GetDate(pInstrument->ExpireDate);
			pField->OptionsType = TThostFtdcOptionsTypeType_2_PutCall(pInstrument->OptionsType);
			pField->StrikePrice = (pInstrument->StrikePrice < DBL_EPSILON || pInstrument->StrikePrice == DBL_MAX) ? 0 : pInstrument->StrikePrice;
			strcpy(pField->UnderlyingInstrID, pInstrument->UnderlyingInstrID);

			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQryInstrument, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(InstrumentField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQryInstrument, m_msgQueue, m_pClass, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}

//void CTraderApi::ReqQryInstrumentCommissionRate(const string& szInstrumentId)
//{
//	CThostFtdcQryInstrumentCommissionRateField body = {0};
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));
//	strncpy(pBody->InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));
//}
//
//void CTraderApi::OnRspQryInstrumentCommissionRate(CThostFtdcInstrumentCommissionRateField *pInstrumentCommissionRate, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
//{
//
//}

//void CTraderApi::ReqQryInstrumentMarginRate(const string& szInstrumentId,TThostFtdcHedgeFlagType HedgeFlag)
//{
//	CThostFtdcQryInstrumentMarginRateField body = {0};
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID,sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID,sizeof(TThostFtdcInvestorIDType));
//	strncpy(pBody->InstrumentID,szInstrumentId.c_str(),sizeof(TThostFtdcInstrumentIDType));
//	pBody->HedgeFlag = HedgeFlag;
//
//	//AddToSendQueue(pRequest);
//}

//void CTraderApi::OnRspQryInstrumentMarginRate(CThostFtdcInstrumentMarginRateField *pInstrumentMarginRate, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
//{
//
//}
//


int CTraderApi::_ReqQrySettlementInfo(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	ReqQueryField* pQuery = (ReqQueryField*)ptr1;

	CThostFtdcQrySettlementInfoField body = { 0 };
	strncpy(body.BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
	strncpy(body.InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));
	sprintf_s(body.TradingDay, "%d", pQuery->DateStart);

	return m_pApi->ReqQrySettlementInfo(&body, ++m_lRequestID);
}

vector<CThostFtdcSettlementInfoField *> vct_SettlementInfo;

void CTraderApi::OnRspQrySettlementInfo(CThostFtdcSettlementInfoField *pSettlementInfo, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQrySettlementInfo", pRspInfo, nRequestID, bIsLast))
	{
		if (pSettlementInfo)
		{
			CThostFtdcSettlementInfoField* pField = new CThostFtdcSettlementInfoField;
			memcpy(pField, pSettlementInfo, sizeof(CThostFtdcSettlementInfoField));
			vct_SettlementInfo.push_back(pField);

			if (bIsLast)
			{
				int len = 0;
				for (vector<CThostFtdcSettlementInfoField *>::iterator it = vct_SettlementInfo.begin(); it != vct_SettlementInfo.end(); ++it)
				{
					CThostFtdcSettlementInfoField* p = *it;
					len += sizeof(p->Content);
				}

				SettlementInfoField* pField = (SettlementInfoField*)m_msgQueue->new_block(sizeof(SettlementInfoField)+len);
				pField->TradingDay = atoi(pSettlementInfo->TradingDay);
				pField->Size = sizeof(SettlementInfoField)+len;
				char* pContent = (((char*)pField) + sizeof(SettlementInfoField));
				memset(pContent, 0, len);
				for (vector<CThostFtdcSettlementInfoField *>::iterator it = vct_SettlementInfo.begin(); it != vct_SettlementInfo.end(); ++it)
				{
					CThostFtdcSettlementInfoField* p = *it;
					strcat(pContent, p->Content);
				}
				m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQrySettlementInfo, m_msgQueue, this, bIsLast, 0, pField, pField->Size, nullptr, 0, nullptr, 0);
			}
			//SettlementInfoField field = { 0 };
			//strncpy(field.TradingDay, pSettlementInfo->TradingDay, sizeof(TThostFtdcDateType));
			//strncpy(field.Content, pSettlementInfo->Content, sizeof(TThostFtdcContentType));

			//m_msgQueue->Input_Copy(ResponeType::OnRspQrySettlementInfo, m_msgQueue, this, bIsLast, 0, &field, sizeof(SettlementInfoField), nullptr, 0, nullptr, 0);
		}
		else
		{
			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQrySettlementInfo, m_msgQueue, this, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}

void CTraderApi::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	IsErrorRspInfo("OnRspError", pRspInfo, nRequestID, bIsLast);
}

//void CTraderApi::ReqQryOrder()
//{
//	CThostFtdcQryOrderField* pBody = (CThostFtdcQryOrderField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcQryOrderField));
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));
//
//	m_msgQueue_Query->Input_NoCopy(RequestType::E_QryOrderField, m_msgQueue_Query, this, 0, 0,
//		pBody, sizeof(CThostFtdcQryOrderField), nullptr, 0, nullptr, 0);
//}

int CTraderApi::_ReqQryOrder(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	CThostFtdcQryOrderField body = { 0 };

	strcpy(body.BrokerID, m_RspUserLogin.BrokerID);
	strcpy(body.InvestorID, m_RspUserLogin.UserID);

	return m_pApi->ReqQryOrder(&body, ++m_lRequestID);
}

void CTraderApi::OnOrder(CThostFtdcOrderField *pOrder, int nRequestID, bool bIsLast)
{
	if (nullptr == pOrder)
		return;

	OrderIDType orderId = { 0 };
	sprintf(orderId, "%d:%d:%s", pOrder->FrontID, pOrder->SessionID, pOrder->OrderRef);
	OrderIDType orderSydId = { 0 };

	{
		// 保存原始订单信息，用于撤单

		unordered_map<string, CThostFtdcOrderField*>::iterator it = m_id_api_order.find(orderId);
		if (it == m_id_api_order.end())
		{
			// 找不到此订单，表示是新单
			CThostFtdcOrderField* pField = new CThostFtdcOrderField();
			memcpy(pField, pOrder, sizeof(CThostFtdcOrderField));
			m_id_api_order.insert(pair<string, CThostFtdcOrderField*>(orderId, pField));
		}
		else
		{
			// 找到了订单
			// 需要再复制保存最后一次的状态，还是只要第一次的用于撤单即可？记下，这样最后好比较
			CThostFtdcOrderField* pField = it->second;
			memcpy(pField, pOrder, sizeof(CThostFtdcOrderField));
		}

		// 保存SysID用于定义成交回报与订单
		sprintf(orderSydId, "%s:%s:%s", pOrder->ExchangeID, pOrder->InstrumentID, pOrder->OrderSysID);
		m_sysId_orderId.insert(pair<string, string>(orderSydId, orderId));
	}

	{
		// 从API的订单转换成自己的结构体
		if (nRequestID == 0)
		{
			OrderField* pField = nullptr;
			unordered_map<string, OrderField*>::iterator it = m_id_platform_order.find(orderId);
			if (it == m_id_platform_order.end())
			{
				pField = (OrderField*)m_msgQueue->new_block(sizeof(OrderField));

				// 开盘时发单信息还没有，所以找不到对应的单子，需要进行Order的恢复
				CThostFtdcOrderField_2_OrderField_0(orderId, pOrder, pField);

				// 添加到map中，用于其它工具的读取，撤单失败时的再通知等
				m_id_platform_order.insert(pair<string, OrderField*>(orderId, pField));
			}
			else
			{
				pField = it->second;
				strcpy(pField->ID, orderId);
				strcpy(pField->LocalID, pField->ID);
				pField->LeavesQty = pOrder->VolumeTotal;
				pField->Price = pOrder->LimitPrice;
				pField->Status = CThostFtdcOrderField_2_OrderStatus(pOrder);
				pField->ExecType = CThostFtdcOrderField_2_ExecType(pOrder);
				strcpy(pField->OrderID, pOrder->OrderSysID);
				strncpy(pField->Text, pOrder->StatusMsg, sizeof(Char256Type));
			}

			m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnOrder, m_msgQueue, m_pClass, 0, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);
		}
		else
		{
			OrderField* pField = nullptr;
			pField = (OrderField*)m_msgQueue->new_block(sizeof(OrderField));

			// 开盘时发单信息还没有，所以找不到对应的单子，需要进行Order的恢复
			CThostFtdcOrderField_2_OrderField_0(orderId, pOrder, pField);

			// 添加到map中，用于其它工具的读取，撤单失败时的再通知等
			//m_id_platform_order.insert(pair<string, OrderField*>(orderId, pField));

			m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRspQryOrder, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(OrderField), nullptr, 0, nullptr, 0);

			m_msgQueue->delete_block(pField);
		}
	}
}

void CTraderApi::OnRspQryOrder(CThostFtdcOrderField *pOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQryOrder", pRspInfo, nRequestID, bIsLast))
	{
		OnOrder(pOrder, nRequestID, bIsLast);
	}
}

//void CTraderApi::ReqQryTrade()
//{
//	CThostFtdcQryTradeField* pBody = (CThostFtdcQryTradeField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcQryTradeField));
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));
//
//	m_msgQueue_Query->Input_NoCopy(RequestType::E_QryTradeField, m_msgQueue_Query, this, 0, 0,
//		pBody, sizeof(CThostFtdcQryTradeField), nullptr, 0, nullptr, 0);
//}

int CTraderApi::_ReqQryTrade(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	CThostFtdcQryTradeField body = { 0 };

	strcpy(body.BrokerID, m_RspUserLogin.BrokerID);
	strcpy(body.InvestorID, m_RspUserLogin.UserID);

	return m_pApi->ReqQryTrade(&body, ++m_lRequestID);
}

void CTraderApi::OnTrade(CThostFtdcTradeField *pTrade, int nRequestID, bool bIsLast)
{
	if (nullptr == pTrade)
		return;

	TradeField* pField = (TradeField*)m_msgQueue->new_block(sizeof(TradeField));
	strcpy(pField->InstrumentID, pTrade->InstrumentID);
	strcpy(pField->ExchangeID, pTrade->ExchangeID);
	pField->Side = TThostFtdcDirectionType_2_OrderSide(pTrade->Direction);
	pField->Qty = pTrade->Volume;
	pField->Price = pTrade->Price;
	pField->OpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pTrade->OffsetFlag);
	pField->HedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pTrade->HedgeFlag);
	pField->Commission = 0;//TODO收续费以后要计算出来
	pField->Time = GetTime(pTrade->TradeTime);
	strcpy(pField->TradeID, pTrade->TradeID);

	if (nRequestID == 0)
	{
		OrderIDType orderSysId = { 0 };
		sprintf(orderSysId, "%s:%s:%s", pTrade->ExchangeID, pTrade->InstrumentID, pTrade->OrderSysID);
		unordered_map<string, string>::iterator it = m_sysId_orderId.find(orderSysId);
		if (it == m_sysId_orderId.end())
		{
			// 此成交找不到对应的报单
			//assert(false);
		}
		else
		{
			// 找到对应的报单
			strcpy(pField->ID, it->second.c_str());

			m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnTrade, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(TradeField), nullptr, 0, nullptr, 0);

			unordered_map<string, OrderField*>::iterator it2 = m_id_platform_order.find(it->second);
			if (it2 == m_id_platform_order.end())
			{
				// 此成交找不到对应的报单
				//assert(false);
			}
			else
			{
				// 更新订单的状态
				// 是否要通知接口
			}

			OnTrade(pField);
		}
	}
	else
	{
		m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRspQryTrade, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(TradeField), nullptr, 0, nullptr, 0);
	}

	// 清理内存
	m_msgQueue->delete_block(pField);
}

void CTraderApi::OnTrade(TradeField *pTrade)
{
	PositionIDType positionId = { 0 };
	sprintf(positionId, "%s:%s:%d:%c",
		pTrade->ExchangeID, pTrade->InstrumentID, TradeField_2_PositionSide(pTrade), pTrade->HedgeFlag);

	PositionField* pField = nullptr;
	unordered_map<string, PositionField*>::iterator it = m_id_platform_position.find(positionId);
	if (it == m_id_platform_position.end())
	{
		pField = (PositionField*)m_msgQueue->new_block(sizeof(PositionField));

		strcpy(pField->Symbol, pTrade->InstrumentID);
		strcpy(pField->InstrumentID, pTrade->InstrumentID);
		strcpy(pField->ExchangeID, pTrade->ExchangeID);
		pField->Side = TradeField_2_PositionSide(pTrade);
		pField->HedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pTrade->HedgeFlag);

		m_id_platform_position.insert(pair<string, PositionField*>(positionId, pField));
	}
	else
	{
		pField = it->second;
	}

	if (pTrade->OpenClose == OpenCloseType::OpenCloseType_Open)
	{
		pField->Position += pTrade->Qty;
		pField->TodayPosition += pTrade->Qty;
	}
	else
	{
		pField->Position -= pTrade->Qty;
		if (pTrade->OpenClose == OpenCloseType::OpenCloseType_CloseToday)
		{
			pField->TodayPosition -= pTrade->Qty;
		}
		else
		{
			pField->HistoryPosition -= pTrade->Qty;
			// 如果昨天的被减成负数，从今天开始继续减
			if (pField->HistoryPosition<0)
			{
				pField->TodayPosition += pField->HistoryPosition;
				pField->HistoryPosition = 0;
			}
		}

		// 计算错误，直接重新查询
		if (pField->Position < 0 || pField->TodayPosition < 0 || pField->HistoryPosition < 0)
		{
			//ReqQryInvestorPosition("", "");
			ReqQueryField body = { 0 };
			ReqQuery(QueryType::QueryType_ReqQryInvestorPosition, &body);
			return;
		}
	}

	m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRspQryInvestorPosition, m_msgQueue, m_pClass, false, 0, pField, sizeof(PositionField), nullptr, 0, nullptr, 0);
}

void CTraderApi::OnRspQryTrade(CThostFtdcTradeField *pTrade, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQryTrade", pRspInfo, nRequestID, bIsLast))
	{
		OnTrade(pTrade, nRequestID, bIsLast);
	}
}

//void CTraderApi::ReqQryQuote()
//{
//	CThostFtdcQryQuoteField* pBody = (CThostFtdcQryQuoteField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcQryQuoteField));
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));
//
//	m_msgQueue_Query->Input_NoCopy(RequestType::E_QryQuoteField, m_msgQueue_Query, this, 0, 0,
//		pBody, sizeof(CThostFtdcQryQuoteField), nullptr, 0, nullptr, 0);
//}

int CTraderApi::_ReqQryQuote(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	CThostFtdcQryQuoteField body = { 0 };

	strcpy(body.BrokerID, m_RspUserLogin.BrokerID);
	strcpy(body.InvestorID, m_RspUserLogin.UserID);

	return m_pApi->ReqQryQuote(&body, ++m_lRequestID);
}

void CTraderApi::OnQuote(CThostFtdcQuoteField *pQuote, int nRequestID, bool bIsLast)
{
	if (nullptr == pQuote)
		return;

	OrderIDType quoteId = { 0 };
	sprintf(quoteId, "%d:%d:%s", pQuote->FrontID, pQuote->SessionID, pQuote->QuoteRef);
	OrderIDType orderSydId = { 0 };

	{
		// 保存原始订单信息，用于撤单

		unordered_map<string, CThostFtdcQuoteField*>::iterator it = m_id_api_quote.find(quoteId);
		if (it == m_id_api_quote.end())
		{
			// 找不到此订单，表示是新单
			CThostFtdcQuoteField* pField = new CThostFtdcQuoteField();
			memcpy(pField, pQuote, sizeof(CThostFtdcQuoteField));
			m_id_api_quote.insert(pair<string, CThostFtdcQuoteField*>(quoteId, pField));
		}
		else
		{
			// 找到了订单
			// 需要再复制保存最后一次的状态，还是只要第一次的用于撤单即可？记下，这样最后好比较
			CThostFtdcQuoteField* pField = it->second;
			memcpy(pField, pQuote, sizeof(CThostFtdcQuoteField));
		}

		// 这个地方是否要进行其它处理？

		// 保存SysID用于定义成交回报与订单
		//sprintf(orderSydId, "%s:%s", pQuote->ExchangeID, pQuote->QuoteSysID);
		//m_sysId_quoteId.insert(pair<string, string>(orderSydId, quoteId));
	}

	{
		// 从API的订单转换成自己的结构体

		if (nRequestID == 0)
		{
			QuoteField* pField = nullptr;
			unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
			if (it == m_id_platform_quote.end())
			{
				// 开盘时发单信息还没有，所以找不到对应的单子，需要进行Order的恢复
				pField = (QuoteField*)m_msgQueue->new_block(sizeof(QuoteField));

				strcpy(pField->InstrumentID, pQuote->InstrumentID);
				strcpy(pField->ExchangeID, pQuote->ExchangeID);

				pField->AskQty = pQuote->AskVolume;
				pField->AskPrice = pQuote->AskPrice;
				pField->AskOpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pQuote->AskOffsetFlag);
				pField->AskHedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pQuote->AskHedgeFlag);

				pField->BidQty = pQuote->BidVolume;
				pField->BidPrice = pQuote->BidPrice;
				pField->BidOpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pQuote->BidOffsetFlag);
				pField->BidHedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pQuote->BidHedgeFlag);

				strcpy(pField->ID, quoteId);
				strcpy(pField->LocalID, pField->ID);
				strcpy(pField->AskOrderID, pQuote->AskOrderSysID);
				strcpy(pField->BidOrderID, pQuote->BidOrderSysID);

				strncpy(pField->Text, pQuote->StatusMsg, sizeof(Char256Type));

				//pField->ExecType = ExecType::ExecNew;
				pField->Status = CThostFtdcQuoteField_2_OrderStatus(pQuote);
				pField->ExecType = ExecType::ExecType_New;


				// 添加到map中，用于其它工具的读取，撤单失败时的再通知等
				m_id_platform_quote.insert(pair<string, QuoteField*>(quoteId, pField));
			}
			else
			{
				pField = it->second;

				strcpy(pField->ID, quoteId);
				strcpy(pField->LocalID, pField->ID);
				strcpy(pField->AskOrderID, pQuote->AskOrderSysID);
				strcpy(pField->BidOrderID, pQuote->BidOrderSysID);

				pField->Status = CThostFtdcQuoteField_2_OrderStatus(pQuote);
				pField->ExecType = CThostFtdcQuoteField_2_ExecType(pQuote);

				strncpy(pField->Text, pQuote->StatusMsg, sizeof(Char256Type));
			}

			m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRtnQuote, m_msgQueue, m_pClass, 0, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);
		}
		else
		{
			QuoteField* pField = nullptr;
			unordered_map<string, QuoteField*>::iterator it = m_id_platform_quote.find(quoteId);
			if (it == m_id_platform_quote.end())
			{
				// 开盘时发单信息还没有，所以找不到对应的单子，需要进行Order的恢复
				pField = (QuoteField*)m_msgQueue->new_block(sizeof(QuoteField));

				strcpy(pField->InstrumentID, pQuote->InstrumentID);
				strcpy(pField->ExchangeID, pQuote->ExchangeID);

				pField->AskQty = pQuote->AskVolume;
				pField->AskPrice = pQuote->AskPrice;
				pField->AskOpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pQuote->AskOffsetFlag);
				pField->AskHedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pQuote->AskHedgeFlag);

				pField->BidQty = pQuote->BidVolume;
				pField->BidPrice = pQuote->BidPrice;
				pField->BidOpenClose = TThostFtdcOffsetFlagType_2_OpenCloseType(pQuote->BidOffsetFlag);
				pField->BidHedgeFlag = TThostFtdcHedgeFlagType_2_HedgeFlagType(pQuote->BidHedgeFlag);

				strcpy(pField->ID, quoteId);
				strcpy(pField->LocalID, pField->ID);
				strcpy(pField->AskOrderID, pQuote->AskOrderSysID);
				strcpy(pField->BidOrderID, pQuote->BidOrderSysID);

				strncpy(pField->Text, pQuote->StatusMsg, sizeof(Char256Type));

				//pField->ExecType = ExecType::ExecNew;
				pField->Status = CThostFtdcQuoteField_2_OrderStatus(pQuote);
				pField->ExecType = CThostFtdcQuoteField_2_ExecType(pQuote);
				strncpy(pField->Text, pQuote->StatusMsg, sizeof(Char256Type));


				// 添加到map中，用于其它工具的读取，撤单失败时的再通知等
				//m_id_platform_quote.insert(pair<string, QuoteField*>(quoteId, pField));

				m_msgQueue->Input_Copy(ResponeType::ResponeType_OnRspQryQuote, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(QuoteField), nullptr, 0, nullptr, 0);

				m_msgQueue->delete_block(pField);
			}
		}
	}
}

void CTraderApi::OnRspQryQuote(CThostFtdcQuoteField *pQuote, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQryQuote", pRspInfo, nRequestID, bIsLast))
	{
		OnQuote(pQuote, nRequestID, bIsLast);
	}
}

void CTraderApi::OnRtnInstrumentStatus(CThostFtdcInstrumentStatusField *pInstrumentStatus)
{
}

//void CTraderApi::ReqQryInvestor()
//{
//	CThostFtdcQryInvestorField* pBody = (CThostFtdcQryInvestorField*)m_msgQueue_Query->new_block(sizeof(CThostFtdcQryInvestorField));
//
//	strncpy(pBody->BrokerID, m_RspUserLogin.BrokerID, sizeof(TThostFtdcBrokerIDType));
//	strncpy(pBody->InvestorID, m_RspUserLogin.UserID, sizeof(TThostFtdcInvestorIDType));
//
//	m_msgQueue_Query->Input_NoCopy(RequestType::E_QryInvestorField, m_msgQueue_Query, this, 0, 0,
//		pBody, sizeof(CThostFtdcQryInvestorField), nullptr, 0, nullptr, 0);
//}

int CTraderApi::_ReqQryInvestor(char type, void* pApi1, void* pApi2, double double1, double double2, void* ptr1, int size1, void* ptr2, int size2, void* ptr3, int size3)
{
	CThostFtdcQryInvestorField body = { 0 };

	strcpy(body.BrokerID, m_RspUserLogin.BrokerID);
	strcpy(body.InvestorID, m_RspUserLogin.UserID);

	return m_pApi->ReqQryInvestor(&body, ++m_lRequestID);
}

void CTraderApi::OnRspQryInvestor(CThostFtdcInvestorField *pInvestor, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	if (!IsErrorRspInfo("OnRspQryInvestor", pRspInfo, nRequestID, bIsLast))
	{
		if (pInvestor)
		{
			memcpy(&m_Investor, pInvestor, sizeof(CThostFtdcInvestorField));

			InvestorField* pField = (InvestorField*)m_msgQueue->new_block(sizeof(InvestorField));

			strcpy(pField->BrokerID, pInvestor->BrokerID);
			strcpy(pField->InvestorID, pInvestor->InvestorID);
			strcpy(pField->InvestorName, pInvestor->InvestorName);
			strcpy(pField->IdentifiedCardNo, pInvestor->IdentifiedCardNo);
			pField->IdentifiedCardType = TThostFtdcIdCardTypeType_2_IdCardType(pInvestor->IdentifiedCardType);

			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQryInvestor, m_msgQueue, m_pClass, bIsLast, 0, pField, sizeof(InvestorField), nullptr, 0, nullptr, 0);

#ifdef ENABLE_LICENSE
			// 这里不使用pInvestor->InvestorID，因为有可能股东代码与登录账号不一样
			int err = m_pLicense->GetErrorCodeByNameThenAccount(pField->InvestorName,m_UserInfo.UserID);
			{
				RspUserLoginField* pField = (RspUserLoginField*)m_msgQueue->new_block(sizeof(RspUserLoginField));
				pField->RawErrorID = err;
				strncpy(pField->Text, m_pLicense->GetErrorInfo(), sizeof(Char256Type));

				if (err < 0)
				{
					m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnConnectionStatus, m_msgQueue, m_pClass, ConnectionStatus::ConnectionStatus_Disconnected, 0, pField, sizeof(RspUserLoginField), nullptr, 0, nullptr, 0);
				}
			}
#endif
		}
		else
		{
			m_msgQueue->Input_NoCopy(ResponeType::ResponeType_OnRspQryInvestor, m_msgQueue, m_pClass, bIsLast, 0, nullptr, 0, nullptr, 0, nullptr, 0);
		}
	}
}
