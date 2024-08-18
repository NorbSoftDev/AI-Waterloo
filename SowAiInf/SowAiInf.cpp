
#include "stdafx.h"
#include "SowAiInf.h"

#include "../sowmod/xunitdef.h"
#include "../sowmod/xtables.h"

#include "xtables.inl"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CSowAiInfApp::CSowAiInfApp()
{
}
CSowAiInfApp theApp;

SSowModFuncs &gMod = theApp.m_sow;

//*************************************************************************************************************
//*************************************************************************************************************
// Function: Init
//*************************************************************************************************************
//*************************************************************************************************************

int SowInit( void *funcs )
{
	SSowModFuncs *sow = (SSowModFuncs *)funcs;
	
	sow->gTableDivStrat   = gDivStrat;
	sow->gTableTacOrders  = TableTacOrders;
	sow->gTableBrigStance = TableStance;

	theApp.m_sow = *sow;

	return 1;
}

//*************************************************************************************************************
//*************************************************************************************************************
//*************************************************************************************************************
//*************************************************************************************************************

