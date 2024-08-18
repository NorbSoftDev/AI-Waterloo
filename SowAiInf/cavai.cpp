//*************************************************************************************************************
//*************************************************************************************************************
// File: cavai.cpp
//*************************************************************************************************************
//*************************************************************************************************************

#include "stdafx.h"
#include "sowaiinf.h"

#include "../sowmod/xunit.h"
#include "../sowmod/xunitdef.h"

#include "user-declarations.h"
#include "SOWWLMOD.h"


#define AI_GUARDYDS   50
#define AI_FOLLOWTICS (15 * TICSPERSEC)
#define AI_CANTFOLLOW    3

#define DISTCAV1AUTOMELEE 100
#define GREATERTHREAT     2000
#define DIST2AMMOLOAD     160  // 40 yards squared
#define CAVATTACK 1
#define SABERBREAK 3
#define CAVWEIGHT 10


#define COL_CHECK(val) CXUtil::MultSpec(val,11,10)

extern bool GetCavCommand( CXOff lead, int state, SXComm &comm, CXUnit unit );
extern bool IsSkirmisher( CXUnit base );
extern bool IsSquareType( CXUnit base, bool GroupSquare, bool OnlyGroup );
extern int FearCheck( CXUnit base, CXUnit sold );
extern bool CheckAdvancePath(CXUnit base, int dist);

//*************************************************************************************************************
//*************************************************************************************************************
// Function: ChargedFaceSituation
//*************************************************************************************************************
//*************************************************************************************************************
static bool ChargedFaceSituation(CXUnit base)
{
     CXUnit sold;

	  if (!base.TargClose() || base.TargCloseYds()>60)  //for avoid to lost CPU I check only if I have 1 target really near
		  return true;

	  sold = base.GetCharger();

	  if (sold==NULL || sold.UnitType()!=eUnitCav)  //check only cavalry enemies
		  return true;

	  if ( IsSkirmisher(sold))  //no skirmishers 
		  return true;

	  if (sold.State()!=eUnitMelee)  //valid for melee 
		   return true;
		
	  if (FearCheck(base,sold)>=SABERBREAK)  //fear check has to be > of 3
	  {
		  base.SetClosest(NULL);
		  base.SetMainTarg(NULL);
		  base.ICommand( eComRetreat, 1 );   //check validity of new retreat location
		  if ( !base.ValidDest( base.GetLocAhead(300) ) )
		  {
			  CXVec vec = base.GetLocAhead(300);

			  if ( base.GetValidDest( base.DeP(), &vec, true ) )
				  base.SetDest(vec);
		  }
		  else
			  base.SetDest(base.GetLocAhead(300));
		  return false;
	  }

	  return true;

}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: CheckAllTargets
//*************************************************************************************************************
//*************************************************************************************************************
static bool CheckAllTargets( CXUnit base, SEnemy *enemy, CXUnit &closest, CXUnit &soldmel, int &mindist, SEnemyExtension *enemyextension )
{
	int quad = 0;
	int dist;
	int myquad = base.GetQuad(NULL); 

	CXUnit sold;
	CXUnit main = NULL;
	int diff = 0;

	soldmel = NULL;
	closest = NULL;
	mindist = 0;

	base.TargBeg();
//loop on all the possibile targets
	while ( base.TargNext( sold, dist ) )
	{
		if ( sold == base.MainTarg() )
			main = sold;

		if (sold.InFort())  //enemy in fort is not valid
			continue;

		// someone is meleeing us, we need to fight back
		if ( ( base.DoesShareHex(sold) || ( sold.Closest() == base && sold.State() == eUnitMelee ) ) && !sold.IsArt() &&
		     sold.State() != eUnitRetreat )
		{
			if (IsSkirmisher(base) && !IsSkirmisher(sold))  //skirmishers can melee only with skirmishers
			{
				base.ICommand( eComGetAway );
				return false;
			}
			if (IsSquareType(sold,true,false) && sold.State()!=eUnitMarch && sold.State()!=eUnitRun)  //I retreat from the melee with a square formed
			{
				base.ICommand(eComRetreat);
				return false;
			}		
			if ( base.MountState() == eMountMounted && base.GetUnitTypeVal(UVAL_CanCharge) )   
			{
				base.SetClosest(sold);

				base.ICommand( eComState, eUnitMelee );
			}
			else
				base.ICommand( eComRetreat );

			return false;
		}
		// can't fire at meleeing targets
		else if ( sold.State() == eUnitMelee && base.State() != eUnitMelee && base.State() != eUnitCharge )
		{
			soldmel = sold;

//			continue;   // cavalry uses blades so meele enemies are valid targets
		}
		else if ( !sold.CanFight() )
			continue;
//		else if ( sold.InFort() )  //cavlary ignore enemies in fort as target
//			continue;

		quad = base.GetQuad( sold );

		if ( !closest || dist < mindist )
		{
			closest = sold;
			mindist = dist;
		}

		// check closest in quad
		if ( !enemy[quad].closest || dist < enemy[quad].cdist )
		{
			enemy[quad].closest = sold;
			enemy[quad].cdist   = dist;
		} 

// frontal target----------------------

		if ( !enemyextension[myquad].closesttarget || dist < enemyextension[myquad].closesttargetdist)
		{
			diff = base.Pos().GetAngleDiff(sold.Loc(),true);  //check angle for avoid lateral movement
			if ( diff <= 60 || diff >= 300) 
			{

					enemyextension[myquad].closesttarget = sold;
					enemyextension[myquad].closesttargetdist  = dist;
			}
		}


//-------------------------------------------------------------


		// total
		int u = sold.NumMen();

		if ( sold.UnitType() == eUnitArt )
			u *= base.GetUnitTypeVal(UVAL_ArtWeight);
		else if ( sold.UnitType() == eUnitCav )
			u *= base.GetUnitTypeVal(UVAL_CavWeight);
		else if ( sold.UnitType() == eUnitInf )
			u *= base.GetUnitTypeVal(UVAL_InfWeight);

		enemy[quad].men  += u;
		enemy[quad].dist += u * dist;
	}

// addition for manage the human targeting
	if (main)
	{
		base.SetClosest(main);	
	}
	else
	{
		base.SetMainTarg(NULL);
		if (enemyextension[myquad].closesttarget.Valid())  //if nearest target already in shooting range
		{
			base.SetClosest(enemyextension[myquad].closesttarget);
		}
		else
		{
			base.SetClosest(NULL);
		}
	}

	return true;
}
//----new cavalry------------------------------------------------------
//*************************************************************************************************************
//*************************************************************************************************************
// Function: SkirmishersLogicTarget
//*************************************************************************************************************
//*************************************************************************************************************
static bool SkirmishersLogicTarget( CXUnit base, int & )
{
  
	bool bRes = true;
    int enemydistxskirmish = 0; //enemy distance for skirmishers fallback


	if (!IsSkirmisher(base))
		return true;


		//skirmishing cavalry
	if  (base.FormCloseYds()>0 && base.EnemyClose())
	{
		enemydistxskirmish = base.FormCloseYds();  //temp variabile for dynamic calculation of fallback distance
		if ( base.EnemyClose().IsArt() || base.EnemyClose().FormCloseYds() >0 )
			enemydistxskirmish = 0;   //artillery or other skirmishers not generate fallback
		else if (  base.EnemyClose().Retreating() )
			enemydistxskirmish = 0;
	}

	if ( base.EnemyClose() && base.FormCloseYds() > 0 && enemydistxskirmish >= base.EnemyCloseYds())   //skirmishers routine (enemy close at place of mindist)
	{
			base.SetClosest(NULL);
			base.SetMainTarg(NULL);
			base.ICommand( eComGetAway );
			return false;
	}

	return bRes;

}
//*************************************************************************************************************
//*************************************************************************************************************
// Function: StandTargetAmmoAI
//*************************************************************************************************************
//*************************************************************************************************************
static bool StandTargetAmmoAI( CXUnit base, SEnemy *enemy, CXUnit closest, const int &mindist, SEnemyExtension *enemyext )
{
	int i;
	int quad;
	int myquad = base.GetQuad(NULL);
	int deltaquad = 0;
	int diff;
	CXOff  lead = base.Leader();

	SXComm comm;

	int threat = 0;

	bool bJustWheel = base.JustWheeled();  //justwheeled memo


	base.JustWheeled(0); //I reset justwheeled


	if ( ( base.State() != eUnitStand && base.State() != eUnitShoot ) )  //if not in standing state
		return true;

	quad      = -1;


	// find if danger in the flanks
	for ( i = 0; i < eQuadMax; i++ )
	{
		if ( enemy[i].men == 0)     // no danger  no enemy
			continue;

		enemy[i].dist /= enemy[i].men;

		enemy[i].threat = enemy[i].men * 100 - enemy[i].dist - enemy[i].cdist;

		deltaquad = abs(base.GetQuad(NULL) - base.GetQuad(enemy[i].closest));


		if ( deltaquad == 2)   //danger on the rear I multiply x 4 
			enemy[i].threat *= 4;

		if  ( deltaquad == 1)   // danger on the flanks I multiply x 2 
			enemy[i].threat *= 2;


		if ( enemy[i].men > 0 && ( quad == -1 || enemy[i].threat > threat ) )  // I evaluate the more dangerous threat
		{
			quad   = i;
			threat = enemy[i].threat;
		}
	}



//if closest is not the nearest and the closest is under the autocharge distance I leave base.closest() 
	if (base.Closest() && enemyext[myquad].closesttargetdist>DISTCAV1AUTOMELEE && !base.MainTarg() &&
		closest && mindist<DISTCAV1AUTOMELEE && closest.GetID()!=base.Closest().GetID())
		base.SetClosest(NULL);

	// if the base.closest is not valid and I have a better closest near I change
	//  new cavalry------------------
	if ( !base.Closest() && closest )
	{	
		diff = base.Pos().GetAngleDiff(closest.Loc(),true);
		if ( ( diff > 45 && diff < 90) || ( diff < 315 && diff > 270) )
		{
			base.SetClosest(closest);
		}
		else if (diff>=90 && diff<=270)
			closest = NULL;
	}
	//----------------------------------

	//no enemy before me I can if I can wheel to the nearest (no frontal enemy
	if (base.Closest()==NULL && closest == NULL && (quad!=myquad && quad!=-1) && enemy[quad].closest && bJustWheel==0)
	{
		if (base.WheelToTarget(enemy[quad].closest))
		{
			base.JustWheeled(1);
			if (base.TargClose() || base.UnderFire() || base.IsFlanked())  //danger I move running
				base.ICommand(eComRun);
			return false;

		}
	}



	if (base.Closest())  //I have a closest
	{


		//I check before If I can charge
		if (base.Leader().Valid())   //ask for the charge order
		{
			if (GetCavCommand(base.Leader(), eSTAttackTarget, comm, base ))  //attacktarget is the charging situation
			{
				if (comm.Comm() != eComMax)
					base.Command(&comm);
				if (comm.Comm()==eComCharge || comm.Comm()==eComState)  //if charge i stop here
					return false;
			}
			else if ( GetCavCommand(base.Leader(), eSTStandTarget, comm, base ) )  //ask to leader //----ask for cavalry standing---ready to attack
			{
				if (comm.Comm() != eComMax)
					base.Command( &comm );	
				if (comm.Comm()==eComFormType)
				{
					comm.Comm(eComRun);
					base.Command( &comm ); //change formation in speed
				}
				return false;
			}
	//autocharge for player controlled brigade
			else if (base.Leader().Valid() && base.Leader().PlyrCtrl() && !base.Blocked() && base.TargClose().Valid())
			{
				CXUnit playertarg;
				int playertargdist = INT_MAX;

				if (base.MainTarg())   //if selected the maintarget
					playertarg = base.MainTarg();
				else
					playertarg = base.TargClose(); //ohterwise another target

				base.TargFind(playertarg,playertargdist);

				if (base.CanCharge(playertarg,true) 
				&& (FearCheck(playertarg,base)>=CAVATTACK || playertarg.Retreating()) 
				&& playertargdist<DISTCAV1AUTOMELEE && !IsSquareType(playertarg,true,false)
				&& !playertarg.InFort())
				{
					base.SetClosest(playertarg);
					base.ICommand(eComCharge);
					return false;
				}
				else if (playertargdist<DISTCAV1AUTOMELEE) 
					base.SetMainTarg(NULL);  //in the case is not possible attack the maintarget I avoid to aim fixed
			}
			else
			{
				base.SetClosest(NULL);
				base.SetMainTarg(NULL);
			}
		}
	}
	else  // no base.closest
	{

//-----if nothing happen all null
		base.SetClosest(NULL);
		base.SetMainTarg(NULL);

	}

	if (base.Leader().Valid() && !base.Closest())  //i cannot attack  I fall back if under fire and already done the volley
	{
				
//when no closest is present but a target close is too near to me, I need to react with a melee
//when a closest is present the stance manage this for the melee also in checkalltargets
		if (base.TargClose() && !base.TargClose().InFort() && base.DoesShareHex(base.TargClose()))   //if happen to have contact
		{
			base.SetClosest(base.TargClose());
			comm.Comm( eComState );
			comm.Val( eUnitMelee );
			base.Command(&comm);
		}
		else if ( GetCavCommand(base.Leader(), eSTStandNoTargetFire, comm, base ) )  //ask to leader standnottargetfire is when cavalry retreat under the fire
		{
			if (comm.Comm() != eComMax)
				base.Command( &comm );	
			if (base.State()==eUnitMarch && base.IsFlanked())
			{
				comm.Comm(eComRun);
				base.Command(&comm);  //I run to execute the order - danger
			}
		}
		else if (base.Leader().Valid() && base.Leader().PlyrCtrl() && (base.UnderFire1() || base.IsFlanked() || base.UnderRecentFire()))  //player controlled brigade
		{
			comm.Comm(eComMoveDir);
			CXVec vec = base.GetLocBehind(150);
			comm.Vec(true,vec);
			comm.Dir(true,base.Dir());
			base.Command(&comm);
			comm.Comm( eComRun );
			base.Command(&comm);
		}
//---new cavalry---------------------------------------
	}

	return false;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: StandNoTargetAI
//*************************************************************************************************************
//*************************************************************************************************************
static bool StandNoTargetAI( CXUnit base, CXUnit closest, CXUnit soldmel, SEnemy * )
{
	int i;
//	int temp;

	bool  bRes = true;
	SXComm comm;

//i continue if standing and no target
	if ( base.State() != eUnitStand || base.CurrTarg() || base.Closest() != NULL || closest != NULL )
		return bRes;

	// only target is in melee  if I have melee target
	if ( soldmel != NULL )
	{

// --- new cavalry-------------------------
		if (base.Leader().Valid())   //ask for the charge order
		{
			if (GetCavCommand(base.Leader(), eSTAttackTarget, comm, base ))
			{
				if (comm.Comm() != eComMax)
					base.Command(&comm);
				if (comm.Comm()==eComCharge)  //if charge i stop here
					return false;
			}
			else if ( bRes && GetCavCommand(base.Leader(), eSTStandTarget, comm, base ) )  //ask to leader //----ask for cavalry standing---ready to attack
			{
				if (comm.Comm() != eComMax)
					base.Command( &comm );	
				if (comm.Comm()==eComFormType)
				{
					comm.Comm(eComRun);
					base.Command( &comm ); //change formation in speed
				}
				return false;
			}
		}
//-----new cavalry--------------------------
	}



	//if ( base.Ammo() == 0 || base.Leader().ShouldGetAmmo( base ) )   //ammo refillment  not to use for the moment cavalry have not fire weapons
	//{
	//	CXVec loc;
	//	bool bFound = base.Leader().FindClosestSpec( eUnitAmmo, base.Loc(), loc );

	//	if ( bFound && base.Loc().Dist2Yds(loc) > DIST2AMMOLOAD )
	//	{
	//		base.ICommand( eComMove, loc );

	//		base.ICommand( eComFormType, eFTMarch );

	//		temp  = base.GetMod( eGameRunForAmmo );
	//		temp += base.Leader().GetMod( eGameRunForAmmo );

	//		if ( temp >= 0 )
	//			base.ICommand( eComRun );

	//		bRes = false;
	//	}
	//}
	//else 


		
	if ( base.Leader().Valid() && GetCavCommand(base.Leader(), eSTStandNoTarget, comm, base ) )  //get command from the leader for no target standing
	{
		if (comm.Comm() != eComMax)
			base.Command( &comm );

		bRes = false;
	}
	else if ( base.UnderFire1() && base.NumTargs() == 0 )   //if under a recent fire and not target
	{
		base.KillTime(0); // so we don't do this again based on the same kill
	
		if ( base.Leader().Valid() && GetCavCommand(base.Leader(), eSTStandNoTargetFire, comm, base ) )  //I ask to leader for orders because no targets
		{
			if (comm.Comm() != eComMax)
				base.Command( &comm );

			bRes = false;
		}
		else if ( base.EnemyClose().Valid())    //if enemy close is valid
		{
			int dist = base.EnemyCloseYds();

			if ( dist <= DISTCAV1AUTOMELEE && ( base.EnemyClose().GetUnitTypeVal(eUnitCav) || base.EnemyClose().IsArt()) )   //only if is cavalry or artillery i add them to the target list, if under the automelee range
				base.TargAdd( base.EnemyClose(), dist );


			if ( bRes ) // we did nothing, let's not just stand here
			{
				i = CXUtil::Ran( 98, 0, 100 );

				if ( i <= base.GetMod(eGameRetreatPct) )   //I check the percentange for the autoretreat
				{
				    base.SetClosest(NULL);
				    base.SetMainTarg(NULL);				
					base.ICommand( eComGetAway, 1 );				
					bRes = false;	
				}
			}
		}
	}

	return bRes;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: CavRaidAI
//*************************************************************************************************************
//*************************************************************************************************************

static bool CavRaidAI( CXUnit base )
{
	bool bNeed = true;

	CXUnit sold;
	CXUnit best = NULL;
	
	int dist;
	int bdist  = 0;
	int wdist2 = base.MaxRangeYds();

	wdist2 *= wdist2;

	if ( base.FollowTarg() )
	{
		if ( !base.EnemyFind( base.FollowTarg(), dist ) )
			base.SetFollow(NULL);
		else if ( base.FollowTarg().IsOfficer() )
			base.SetFollow(NULL);
		else if ( base.FollowTarg().InFort() )
			base.SetFollow(NULL);
		else if (IsSquareType(base.FollowTarg(),true,false)) //don't raid on squares
			base.SetFollow(NULL);
		else if (!base.Leader().CanICharge(base))  //cannot charge I search another target
			base.SetFollow(NULL);
		else if (!base.CanCharge(base.FollowTarg()))
			base.SetFollow(NULL);
	}

	if ( base.FollowTarg() && !base.FollowTarg().HasInfOrArtFriends( wdist2, base.Leader().Personality() ) )
	{
		if ( base.FollowTarg().UnitType() == eUnitInf || base.FollowTarg().UnitType() == eUnitCav )
		{
//			if ( base.Leader().ShouldAttack( true, base.FollowTarg(), base, false ) )
			if (FearCheck(base.FollowTarg(),base)>=CAVATTACK)  //fear check for raids
			 	bNeed = false;
		}
		else
			bNeed = false;
	}
		
	if ( bNeed )
	{
		base.EnemyBeg();

		sold = NULL;

		while ( base.EnemyNext( sold, dist ) )
		{
			if ( best && bdist < dist )
				continue;

			if ( sold.IsOfficer() )
				continue;

			if ( sold.InFort() )
				continue;

			if (IsSquareType(sold,true,false))
				continue;

			if ( !sold.HasInfOrArtFriends( wdist2, base.Leader().Personality() ) )
			{
				if ( sold.UnitType() == eUnitInf || sold.UnitType() == eUnitCav )
				{
//					if ( base.Leader().ShouldAttack( true, sold, base, false ) )
					if (FearCheck(sold,base)>=CAVATTACK)  //fear check for raids
					{
						best  = sold;
						bdist = dist;
					}
				}
				else
				{
					best  = sold;
					bdist = dist;
				}
			}
		}

		if ( best )
			bNeed = false;

		base.SetFollow(best);
	}
	return !bNeed;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: FollowAI
//*************************************************************************************************************
//*************************************************************************************************************

static bool FollowAI( CXUnit base )
{
	bool bRes = true;
	CXVec loc;

	if ( base.FollowTarg() )
	{
		if ( base.NumTargs() == 1 && base.FollowType() == eFollowRaid )
			base.SetFollow(base.TargClose());

		if ( base.FollowType() == eFollowGuard )
			loc = base.FollowTarg().GetLocBehind( AI_GUARDYDS );
		else
			loc = base.FollowTarg().Loc();

		if ( base.FollowTarg().Hidden() )
		{
			base.SetFollow(NULL);
			return bRes;
		}
	}
	else if ( base.State() == eUnitStand && ( base.FollowType() == eFollowScout || base.FollowType() == eFollowRaid || base.FollowType() == eFollowScreen ) )
	{
		// I've made it, let's get a new spot
		if ( base.Loc() == base.FollowLoc() )
		{
		 	loc = base.FollowLoc();
			base.Leader().GetScoutLoc( base, loc, false );

			base.SetFollowLoc(loc);
		}
	 	loc = base.FollowLoc();
	}
	else
		loc = base.Dst();
	
	if ( base.State() == eUnitStand && base.Loc() != loc )
		base.FollowTics( AI_FOLLOWTICS + 5 );
	else if ( base.Dst() != loc )
	{
		base.FollowTics(	base.FollowTics() + 1 );
		base.SetDest( loc );
	}

	if ( base.FollowTics() > AI_FOLLOWTICS )
	{
//		base.SetDest( loc );

		base.FollowTics(0);

		base.ICommand( eComMove, loc );
//		base.ICommand( eComMove );

		if ( base.FollowType() == eFollowGuard )
			base.ICommand( eComEndDir, base.FollowTarg().Dir() );
		else if ( base.FollowTarg().Valid() && base.EnemyCloseYds() < 0 ) // we see an enemy
			base.ICommand( eComUseRoad );
	}
	return bRes;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: GuardAI
//*************************************************************************************************************
//*************************************************************************************************************

static bool GuardAI( CXUnit base )
{
	bool bNeed = false;

	CXUnit sold;
	CXUnit follow = base.FollowTarg();

	if (follow && follow.InFort())
		return bNeed;

	if ( base.State() == eUnitCharge || base.State() == eUnitMelee )
		bNeed = true;
	else if ( !follow )
		bNeed = true;
	else if ( follow.Rank() == eRankBrig || !follow.IsOfficer() )
	{
		sold = follow.GetCharger();

		if ( sold.Valid() )
		{
			base.SetClosest(sold);

			base.ICommand( eComState, eUnitCharge );

			bNeed = true;
		}
	}
	return bNeed;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: FollowTargAI
//*************************************************************************************************************
//*************************************************************************************************************

static bool FollowTargAI( CXUnit base )
{
	bool   bRes = true;
	CXUnit sold;

	int dist;
	int distr = 0;
	int fdist = 0;

	sold = base.EnemyClose();
	dist = base.EnemyCloseYds();

	if ( sold.Valid() )
		distr = base.Loc().DistYds( sold.Loc() );

	// if we can't see who we were following
	if ( base.FollowTarg().Valid() && base.FollowType() != eFollowGuard && !base.EnemyFind( base.FollowTarg(), fdist ) )
	{
		base.SetFollow(NULL);
		bRes     = false;
	}
	else if ( base.FollowType() == eFollowGuard && GuardAI(base) )
	{
		bRes     = false;
	}
	else if ( base.FollowType() == eFollowRaid && CavRaidAI(base) )
	{
		// cavraidai can change m_follow
		if ( base.FollowTarg().Valid() )
			distr = base.Loc().DistYds( base.FollowTarg().Loc() );
		bool nocharge = false;

		//---I avoid squares done from other units-----
		if (base.FollowTarg().Valid() && base.TargClose() && base.TargCloseYds()<100
			&& base.TargClose().GetID()!=base.FollowTarg().GetID()
			&& IsSquareType(base.TargClose(),true,false))
		  nocharge = true;

		if ( !nocharge && base.FollowTarg().Valid() && distr < base.MaxRangeYds() &&
			base.State() != eUnitCharge && base.Leader().CanICharge(base) &&
			   base.CanCharge( base.FollowTarg() ) && base.GetUnitTypeVal(UVAL_CanCharge) && !IsSquareType(base.FollowTarg(),true,false) )
		{
			base.SetClosest(base.FollowTarg());
			base.ICommand( eComState, eUnitCharge );	
			bRes = false;
		}
		else if ( base.State() != eUnitRun && base.Moving() )
		{
			base.ICommand( eComRun );
			bRes = false;
		}
		else if ( base.UnderRecentFire() && !base.Moving() )
		{
			base.ICommand( eComGetAway );
			bRes = false;
		}
	}
	else if ( base.FollowType() == eFollowScreen )
	{
		if ( sold && distr < base.LongRangeYds() && !base.Moving() )
		{
			// get outta here
			base.ICommand( eComGetAway );
			bRes = false;
		}
		else if ( base.Moving() && sold && distr < base.MaxRangeYds() )
		{
			// Stop, I'm close enough
			base.ICommand( eComHalt );
			if (base.IsFormType(eFTMarch))  //if still in march formation i switch to line
				base.ICommand(eComFormType,eFTFight);
			bRes = false;
		}
		else if ( base.FollowTarg() && !base.TargClose() && base.Moving() && base.State() != eUnitRun )
			base.ICommand( eComRun );
		else if ( !base.FollowTarg() && sold && !sold.InFort())
			base.SetFollow(sold);
	}
	else if ( base.FollowType() == eFollowScout || base.FollowType() == eFollowRaid )
	{
		if ( sold && dist < 300 || base.UnderFire() )
		{
			// get outta here
			base.ICommand( eComGetAway );
			bRes = false;
		}
		else if ( base.FollowTarg() && fdist < 500 )
		{
			// Stop, I'm close enough
			base.ICommand( eComHalt );
			bRes = false;
		}
		else if ( !base.FollowTarg() && sold && !sold.InFort() && !IsSquareType(sold,true,false) )
			base.SetFollow(sold);
	}	
	return bRes;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: MoraleAI
//*************************************************************************************************************
//*************************************************************************************************************

static bool MoraleAI( CXUnit base )
{
	int  i;
	bool bRes = true;
	bool bDanger;
	bool bRouteCheck = base.CheckRouting();

 //danger evaluation: there a target or under fire and there are targets or  or is under a charge																		 	 
	bDanger = ( (base.UnderFire() || base.CurrTarg()) &&  base.NumTargs() );  

	// Routed units
	if ( base.Morale() <= MORAL_ROUTED )
	{
		if ( base.State() != eUnitRouted )
			base.ICommand( eComRoute, 1 );

		bRes = false;
	}
	else if ( bRouteCheck ) //routing check
	{
		base.RouteUnit();
		bRes = false;
	}
	// Broken units
	else if ( base.Morale() <= MORAL_BROKEN )
	{
		if ( (base.State() != eUnitRetreat || !base.Moving()) && bDanger )
		{
			base.SetClosest(NULL);
			base.SetMainTarg(NULL);
			base.ICommand( eComRetreat, 1 );
		}
		base.SetClosest(NULL);
		base.SetMainTarg(NULL);

		bRes = false;
	}
	// Check fallback, retreat after drops in morale
	else if ( base.PrevMor() > base.Morale() && bDanger )
	{
		if ( base.State() != eUnitRetreat )
		{
			i = CXUtil::Ran( 98, 0, 100 );

			if ( base.State() != eUnitMelee && base.State() != eUnitCharge &&
				   i <= base.GetMod(eGameFallbackPct) && base.CurrTarg())    //davide  no fallback for cavalry
			{
	            base.SetClosest(NULL);
		        base.SetMainTarg(NULL);
				base.ICommand( eComRetreat );			
				bRes = false;	
			}
			else if ( i <= base.GetMod(eGameRetreatPct) )   //retreat check
			{
		        base.SetClosest(NULL);
	            base.SetMainTarg(NULL);
				base.ICommand( eComRetreat, 1 );			
				bRes = false;	
			}

		}
	}

	if ( !bRes )  //if i'm retreating\falling back i reset the closest
	{
		base.SetClosest(NULL);
		base.SetMainTarg(NULL);
	}

	return bRes;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: MarchNoTargetAI
//*************************************************************************************************************
//*************************************************************************************************************

static bool MarchNoTargetAI( CXUnit base )
{
	bool bRes = true;
	SXComm comm;

	if ( base.CurrTarg() )   //current target? I go out
		return bRes;


	if ( base.State() == eUnitFallback )    //if falling back but not targets or not under fire I switch to run
	{
		if ( !base.UnderFire() || base.NumTargs() == 0 )
			base.ICommand( eComJustRun );

		bRes = false;
	}
	else if ( base.State() == eUnitAdvance )    //if advancing  I check if I can halt
	{
		if ( base.HaltTics() <= 0 && base.ValidDest(base.Loc()) )
			base.ICommand( eComHalt );

		bRes = false;
	}
	else if ( !base.Marching() )   //I'm not marching I go out
		return bRes;
	else if ( base.IsFormType(eFTMarch) && base.EnemyClose().Valid() && base.EnemyCloseYds() < COL_CHECK(base.MaxRangeYds()) )  //I switch ti fighting formation if a valid enemy is sufficiently near
	{
		// Check with our leader
		//this function should order to march in a formation different from that of march
		if ( base.Leader().Valid() && GetCavCommand(base.Leader(), eSTMarchNoTarget, comm, base ) )
		{	
			if (comm.Comm() != eComMax)
				base.Command( &comm );
		}
		else
			base.ICommand( eComFormType, eFTFight );  

		if ( base.UnderRecentFire() )  //run if under fire
			base.ICommand( eComRun );

		bRes = false;
	}
	else if ( !base.ValidSpot( base.Dst() ) )   //if my destination is a not valid spot i check if I can halt where I'm on the basis of enemy distance and the validity of place
	{
		if ( base.EnemyClose().Valid() && base.EnemyCloseYds() < COL_CHECK(base.MaxRangeYds()) )
		{
			if ( base.HaltTics() <= 0 && base.ValidDest(base.Loc()) )
				base.ICommand( eComHalt );
		}
		// if the enemy isn't close, just keep marching there, hopefully it will be ok when we get there
		bRes = false;
	}
	else if ( !base.ValidWalk( base.Loc() ) )  //I check if I can walk on the place where I'm, if not I repath
	{
		base.Repath();
		bRes = false;
	}
	else if ( base.IsSprinting() && base.State() == eUnitRun )  //if I'm sprinting and i'm in run state, i return to walk
	{
		base.IsSprinting(0);
		base.ICommand( eComWalk );
	}
	return bRes;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: MeleeAI
//*************************************************************************************************************
//*************************************************************************************************************
static bool MeleeAI( CXUnit base )
{
	bool bRes = true;
	SXComm comm;

	// If Meleeing
	if ( base.State() == eUnitMelee || base.State() == eUnitCharge )
	{
		bRes = false;

		if ( !base.Closest())    //I have not closest I halt
		{			
			base.SetMainTarg(NULL);
			base.SetClosest(NULL);
			base.ICommand( eComHalt );
		}
		else 
		{				
			if ( base.State() == eUnitCharge)  //i'm charging but i can charge the closest, I stop
			{			
				
				//ask for the charge orders in the case of square or of pursuit		
				if ( ( !base.CanCharge( base.Closest() ) || base.Closest().InFort() ) )  // cant' charge I stop
				{
					base.SetMainTarg(NULL);
					base.SetClosest(NULL);
					base.ICommand( eComHalt );
				}
				else if (base.Leader().Valid() && GetCavCommand(base.Leader(), eSTChargeTargetSquare, comm, base ))
				{
					if (comm.Comm() != eComMax)
						base.Command(&comm);
					return bRes;
				}
				//I overlap with another target during my charge I switch to the melee with him if he's in melee already
//				else if (base.TargClose() && base.TargClose().GetID()!=base.Closest().GetID() && base.TargClose().State()==eUnitMelee && base.DoesShareHex(base.TargClose()))
				else if (base.TargClose() && !base.TargClose().InFort()
					&& base.TargClose().GetID()!=base.Closest().GetID() && base.DoesShareHex(base.TargClose()))
				{
					base.SetMainTarg(NULL);
					base.SetClosest(NULL);
					base.ICommand( eComHalt );
					base.SetClosest(base.TargClose());
					base.ICommand(eComState,eUnitMelee);
				}
				else if (base.Leader().Valid() && base.Leader().PlyrCtrl() && IsSquareType(base.Closest(),true,false) //for player controlled units
					&& base.Closest().State()!=eUnitMarch && base.Closest().State()!=eUnitRun && !base.Closest().Retreating() )
				{
					base.SetMainTarg(NULL);
					base.SetClosest(NULL);
					base.ICommand( eComHalt );
				}
				else if (base.Closest().Retreating())  //the target is retreating
				{
					CXUnit ChargerMe;
					
					if ( base.TargClose().Valid() && !base.TargClose().InFort() && !base.TargClose().Retreating() && base.DoesShareHex(base.TargClose())) //if I overlap with this i go in melee with him
					{
						if (IsSquareType(base.TargClose(),true,false) && base.TargClose().State()!=eUnitMarch && base.TargClose().State()!=eUnitRun)  //I overalp with a square formed 
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand(eComRetreat);
						}
						else
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							base.SetClosest(base.TargClose());
							base.ICommand(eComState,eUnitMelee);
						}
					}
					else if ((ChargerMe = base.GetCharger()) && ChargerMe && base.DoesShareHex(ChargerMe))
					{
						if (IsSquareType(ChargerMe,true,false) && ChargerMe.State()!=eUnitMarch && ChargerMe.State()!=eUnitRun)  //I overalp with a square formed 
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand(eComRetreat);
						}
						else
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							base.SetClosest(ChargerMe);
							base.ICommand(eComState,eUnitMelee);
						}
					}
					else if ( base.TargClose().Valid() && !base.TargClose().Retreating() && !base.TargClose().InFort() //I charge another possible target
						&& (!IsSquareType(base.TargClose(),true,false) || base.TargClose().State()==eUnitMarch || base.TargClose().State()==eUnitRun) && base.CanCharge(base.TargClose(),true) 
						&& base.Leader().Valid() && base.Leader().CanICharge(base))
					{
						base.SetMainTarg(NULL);
						base.SetClosest(NULL);
						base.ICommand( eComHalt );
						base.SetClosest(base.TargClose());
						base.ICommand( eComCharge );
					}
					else if (!base.ValidDest(base.Closest().Loc()))
					{
						base.SetMainTarg(NULL);
						base.SetClosest(NULL);
						base.ICommand( eComHalt );
					}
//					else if (!base.ValidWalk(base.Loc()) || !base.ValidWalk(base.Closest().Loc()))   //ohterwise I stop
//				I check the retreating path of enemy: if obstacle is behind and there's a friend in range, i check if this friend is in fort or in square
// if not as soon I overlap with him, I enter in melee with him
					else if (base.Closest().FriendClose() && !base.Closest().FriendClose().IsOfficer()
						&& !CheckAdvancePath(base.Closest(),DISTCAV1AUTOMELEE/2))  //stop
					{
						CXUnit friendclose = base.Closest().FriendClose();
						if (IsSquareType(friendclose,true,false) || friendclose.InFort())
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
						}
						else if (base.DoesShareHex(friendclose))
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							base.SetClosest(friendclose);
							base.ICommand(eComState,eUnitMelee);
						}
						else if (base.Closest().DoesShareHex(friendclose))
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							base.SetClosest(friendclose);
							base.ICommand( eComCharge );
						}
					}

				}
			}
			else if ( base.State() == eUnitMelee  )   //i'm meleing but i can charge the closest, I stop
			{	

				if (base.Leader().Valid() && GetCavCommand(base.Leader(), eSTMeleeTargetSquare, comm, base ))  //ask to the leader what to do
				{
					if (comm.Comm() != eComMax)
						base.Command(&comm);
					return bRes;
				}	
				else if (base.Leader().Valid() && base.Leader().PlyrCtrl() && IsSquareType(base.Closest(),true,false) //for player controlled units
					&& base.Closest().State()!=eUnitMarch && base.Closest().State()!=eUnitRun && !base.Closest().Retreating())
				{
					base.SetMainTarg(NULL);
					base.SetClosest(NULL);
					base.ICommand( eComGetAway );
				}
				//else if (!base.CanCharge( base.Closest(), true ))  //cannot melee , I stop
				else if ( base.Leader().Valid() && !base.Leader().CanICharge(base) && !base.Closest().Retreating())
				{
					base.SetClosest(NULL);
					base.SetMainTarg(NULL);
					base.ICommand( eComGetAway );
				}
				else if (base.Closest().Retreating() ) //i'm meleing but the closest is retreating i ask to leader if I have to stop
				{
					CXUnit ChargerMe;

					// this is really for cav
					if ( base.TargClose().Valid() && !base.TargClose().InFort() && !base.TargClose().Retreating() && base.DoesShareHex(base.TargClose())) //switch of melee target
					{
						if (IsSquareType(base.TargClose(),true,false) && base.TargClose().State()!=eUnitMarch && base.TargClose().State()!=eUnitRun)  //I overalp with a square formed 
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand(eComRetreat);
						}
						else
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							base.SetClosest(base.TargClose());
							base.ICommand(eComState,eUnitMelee);
						}
					}
					else if ((ChargerMe = base.GetCharger()) && ChargerMe && base.DoesShareHex(ChargerMe))
					{
						if (IsSquareType(ChargerMe,true,false) && ChargerMe.State()!=eUnitMarch && ChargerMe.State()!=eUnitRun)  //I overalp with a square formed 
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand(eComRetreat);
						}
						else
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							base.SetClosest(ChargerMe);
							base.ICommand(eComState,eUnitMelee);
						}
					}
					else if ( base.TargClose().Valid() && !base.TargClose().Retreating() && !base.TargClose().InFort() //I charge another possible target nearest than the target I have to pursuit
						&& (!IsSquareType(base.TargClose(),true,false) || base.TargClose().State()==eUnitMarch || base.TargClose().State()==eUnitRun) && base.CanCharge(base.TargClose(),true) 
						&& base.Leader().Valid() && base.Leader().CanICharge(base))
					{
						base.SetMainTarg(NULL);
						base.SetClosest(NULL);
						base.ICommand( eComHalt );
						base.SetClosest(base.TargClose());
						base.ICommand( eComCharge );
					}
//					else if (!base.ValidWalk(base.Loc()) || !base.ValidWalk(base.Closest().Loc()))  //stop
					else if (!base.ValidDest(base.Closest().Loc()))
					{
						base.SetMainTarg(NULL);
						base.SetClosest(NULL);
						base.ICommand( eComHalt );
					}
//				I check the retreating path of enemy: if obstacle is behind and there's a friend in range, i check if this friend is in fort or in square
// if not as soon I overlap with him, I enter in melee with him
					else if (base.Closest().FriendClose() && !base.Closest().FriendClose().IsOfficer() 
						&& !CheckAdvancePath(base.Closest(),DISTCAV1AUTOMELEE/2))  //stop
					{
						CXUnit friendclose = base.Closest().FriendClose();
						if (IsSquareType(friendclose,true,false) || friendclose.InFort())
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
						}
						else if (base.DoesShareHex(friendclose))
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							base.SetClosest(friendclose);
							base.ICommand(eComState,eUnitMelee);
						}
						else
						{
							base.SetMainTarg(NULL);
							base.SetClosest(NULL);
							base.ICommand( eComHalt );
							if (base.TargClose().Valid() && !base.TargClose().InFort() )
							{
								base.SetClosest(base.TargClose());
								base.ICommand( eComCharge );
							}						
						}
					}			
					else if ( base.Leader().Valid() && !base.Leader().CanICharge(base) )  //true stop
					{
						base.SetMainTarg(NULL);
						base.SetClosest(NULL);
						base.ICommand( eComHalt );
					}
					else    //pursuit effect
					{
						base.SetMainTarg(NULL);
						base.SetClosest(NULL);
						base.ICommand( eComHalt );
						if (base.TargClose().Valid() && !base.TargClose().InFort() )
						{
							base.SetClosest(base.TargClose());
							base.ICommand( eComCharge );
						}						
					}
				}			
				else if ( !base.CanCharge( base.Closest() ) || base.Closest().InFort() )  // cant' charge I stop
				{
					base.SetMainTarg(NULL);
					base.SetClosest(NULL);
					base.ICommand( eComHalt );
				}			
			}
		}
	}
	return bRes;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: MarchTargetAI
// returning false usually means we're gonna stop
//*************************************************************************************************************
//*************************************************************************************************************
static bool MarchTargetAI( CXUnit base, SEnemy *, CXUnit closest, int mindist, SEnemyExtension *enemyext)
{
	bool bRes = true;
	int diff;
	CXOff lead = base.Leader();
	SXComm comm;
	int myquad = base.GetQuad(NULL);

	if ( !base.CurrTarg() || !base.Marching() )  //i have not target or not marching I go out from here
		return bRes;


//if closest is not the nearest and the closest is under the autocharge distance I leave base.closest() 
	if (base.Closest() && enemyext[myquad].closesttargetdist>DISTCAV1AUTOMELEE && !base.MainTarg() &&
		closest && mindist<DISTCAV1AUTOMELEE && closest.GetID()!=base.Closest().GetID())
		base.SetClosest(NULL);


	if (!base.Closest() && closest) //no frontal quadrant i check the closest in the frontal semiarc if possible i move against him
	{
		diff = base.Pos().GetAngleDiff(closest.Loc(),true);
		if ( ( diff < 90 || diff > 270) ) //&& mindist<(base.MaxRangeYds()/2))
		{
			base.SetClosest(closest);
		}
	}

	if (!base.Closest())
	{
//when no closest is present but a target close is too near to me, I need to react with a melee
//when a closest is present the stance manage this for the melee also in checkalltargets


		if (base.TargClose() && !base.TargClose().InFort() && base.DoesShareHex(base.TargClose()))   //if happen to have contact
		{
			base.SetClosest(base.TargClose());
			comm.Comm( eComState );
			comm.Val( eUnitMelee );
			base.Command(&comm);
			return false;
		}

		diff = base.Pos().GetAngleDiff(base.TargClose().Loc(),true);
		if ( diff < 90 || diff > 270)    //checks valid only with enemy not behind me
		{
			if (base.IsFormType(eFTMarch))  //if i'm not in march formation I stop
				bRes = false;	
			if ( base.UnderRecentFire() && base.GetMod(eGameMarchUnderFire) < 0 )  
				bRes = false;
			if ( base.TargCloseYds() < base.GetUnitTypeVal(UVAL_MarchTargYds) )
				bRes = false;
		}
		
		//if (base.Leader().Valid() && GetCavCommand(base.Leader(), eSTAttackTarget, comm, base )) //ask to leader what to do, eventual charge order
		//{
		//	if (comm.Comm() != eComMax)
		//		base.Command(&comm);
		//	if (comm.Comm()==eComCharge || comm.Comm()==eComState)  //if charge i stop here
		//		return false;
		//}

	}
	else //base.closest valid
	{	
		
		if (base.Leader().Valid() && GetCavCommand(base.Leader(), eSTAttackTarget, comm, base )) //ask to leader what to do, eventual charge order
		{
			if (comm.Comm() != eComMax)
				base.Command(&comm);
			if (comm.Comm()==eComCharge || comm.Comm()==eComState)  //if charge i stop here
				return false;
		}
		else if (base.Leader().Valid() && GetCavCommand(base.Leader(), eSTMarchTargetOnlyMelee, comm, base ))  //if I'm here no charge order given so march target orders
		{
			if (comm.Comm() != eComMax)
				base.Command(&comm);
			return false;
		}
//autocharge for player controlled brigade
		else if (base.Leader().Valid() && base.Leader().PlyrCtrl() && !base.Blocked() && base.TargClose().Valid())
		{
			CXUnit playertarg;
			int playertargdist = INT_MAX;

			if (base.MainTarg())   //if selected the maintarget
				playertarg = base.MainTarg();
			else
				playertarg = base.TargClose(); //ohterwise another target

			base.TargFind(playertarg,playertargdist);

			if (base.CanCharge(playertarg,true) 
			&& (FearCheck(playertarg,base)>=CAVATTACK || playertarg.Retreating()) 
			&& playertargdist<DISTCAV1AUTOMELEE && !IsSquareType(playertarg,true,false)
			&& !playertarg.InFort())
			{
				base.SetClosest(playertarg);
				base.ICommand(eComCharge);
				return false;
			}
			else if (playertargdist<DISTCAV1AUTOMELEE)
			{
				base.SetMainTarg(NULL);  //in the case is not possible attack the maintarget I avoid to aim fixed				
				bRes = false;
			}
		}
		else  //no attack possibility so I check causes of stop
		{
			diff = base.Pos().GetAngleDiff(base.TargClose().Loc(),true);
			if ( diff < 90 || diff > 270)    //checks valid only with enemy not behind me
			{
				if (base.IsFormType(eFTMarch))  //if i'm not in march formation I stop
					bRes = false;			
				if ( base.UnderRecentFire() && base.GetMod(eGameMarchUnderFire) < 0 )  
					bRes = false;
				if ( base.TargCloseYds() < base.GetUnitTypeVal(UVAL_MarchTargYds) )
					bRes = false;
			}
		}

	}

	if ( !bRes && base.HaltTics() <= 0 && base.ValidDest( base.Loc() ) )  //if I have to halt and I can on the place, I halt
	{
		base.ICommand( eComHalt );
		base.ICommand( eComRun  );
		bRes = false;
	}

	return bRes;
}
//*************************************************************************************************************
//*************************************************************************************************************
// Function: SkirmishersLogicNoTarget
//*************************************************************************************************************
//*************************************************************************************************************
static bool SkirmishersLogicNoTarget( CXUnit base )
{
	bool bRes = true; 
	SXComm comm;
	int enemydistxskirmish = 0; //enemy distance for skirmishers fallback

//----routine for volunteer jagers-- no able to fight in line only skirmishing
	if (base.FormTypeIndex(eFTFight)==base.FormTypeIndex(eFTAltSkirmish))
	{
		if (!base.IsFormType(eFTAltSkirmish) && base.EnemyClose() && base.EnemyCloseYds()<(base.MaxRangeYds()*3))
		{	
			base.ICommand(eComFormType,eFTAltSkirmish);
			base.ICommand(eComMove,base.GetLocAhead(150));
			base.ICommand(eComRun);
			return false;
		}
		else if (base.IsFormType(eFTAltSkirmish) && ( !base.EnemyClose() || base.EnemyCloseYds()>=(base.MaxRangeYds()*3)))
		{
			base.ICommand(eComFormType,eFTLine);
			base.ICommand(eComRun);
			return false;
		}
	}
//---------------------------------------------------------------------------------	

	if (!IsSkirmisher(base))  //from here to below routine only for skirmishers
		return true;

	if  (base.FormCloseYds()>0 && base.EnemyClose())  //dynamic fallback logic
	{

		enemydistxskirmish = base.FormCloseYds();  //temp variabile for dynamic calculation of fallback distance
		if ( base.EnemyClose().IsArt() || base.EnemyClose().FormCloseYds() >0)
			enemydistxskirmish = 0;   //artillery or other skirmishers not generate fallback
		else if (  base.EnemyClose().Retreating() )  //no limit for retreating enemy
			enemydistxskirmish = 0;	
	}

	if ( base.EnemyClose() && base.FormCloseYds() > 0 && enemydistxskirmish >= base.EnemyCloseYds())   //skirmishers routine
	{
			base.SetClosest(NULL);
			base.SetMainTarg(NULL);
			base.ICommand( eComGetAway );
			return false;
	}


	return bRes;

}
//*************************************************************************************************************
//*************************************************************************************************************
// Function: Think
//*************************************************************************************************************
//*************************************************************************************************************

static bool Think( int /*ticks*/, CXUnit base )
{
	int mindist;

	CXUnit soldmel = NULL;
	CXUnit closest = NULL;

	SEnemy enemy[ eQuadMax ];

	memset( enemy, 0, sizeof enemy );


//  davide---------------------
	SEnemyExtension enemyextension[ eQuadMax ];

	memset( enemyextension, 0, sizeof enemyextension );

//-----------------------------	


	if ( base.State() == eUnitRouted )    //routed units go out here
		return true;

	if ( !MoraleAI(base) ) // routed will always go here
		return true;

	if ( base.State() == eUnitRetreat )  //unit is retreating out here
	{		
		if ( !base.ValidDest( base.Dst() ) )
		{
			CXVec vec = base.Dst();
				
			if ( base.GetValidDest( base.DeP(), &vec, true ) )
				base.SetDest(vec);
		}
		return true;
	}


	// If Meleeing
	if ( !MeleeAI(base) )  //melee management
		return true;

	if ( base.FollowType() != eFollowNone && base.Morale() <= AI_CANTFOLLOW )   //i check if I have the morale for the various specific types of follow
	{
		base.SetFollow(NULL);
		base.FollowType(eFollowNone);

		if ( !base.Attached() )             //if not I reattach
			base.ICommand( eComAttach );
	}

//if I have a specific type of follow and I have not a target close and I'm not blocked or i'm raiding and the targets numbers are 1 ; I check the routine per for management of
// if specific follow types (screening,scouts,raids)
	if ( ( base.FollowType() != eFollowNone && !base.TargClose() && !base.Blocked() ) || ( base.FollowType() == eFollowRaid && base.NumTargs() == 1 ) )
		FollowAI(base);

//If i'm still acting as follow specializer I continue to do it
	if ( base.FollowType() != eFollowNone )
		if ( !FollowTargAI( base ) )
			return true;



	// If no targets
	if ( !base.CurrTarg() )
	{

		if ( base.AttackMarch() )  //if attacking march I go out here
			return true;

		if (!SkirmishersLogicNoTarget(base))  //logic cavlary skirmishers
			return true;


		if ( !StandNoTargetAI( base, NULL, NULL, enemy ) )  //standing state no targets
			return true;


		if ( !MarchNoTargetAI(base) )  //marching state no targets
			return false;

		base.SetClosest(NULL);
		base.SetMainTarg(NULL);


	 	return true;
	}


//------ new cavalry---------------------
	// being charged on the flank\rear is a killing situation
	if (!ChargedFaceSituation(base))
		return true;

//-------end new cavalry---------------------

	if ( !CheckAllTargets( base, enemy, closest, soldmel, mindist, enemyextension ) )
		return true;


	if ( base.AttackMarch() )  //if I'm in attack march I continue
		return true;

//  new skirmishers----------------------
	if (!SkirmishersLogicTarget(base, mindist))
		return true;


//----new skirmishers---------------------



	// run again to see if we can charge
	if ( !StandNoTargetAI( base, closest, soldmel, enemy ) )  //stand not target, rechecked here for the melee check
		return true;


	if ( !StandTargetAmmoAI( base, enemy, closest, mindist, enemyextension) )  //I stand with a target
		return true;



	if ( !MarchTargetAI(base, enemy, closest, mindist, enemyextension) )  //I march with a target
		return false;

	return false;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: MyThink
//*************************************************************************************************************
//*************************************************************************************************************

static bool MyThink( int /*ticks*/, CXUnit base )
{
	int mindist;
	
	CXUnit soldmel = NULL;
	CXUnit closest = NULL;

	SEnemy enemy[ eQuadMax ];

	memset( enemy, 0, sizeof enemy );

//  davide---------------------
	SEnemyExtension enemyextension[ eQuadMax ];

	memset( enemyextension, 0, sizeof enemyextension );

//-----------------------------


	if ( base.State() == eUnitRouted )
		return true;

	if ( !MoraleAI(base) )
		return true;

	if ( base.State() == eUnitRetreat )    //for the retreating unit i find a valid destination point for the retreat
	{
		base.ValidatePath();
		return true;
	}


	// If Meleeing
	if ( !MeleeAI(base) )
		return true;

	// If no targets
	if ( !base.CurrTarg() )
	{
		if ( base.State() == eUnitShoot )
			base.ICommand( eComHalt );

		base.SetClosest(NULL);
		base.SetMainTarg(NULL);

	 	return true;
	}


//------ new cavalry---------------------
	// being charged on the flank\rear is a killing situation
	if (!ChargedFaceSituation(base))
		return true;

//-------end new cavalry---------------------
	
	if ( !CheckAllTargets( base, enemy, closest, soldmel, mindist, enemyextension ) )
		return true;


	if ( base.State() == eUnitStand && base.CurrTarg())
	{
		if ( !base.CanFormFire() )
			base.ICommand( eComFormType, eFTFight );
		else
		{
			base.SetClosest(closest);

		}
	}
	return true;
}

//*************************************************************************************************************
//*************************************************************************************************************
// Function: SowAIFunc
//*************************************************************************************************************
//*************************************************************************************************************

int SowCavAIFunc( int ticks, void *punit )
{
	int res;

	CXUnit unit((CUnit *)punit);

	if ( unit.PlyrCtrl() )
		res = MyThink( ticks, unit );		
	else
		res = Think( ticks, unit );		

	return res;
}

//*************************************************************************************************************
//*************************************************************************************************************
//*************************************************************************************************************
//*************************************************************************************************************

