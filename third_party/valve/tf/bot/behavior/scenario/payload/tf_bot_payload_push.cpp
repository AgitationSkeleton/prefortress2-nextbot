//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_payload_push.cpp
// Push the cartTrigger to the goal
// Michael Booth, April 2010

#include "cbase.h"
#include "nav_mesh.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "team_control_point_master.h"
#include "team_train_watcher.h"
#include "trigger_area_capture.h"
#include "bot/tf_bot.h"
#include "bot/behavior/scenario/payload/tf_bot_payload_push.h"
#include "bot/behavior/scenario/payload/tf_bot_payload_guard.h"
#include "bot/behavior/tf_bot_seek_and_destroy.h"
#include "bot/behavior/medic/tf_bot_medic_heal.h"
#include "bot/behavior/engineer/tf_bot_engineer_build.h"


extern ConVar tf_bot_path_lookahead_range;
ConVar tf_bot_cart_push_radius( "tf_bot_cart_push_radius", "60", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotPayloadPush::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_path.Invalidate();

	m_hideAngle = 180.0f;
	m_pf2RoleTimer.Start( RandomFloat( 3.0f, 15.0f ) );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotPayloadPush::Update( CTFBot *me, float interval )
{
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleRecently() )
	{
		// prepare to fight
		me->EquipBestWeaponForThreat( threat );
	}

	if ( TFGameRules()->InSetup() )
	{
		// wait until the gates open, then path
		m_path.Invalidate();
		m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );

		return Continue();
	}

	CTeamTrainWatcher *trainWatcher = TFGameRules()->GetPayloadToPush( me->GetTeamNumber() );
	if ( !trainWatcher )
	{
		return Continue();
	}

	CBaseEntity *cart = trainWatcher->GetTrainEntity();
	if ( !cart )
	{
		return Continue();
	}

	if ( m_pf2RoleTimer.IsElapsed() )
	{
		m_pf2RoleTimer.Start( RandomFloat( 3.0f, 15.0f ) );

		// PF2NB (TF2C): enough teammates are on the cart already - go hunt for a while instead of crowding it
		if ( trainWatcher->GetCapturerCount() >= 3 && !me->IsRangeLessThan( cart->WorldSpaceCenter(), 200.0f ) )
		{
			return SuspendFor( new CTFBotSeekAndDestroy( 10.0f ), "Enough pushers - roaming" );
		}

		// PF2NB (TF2C): payload race - if the enemy cart is ahead of ours, some of us go and stop it
		if ( TFGameRules()->HasMultipleTrains() )
		{
			CTeamTrainWatcher *enemyCart = TFGameRules()->GetPayloadToBlock( me->GetTeamNumber() );
			if ( enemyCart && !enemyCart->IsDisabled() && enemyCart->GetTrainProgress() > trainWatcher->GetTrainProgress() &&
				 me->TransientlyConsistentRandomValue( 5.0f ) < MAX( 0.2f, enemyCart->GetTrainProgress() ) )
			{
				return ChangeTo( new CTFBotPayloadGuard, "Enemy cart is ahead of ours - stopping it" );
			}
		}
	}

	// move toward the point, periodically repathing to account for changing situation
	if ( m_repathTimer.IsElapsed() )
	{
		VPROF_BUDGET( "CTFBotPayloadPush::Update( repath )", "NextBot" );

		Vector cartForward;
		cart->GetVectors( &cartForward, NULL, NULL );

		// default push position is behind cart
		Vector pushPos = cart->WorldSpaceCenter() - cartForward * tf_bot_cart_push_radius.GetFloat();

		// try to hide from enemies on other side of cart
		const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
		if ( threat )
		{
			Vector enemyToCart = cart->WorldSpaceCenter() - threat->GetLastKnownPosition();
			enemyToCart.z = 0.0f;
			enemyToCart.NormalizeInPlace();

			pushPos = cart->WorldSpaceCenter() + tf_bot_cart_push_radius.GetFloat() * enemyToCart;		
		}

		CTFBotPathCost cost( me, DEFAULT_ROUTE );
		m_path.Compute( me, pushPos, cost );

		m_repathTimer.Start( RandomFloat( 0.2f, 0.4f ) );
	}

	// push the cartTrigger
	m_path.Update( me );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot > CTFBotPayloadPush::OnResume( CTFBot *me, Action< CTFBot > *interruptingAction )
{
	VPROF_BUDGET( "CTFBotPayloadPush::OnResume", "NextBot" );

	m_repathTimer.Invalidate();

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotPayloadPush::OnStuck( CTFBot *me )
{
	VPROF_BUDGET( "CTFBotPayloadPush::OnStuck", "NextBot" );

	m_repathTimer.Invalidate();
	me->GetLocomotionInterface()->ClearStuckStatus();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotPayloadPush::OnMoveToSuccess( CTFBot *me, const Path *path )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotPayloadPush::OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason )
{
	VPROF_BUDGET( "CTFBotPayloadPush::OnMoveToFailure", "NextBot" );

	m_repathTimer.Invalidate();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
QueryResultType	CTFBotPayloadPush::ShouldRetreat( const INextBot *bot ) const
{
	return ANSWER_UNDEFINED;
}


//---------------------------------------------------------------------------------------------
QueryResultType CTFBotPayloadPush::ShouldHurry( const INextBot *bot ) const
{
	return ANSWER_UNDEFINED;
}

