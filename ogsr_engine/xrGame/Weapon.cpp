// Weapon.cpp: implementation of the CWeapon class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "Weapon.h"
#include "ParticlesObject.h"
#include "HUDManager.h"
#include "WeaponHUD.h"
#include "entity_alive.h"
#include "inventory_item_impl.h"

#include "inventory.h"
#include "xrserver_objects_alife_items.h"

#include "actor.h"
#include "actoreffector.h"
#include "level.h"

#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "../xr_3da/skeletoncustom.h"
#include "ai_object_location.h"
#include "clsid_game.h"
#include "mathutils.h"
#include "object_broker.h"
#include "..\xr_3da\IGame_Persistent.h"

#include "game_object_space.h"
#include "script_callback_ex.h"
#include "script_game_object.h"

#include "WeaponMagazinedWGrenade.h"

#define WEAPON_REMOVE_TIME		60000
#define ROTATION_TIME			0.25f

extern ENGINE_API Fvector4 w_states;
extern ENGINE_API Fvector3 w_timers;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CWeapon::CWeapon(LPCSTR name)
{
	SetState				(eHidden);
	SetNextState			(eHidden);
	m_sub_state				= eSubstateReloadBegin;
	m_idle_state				= eIdle;
	m_bTriStateReload		= false;
	SetDefaults				();

	m_Offset.identity		();
	m_StrapOffset.identity	();

	iAmmoCurrent			= -1;
	m_dwAmmoCurrentCalcFrame= 0;

	iAmmoElapsed			= -1;
	iMagazineSize			= -1;
	m_ammoType				= 0;

	eHandDependence			= hdNone;

	m_fZoomFactor = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;

	m_fZoomRotationFactor	= 0.f;


	m_pAmmo					= NULL;


	m_pFlameParticles2		= NULL;
	m_sFlameParticles2		= NULL;


	m_fCurrentCartirdgeDisp = 1.f;

	m_strap_bone0			= 0;
	m_strap_bone1			= 0;
	m_StrapOffset.identity	();
	m_strapped_mode			= false;
	m_can_be_strapped		= false;
	m_ef_main_weapon_type	= u32(-1);
	m_ef_weapon_type		= u32(-1);
	m_UIScope				= NULL;
	m_set_next_ammoType_on_reload = u32(-1);

	m_nearwall_last_hud_fov = psHUD_FOV_def;
}

CWeapon::~CWeapon		()
{
	xr_delete	(m_UIScope);
}

//void CWeapon::Hit(float P, Fvector &dir,	
//		    CObject* who, s16 element,
//		    Fvector position_in_object_space, 
//		    float impulse, 
//		    ALife::EHitType hit_type)
void CWeapon::Hit					(SHit* pHDS)
{
//	inherited::Hit(P, dir, who, element, position_in_object_space,impulse,hit_type);
	inherited::Hit(pHDS);
}



void CWeapon::UpdateXForm	()
{
	if (Device.dwFrame!=dwXF_Frame)
	{
		dwXF_Frame = Device.dwFrame;

		if (0==H_Parent())	return;

		// Get access to entity and its visual
		CEntityAlive*	E		= smart_cast<CEntityAlive*>(H_Parent());
		
		if(!E) 
			return;

		const CInventoryOwner	*parent = smart_cast<const CInventoryOwner*>(E);
		if (parent && parent->use_simplified_visual())
			return;

		if (parent->attached(this))
			return;

		R_ASSERT		(E);
		CKinematics*	V		= smart_cast<CKinematics*>	(E->Visual());
		VERIFY			(V);

		// Get matrices
		int				boneL,boneR,boneR2;
		E->g_WeaponBones(boneL,boneR,boneR2);
		if ((HandDependence() == hd1Hand) || (GetState() == eReload) || (!E->g_Alive()))
			boneL = boneR2;
#pragma todo("TO ALL: serious performance problem")
		V->CalculateBones	();
		Fmatrix& mL			= V->LL_GetTransform(u16(boneL));
		Fmatrix& mR			= V->LL_GetTransform(u16(boneR));
		// Calculate
		Fmatrix				mRes;
		Fvector				R,D,N;
		D.sub				(mL.c,mR.c);	

		if(fis_zero(D.magnitude()))
		{
			mRes.set(E->XFORM());
			mRes.c.set(mR.c);
		}
		else
		{		
			D.normalize();
			R.crossproduct	(mR.j,D);

			N.crossproduct	(D,R);			
			N.normalize();

			mRes.set		(R,N,D,mR.c);
			mRes.mulA_43	(E->XFORM());
		}

		UpdatePosition	(mRes);
	}
}

void CWeapon::UpdateFireDependencies_internal()
{
	if (Device.dwFrame!=dwFP_Frame) 
	{
		dwFP_Frame			= Device.dwFrame;

		UpdateXForm			();

		if (GetHUDmode() && (0!=H_Parent()) )
		{
			// 1st person view - skeletoned
			CKinematics* V			= smart_cast<CKinematics*>(m_pHUD->Visual());
			VERIFY					(V);
			V->CalculateBones		();

			// fire point&direction
			Fmatrix& fire_mat		= V->LL_GetTransform(u16(m_pHUD->FireBone()));
			Fmatrix& parent			= m_pHUD->Transform	();

			const Fvector& fp		= m_pHUD->FirePoint();
			const Fvector& fp2		= m_pHUD->FirePoint2();
			const Fvector& sp		= m_pHUD->ShellPoint();

			fire_mat.transform_tiny	(m_firedeps.vLastFP,fp);
			parent.transform_tiny	(m_firedeps.vLastFP);
			fire_mat.transform_tiny	(m_firedeps.vLastFP2,fp2);
			parent.transform_tiny	(m_firedeps.vLastFP2);
		
			fire_mat.transform_tiny	(m_firedeps.vLastSP,sp);
			parent.transform_tiny	(m_firedeps.vLastSP);

			m_firedeps.vLastFD.set	(0.f,0.f,1.f);
			parent.transform_dir	(m_firedeps.vLastFD);

			m_firedeps.m_FireParticlesXForm.identity();
			m_firedeps.m_FireParticlesXForm.k.set(m_firedeps.vLastFD);
			Fvector::generate_orthonormal_basis_normalized(m_firedeps.m_FireParticlesXForm.k,
									m_firedeps.m_FireParticlesXForm.j, m_firedeps.m_FireParticlesXForm.i);
		} else {
			// 3rd person or no parent
			Fmatrix& parent			= XFORM();
			Fvector& fp				= vLoadedFirePoint;
			Fvector& fp2			= vLoadedFirePoint2;
			Fvector& sp				= vLoadedShellPoint;

			parent.transform_tiny	(m_firedeps.vLastFP,fp);
			parent.transform_tiny	(m_firedeps.vLastFP2,fp2);
			parent.transform_tiny	(m_firedeps.vLastSP,sp);
			
			m_firedeps.vLastFD.set	(0.f,0.f,1.f);
			parent.transform_dir	(m_firedeps.vLastFD);

			m_firedeps.m_FireParticlesXForm.set(parent);
		}
	}
}

void CWeapon::ForceUpdateFireParticles()
{
	if ( !GetHUDmode() )
	{//update particlesXFORM real bullet direction

		if (!H_Parent())		return;
#ifdef DEBUG
		CInventoryOwner* io		= smart_cast<CInventoryOwner*>(H_Parent());
		if(NULL == io->inventory().ActiveItem())
		{
				Log("current_state", GetState() );
				Log("next_state", GetNextState());
				Log("state_time", m_dwStateTime);
				Log("item_sect", cNameSect().c_str());
				Log("H_Parent", H_Parent()->cNameSect().c_str());
		}
#endif
		Fvector					p, d; 
		smart_cast<CEntity*>(H_Parent())->g_fireParams	(this, p,d);

		Fmatrix						_pxf;
		_pxf.k						= d;
		_pxf.i.crossproduct			(Fvector().set(0.0f,1.0f,0.0f),	_pxf.k);
		_pxf.j.crossproduct			(_pxf.k,		_pxf.i);
		_pxf.c						= XFORM().c;
		
		m_firedeps.m_FireParticlesXForm.set	(_pxf);

	}

}

LPCSTR wpn_scope_def_bone = "wpn_scope";
LPCSTR wpn_silencer_def_bone = "wpn_silencer";
LPCSTR wpn_launcher_def_bone = "wpn_launcher";

void CWeapon::Load		(LPCSTR section)
{
	inherited::Load					(section);
	CShootingObject::Load			(section);

	
	if(pSettings->line_exist(section, "flame_particles_2"))
		m_sFlameParticles2 = pSettings->r_string(section, "flame_particles_2");

#ifdef DEBUG
	{
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"position");
		ypr					= pSettings->r_fvector3		(section,"orientation");
		ypr.mul				(PI/180.f);

		m_Offset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_Offset.translate_over	(pos);
	}

	m_StrapOffset			= m_Offset;
	if (pSettings->line_exist(section,"strap_position") && pSettings->line_exist(section,"strap_orientation")) {
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"strap_position");
		ypr					= pSettings->r_fvector3		(section,"strap_orientation");
		ypr.mul				(PI/180.f);

		m_StrapOffset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_StrapOffset.translate_over	(pos);
	}
#endif

	// load ammo classes
	m_ammoTypes.clear	(); 
	LPCSTR				S = pSettings->r_string(section,"ammo_class");
	if (S && S[0]) 
	{
		string128		_ammoItem;
		int				count		= _GetItemCount	(S);
		for (int it=0; it<count; ++it)	
		{
			_GetItem				(S,it,_ammoItem);
			m_ammoTypes.push_back	(_ammoItem);
		}
	}

	iAmmoElapsed		= pSettings->r_s32		(section,"ammo_elapsed"		);
	iMagazineSize		= pSettings->r_s32		(section,"ammo_mag_size"	);
	
	////////////////////////////////////////////////////
	// дисперсия стрельбы

	//подбрасывание камеры во время отдачи
	camMaxAngle			= pSettings->r_float		(section,"cam_max_angle"	); 
	camMaxAngle			= deg2rad					(camMaxAngle);
	camRelaxSpeed		= pSettings->r_float		(section,"cam_relax_speed"	); 
	camRelaxSpeed		= deg2rad					(camRelaxSpeed);
	if (pSettings->line_exist(section, "cam_relax_speed_ai"))
	{
		camRelaxSpeed_AI		= pSettings->r_float		(section,"cam_relax_speed_ai"	); 
		camRelaxSpeed_AI		= deg2rad					(camRelaxSpeed_AI);
	}
	else
	{
		camRelaxSpeed_AI	= camRelaxSpeed;
	}
	
//	camDispersion		= pSettings->r_float		(section,"cam_dispersion"	); 
//	camDispersion		= deg2rad					(camDispersion);

	camMaxAngleHorz		= pSettings->r_float		(section,"cam_max_angle_horz"	); 
	camMaxAngleHorz		= deg2rad					(camMaxAngleHorz);
	camStepAngleHorz	= pSettings->r_float		(section,"cam_step_angle_horz"	); 
	camStepAngleHorz	= deg2rad					(camStepAngleHorz);	
	camDispertionFrac			= READ_IF_EXISTS(pSettings, r_float, section, "cam_dispertion_frac",	0.7f);
	//  [8/2/2005]
	//m_fParentDispersionModifier = READ_IF_EXISTS(pSettings, r_float, section, "parent_dispersion_modifier",1.0f);
	m_fPDM_disp_base			= READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_base",	1.0f);
	m_fPDM_disp_vel_factor		= READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_vel_factor",	1.0f);
	m_fPDM_disp_accel_factor	= READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_accel_factor",	1.0f);
	m_fPDM_disp_crouch			= READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_crouch",	1.0f);
	m_fPDM_disp_crouch_no_acc	= READ_IF_EXISTS(pSettings, r_float, section, "PDM_disp_crouch_no_acc",	1.0f);
	//  [8/2/2005]

	fireDispersionConditionFactor = pSettings->r_float(section,"fire_dispersion_condition_factor"); 
	misfireProbability			  = pSettings->r_float(section,"misfire_probability"); 
	misfireConditionK			  = READ_IF_EXISTS(pSettings, r_float, section, "misfire_condition_k",	1.0f);
	conditionDecreasePerShot	  = pSettings->r_float(section,"condition_shot_dec"); 
		
	vLoadedFirePoint	= pSettings->r_fvector3		(section,"fire_point"		);
	
	if(pSettings->line_exist(section,"fire_point2")) 
		vLoadedFirePoint2= pSettings->r_fvector3	(section,"fire_point2");
	else 
		vLoadedFirePoint2= vLoadedFirePoint;

	// hands
	eHandDependence		= EHandDependence(pSettings->r_s32(section,"hand_dependence"));
	m_bIsSingleHanded	= true;
	if (pSettings->line_exist(section, "single_handed"))
		m_bIsSingleHanded	= !!pSettings->r_bool(section, "single_handed");
	// 
	m_fMinRadius		= pSettings->r_float		(section,"min_radius");
	m_fMaxRadius		= pSettings->r_float		(section,"max_radius");


	// информация о возможных апгрейдах и их визуализации в инвентаре
	m_eScopeStatus			 = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"scope_status");
	m_eSilencerStatus		 = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"silencer_status");
	m_eGrenadeLauncherStatus = (ALife::EWeaponAddonStatus)pSettings->r_s32(section,"grenade_launcher_status");

	m_bZoomEnabled = !!pSettings->r_bool(section,"zoom_enabled");
	m_bUseScopeZoom = !!READ_IF_EXISTS(pSettings, r_bool, section, "use_scope_zoom", false);
	m_bUseScopeGrenadeZoom = !!READ_IF_EXISTS(pSettings, r_bool, section, "use_scope_grenade_zoom", false);

	m_fZoomRotateTime = ROTATION_TIME;
	m_bScopeDynamicZoom = false;
	m_fScopeZoomFactor = 0;
	m_fRTZoomFactor = 0;

	UpdateZoomOffset();

	if(m_eScopeStatus == ALife::eAddonAttachable)
	{
		m_sScopeName = pSettings->r_string(section,"scope_name");
		m_iScopeX = pSettings->r_s32(section,"scope_x");
		m_iScopeY = pSettings->r_s32(section,"scope_y");
	}

	if(m_eSilencerStatus == ALife::eAddonAttachable)
	{
		m_sSilencerName = pSettings->r_string(section,"silencer_name");
		m_iSilencerX = pSettings->r_s32(section,"silencer_x");
		m_iSilencerY = pSettings->r_s32(section,"silencer_y");
	}

	if(m_eGrenadeLauncherStatus == ALife::eAddonAttachable)
	{
		m_sGrenadeLauncherName = pSettings->r_string(section,"grenade_launcher_name");
		m_iGrenadeLauncherX = pSettings->r_s32(section,"grenade_launcher_x");
		m_iGrenadeLauncherY = pSettings->r_s32(section,"grenade_launcher_y");
	}

	if (pSettings->line_exist(section, "scope_bone"))
		m_sWpn_scope_bone = pSettings->r_string(section, "scope_bone");
	else
		m_sWpn_scope_bone = wpn_scope_def_bone;

	if (pSettings->line_exist(section, "silencer_bone"))
		m_sWpn_silencer_bone = pSettings->r_string(section, "silencer_bone");
	else
		m_sWpn_silencer_bone = wpn_silencer_def_bone;

	if (pSettings->line_exist(section, "launcher_bone"))
		m_sWpn_launcher_bone = pSettings->r_string(section, "launcher_bone");
	else
		m_sWpn_launcher_bone = wpn_launcher_def_bone;

	//Можно и из конфига прицела читать и наоборот! Пока так.
	m_fSecondVPZoomFactor = 0.0f;
	m_fZoomHudFov = 0.0f;
	m_fSecondVPHudFov = 0.0f;
	m_fScopeInertionFactor = m_fControlInertionFactor;

	InitAddons();

	//////////////////////////////////////
	//время убирания оружия с уровня
	if(pSettings->line_exist(section,"weapon_remove_time"))
		m_dwWeaponRemoveTime = pSettings->r_u32(section,"weapon_remove_time");
	else
		m_dwWeaponRemoveTime = WEAPON_REMOVE_TIME;
	//////////////////////////////////////
	if(pSettings->line_exist(section,"auto_spawn_ammo"))
		m_bAutoSpawnAmmo = pSettings->r_bool(section,"auto_spawn_ammo");
	else
		m_bAutoSpawnAmmo = TRUE;
	//////////////////////////////////////


	m_bHideCrosshairInZoom = true;
	if(pSettings->line_exist(hud_sect, "zoom_hide_crosshair"))
		m_bHideCrosshairInZoom = !!pSettings->r_bool(hud_sect, "zoom_hide_crosshair");

	m_bZoomInertionAllow = false;
	if (pSettings->line_exist(hud_sect, "allow_zoom_inertion"))
		m_bZoomInertionAllow = !!pSettings->r_bool(hud_sect, "allow_zoom_inertion");

	//////////////////////////////////////////////////////////

	m_bHasTracers = READ_IF_EXISTS(pSettings, r_bool, section, "tracers", true);
	m_u8TracerColorID = READ_IF_EXISTS(pSettings, r_u8, section, "tracers_color_ID", u8(-1));

	string256						temp;
	for (int i=egdNovice; i<egdCount; ++i) {
		strconcat					(sizeof(temp),temp,"hit_probability_",get_token_name(difficulty_type_token,i));
		m_hit_probability[i]		= READ_IF_EXISTS(pSettings,r_float,section,temp,1.f);
	}
	
	m_highlightAddons.clear();
	if (pSettings->line_exist(section, "highlight_addons")) {
		LPCSTR S = pSettings->r_string(section, "highlight_addons");
		if (S && S[0]) {
			string128 _addonItem;
			int count = _GetItemCount(S);
			for (int it = 0; it < count; ++it) {
				_GetItem(S, it, _addonItem);
				m_highlightAddons.push_back(_addonItem);
			}
		}
	}

	m_nearwall_on = READ_IF_EXISTS(pSettings, r_bool, section, "nearwall_on", false);
	if (m_nearwall_on)
	{
		// Параметры изменения HUD FOV когда игрок стоит вплотную к стене
		m_nearwall_target_hud_fov = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_target_hud_fov", 0.27f);
		m_nearwall_dist_min = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_dist_min", 0.5f);
		m_nearwall_dist_max = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_dist_max", 1.f);
		m_nearwall_speed_mod = READ_IF_EXISTS(pSettings, r_float, section, "nearwall_speed_mod", 10.f);
	}
}

void CWeapon::LoadFireParams		(LPCSTR section, LPCSTR prefix)
{
	camDispersion		= pSettings->r_float		(section,"cam_dispersion"	); 
	camDispersion		= deg2rad					(camDispersion);

	if (pSettings->line_exist(section,"cam_dispersion_inc"))
	{
		camDispersionInc		= pSettings->r_float		(section,"cam_dispersion_inc"	); 
		camDispersionInc		= deg2rad					(camDispersionInc);
	}
	else
		camDispersionInc = 0;

	CShootingObject::LoadFireParams(section, prefix);
};

void CWeapon::LoadZoomOffset(LPCSTR section, LPCSTR prefix)
{
	string256 full_name;
	if (Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system)
		&& is_second_zoom_offset_enabled //Если включен режим второго прицеливания
		&& !READ_IF_EXISTS(pSettings, r_bool, *cNameSect(), "disable_second_scope", false) //И второй прицел не запрещён (нужно для поддержки замороченной системы ogse_addons)
		&& pSettings->line_exist(hud_sect, strconcat(sizeof(full_name), full_name, "second_", prefix, "zoom_offset")) //И в секциии худа есть настройки для второго режима прицеливания
		&& pSettings->line_exist(hud_sect, strconcat(sizeof(full_name), full_name, "second_", prefix, "zoom_rotate_x"))
		&& pSettings->line_exist(hud_sect, strconcat(sizeof(full_name), full_name, "second_", prefix, "zoom_rotate_y"))
	) { //Используем настройки для второго режима прицеливания
		m_pHUD->SetZoomOffset(pSettings->r_fvector3(hud_sect, strconcat(sizeof(full_name), full_name, "second_", prefix, "zoom_offset")));
		m_pHUD->SetZoomRotateX(pSettings->r_float(hud_sect,   strconcat(sizeof(full_name), full_name, "second_", prefix, "zoom_rotate_x")));
		m_pHUD->SetZoomRotateY(pSettings->r_float(hud_sect,   strconcat(sizeof(full_name), full_name, "second_", prefix, "zoom_rotate_y")));
		//
		is_second_zoom_offset_enabled = true;
		//Msg("--Second scope enabled!");
	}
	else //В противном случае используем стандартные настройки
	{
		m_pHUD->SetZoomOffset(pSettings->r_fvector3(hud_sect, strconcat(sizeof(full_name), full_name, prefix, "zoom_offset")));
		m_pHUD->SetZoomRotateX(pSettings->r_float(hud_sect,   strconcat(sizeof(full_name), full_name, prefix, "zoom_rotate_x")));
		m_pHUD->SetZoomRotateY(pSettings->r_float(hud_sect,   strconcat(sizeof(full_name), full_name, prefix, "zoom_rotate_y")));
		//
		is_second_zoom_offset_enabled = false;
		//Msg("~~Second scope disabled!");
	}

	//Зум фактор обновлять здесь необходимо. second_soom_factor поддерживается.
	m_fZoomFactor = this->CurrentZoomFactor();
	//

	if(pSettings->line_exist(hud_sect, "zoom_rotate_time"))
		m_fZoomRotateTime = pSettings->r_float(hud_sect,"zoom_rotate_time");

	callback(GameObject::eOnSecondScopeSwitch)(is_second_zoom_offset_enabled); //Для нормальной поддержки скриптовых оружейных наворотов ОГСЕ
}

void CWeapon::UpdateZoomOffset() //Собрал все манипуляции с зум оффсетом сюда, чтоб были в одном месте.
{
	if (m_bZoomEnabled && m_pHUD)
	{
		const bool has_gl = GrenadeLauncherAttachable() && IsGrenadeLauncherAttached();
		const bool has_scope = ScopeAttachable() && IsScopeAttached();

		if (IsGrenadeMode())
		{
			if (m_bUseScopeGrenadeZoom && has_scope)
				LoadZoomOffset(*hud_sect, "scope_grenade_");
			else
				LoadZoomOffset(*hud_sect, "grenade_");
		}
		else if (has_gl)
		{
			if (m_bUseScopeZoom && has_scope)
				LoadZoomOffset(*hud_sect, "scope_grenade_normal_");
			else
				LoadZoomOffset(*hud_sect, "grenade_normal_");
		}
		else
		{
			if (m_bUseScopeZoom && has_scope)
				LoadZoomOffset(*hud_sect, "scope_");
			else
				LoadZoomOffset(*hud_sect, "");
		}
	}
}

void CWeapon::SwitchScope()
{
	is_second_zoom_offset_enabled = !is_second_zoom_offset_enabled;
	UpdateZoomOffset();
}

BOOL CWeapon::net_Spawn		(CSE_Abstract* DC)
{
	BOOL bResult					= inherited::net_Spawn(DC);
	CSE_Abstract					*e	= (CSE_Abstract*)(DC);
	CSE_ALifeItemWeapon			    *E	= smart_cast<CSE_ALifeItemWeapon*>(e);

	//iAmmoCurrent					= E->a_current;
	iAmmoElapsed					= E->a_elapsed;
	m_flagsAddOnState				= E->m_addon_flags.get();
	m_ammoType						= E->ammo_type;
	SetState						(E->wpn_state);
	SetNextState					(E->wpn_state);
	
	m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));	
	if(iAmmoElapsed) 
	{
		m_fCurrentCartirdgeDisp = m_DefaultCartridge.m_kDisp;
		for(int i = 0; i < iAmmoElapsed; ++i) 
			m_magazine.push_back(m_DefaultCartridge);
	}


	UpdateAddonsVisibility();
	InitAddons();


	m_dwWeaponIndependencyTime = 0;

	VERIFY((u32)iAmmoElapsed == m_magazine.size());
	m_bAmmoWasSpawned		= false;

	return bResult;
}

void CWeapon::net_Destroy	()
{
	inherited::net_Destroy	();

	//удалить объекты партиклов
	StopFlameParticles	();
	StopFlameParticles2	();
	StopLight			();
	Light_Destroy		();

	m_magazine.clear();
	m_magazine.shrink_to_fit();
}

BOOL CWeapon::IsUpdating()
{	
	bool bIsActiveItem = m_pCurrentInventory && m_pCurrentInventory->ActiveItem()==this;
	return bIsActiveItem || bWorking || m_bPending || getVisible();
}

void CWeapon::net_Export(NET_Packet& P)
{
	inherited::net_Export	(P);

	P.w_float_q8			(m_fCondition,0.0f,1.0f);

	u8 need_upd				= IsUpdating() ? 1 : 0;
	P.w_u8					(need_upd);
	P.w_u16					(u16(iAmmoElapsed));
	P.w_u8					(m_flagsAddOnState);
	P.w_u8					((u8)m_ammoType);
	P.w_u8					((u8)GetState());
	P.w_u8					((u8)m_bZoomMode);
}

void CWeapon::net_Import(NET_Packet& P)
{
	inherited::net_Import (P);

	P.r_float_q8			(m_fCondition,0.0f,1.0f);

	u8 flags				= 0;
	P.r_u8					(flags);

	u16 ammo_elapsed = 0;
	P.r_u16					(ammo_elapsed);

	u8						NewAddonState;
	P.r_u8					(NewAddonState);

	m_flagsAddOnState		= NewAddonState;
	UpdateAddonsVisibility	();

	u8 ammoType, wstate;
	P.r_u8					(ammoType);
	P.r_u8					(wstate);

	u8 Zoom;
	P.r_u8					(Zoom);

	if (H_Parent() && H_Parent()->Remote())
	{
		if (Zoom) 
			OnZoomIn();
		else 
			OnZoomOut();
	};
	switch (wstate)
	{	
	case eFire:
	case eFire2:
	case eSwitch:
	case eReload:
		{
		}break;	
	default:
		{
			if (ammoType >= m_ammoTypes.size())
				Msg("!! Weapon [%d], State - [%d]", ID(), wstate);
			else
			{
				m_ammoType = ammoType;
				SetAmmoElapsed((ammo_elapsed));
			}
		}break;
	}
	
	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeapon::save(NET_Packet &output_packet)
{
	inherited::save	(output_packet);
	save_data		(iAmmoElapsed,		output_packet);
	save_data		(m_flagsAddOnState, output_packet);
	save_data		(m_ammoType,		output_packet);
	save_data		(m_bZoomMode,		output_packet);
}

void CWeapon::load(IReader &input_packet)
{
	inherited::load	(input_packet);
	load_data		(iAmmoElapsed,		input_packet);
	load_data		(m_flagsAddOnState, input_packet);
	UpdateAddonsVisibility	();
	load_data		(m_ammoType,		input_packet);
	load_data		(m_bZoomMode,		input_packet);

	if (m_bZoomMode)	OnZoomIn();
		else			OnZoomOut();
}


void CWeapon::OnEvent(NET_Packet& P, u16 type) 
{
	switch (type)
	{
	case GE_ADDON_CHANGE:
		{
			P.r_u8					(m_flagsAddOnState);
			InitAddons();
			UpdateAddonsVisibility();
		}break;

	case GE_WPN_STATE_CHANGE:
		{
			u8				state;
			P.r_u8			(state);
			P.r_u8			(m_sub_state);		
//			u8 NewAmmoType = 
				P.r_u8();
			u8 AmmoElapsed = P.r_u8();
			u8 NextAmmo = P.r_u8();
			if (NextAmmo == u8(-1))
				m_set_next_ammoType_on_reload = u32(-1);
			else
				m_set_next_ammoType_on_reload = u8(NextAmmo);

			if (OnClient()) SetAmmoElapsed(int(AmmoElapsed));			
			OnStateSwitch	(u32(state));
		}
		break;
	default:
		{
			inherited::OnEvent(P,type);
		}break;
	}
};

void CWeapon::shedule_Update	(u32 dT)
{
	// Queue shrink
//	u32	dwTimeCL		= Level().timeServer()-NET_Latency;
//	while ((NET.size()>2) && (NET[1].dwTimeStamp<dwTimeCL)) NET.pop_front();	

	// Inherited
	inherited::shedule_Update	(dT);
}

void CWeapon::OnH_B_Independent	(bool just_before_destroy)
{
	RemoveShotEffector			();

	inherited::OnH_B_Independent(just_before_destroy);

	if (m_pHUD)
		m_pHUD->Hide			();

	//завершить принудительно все процессы что шли
	FireEnd();
	m_bPending = false;
	SwitchState(eIdle);

	m_strapped_mode				= false;
	SetHUDmode					(FALSE);
	OnZoomOut();
	m_fZoomRotationFactor	= 0.f;
	UpdateXForm					();

	m_nearwall_last_hud_fov = psHUD_FOV_def;
}

void CWeapon::OnH_A_Independent	()
{
	m_dwWeaponIndependencyTime = Level().timeServer();
	inherited::OnH_A_Independent();
	Light_Destroy				();
};

void CWeapon::OnH_A_Chield		()
{
	inherited::OnH_A_Chield		();

	UpdateAddonsVisibility		();
};

void CWeapon::OnActiveItem ()
{
	inherited::OnActiveItem		();
	//если мы занружаемся и оружие было в руках
	SetState					(eIdle);
	SetNextState				(eIdle);
	if (m_pHUD) m_pHUD->Show	();
}

void CWeapon::OnHiddenItem ()
{
	inherited::OnHiddenItem();
	if (m_pHUD)	m_pHUD->Hide ();
	SetState					(eHidden);
	SetNextState				(eHidden);
	m_set_next_ammoType_on_reload	= u32(-1);
}


void CWeapon::OnH_B_Chield		()
{
	m_dwWeaponIndependencyTime = 0;
	inherited::OnH_B_Chield		();

	OnZoomOut					();
	m_set_next_ammoType_on_reload	= u32(-1);

	m_nearwall_last_hud_fov = psHUD_FOV_def;
}

static float state_time = 0;				// таймер нахождения оружия в текущем состоянии
static float state_time_heat = 0;			// таймер нагрева оружия
static float previous_heating = 0;		// "нагретость" оружия в предыдущем состоянии

#include "WeaponBinoculars.h"

void CWeapon::UpdateWeaponParams()
{
#pragma todo("KD: переделать к чертовой матери этот тихий ужас")

	if (!IsHidden()) {
		w_states.x = m_fZoomRotationFactor;			//x = zoom mode, y - текущее состояние, z - старое состояние
		if ( psActorFlags.test( AF_DOF_SCOPE ) && !( IsZoomed() && !IsRotatingToZoom() && ZoomTexture() ) )
		  w_states.x = 0.f;
		if (w_states.y != GetState())	// первый апдейт или стейт изменился
		{
			w_states.z = w_states.y;						// записываем старое состояние
			state_time_heat = state_time = Device.fTimeGlobal;	// инитим счетчики времени
			previous_heating = w_timers.z;				// сохраняем "нагретость" оружия
			w_timers.y = w_timers.x;						// записываем время нахождения в предыдущем состоянии
			w_states.y = (float)GetState();				// обновляем состояние
		}
		// флаг бинокля в руках (в этом режиме не нужно размытие)
		if (smart_cast<CWeaponBinoculars*>(this))
			w_states.w = 0;
		else
			w_states.w = 1;
		if ((w_states.y == eFire) || (w_states.y == eFire2))	//стреляем, значит оружие греется
		{
			w_timers.z = Device.fTimeGlobal - state_time_heat + previous_heating;
		}
		else		// не стреляем - оружие охлаждается
		{
			if (w_timers.z > EPS)		// оружие все еще нагрето
			{
				float tm = state_time_heat + previous_heating - Device.fTimeGlobal;
				w_timers.z = (tm<EPS) ? 0.f : tm;
			}
		}
		w_timers.x = Device.fTimeGlobal - state_time;		// обновляем таймер текущего состояния
	}
}


u8 CWeapon::idle_state() {
  CActor *actor = smart_cast<CActor*>( H_Parent() );

  if ( actor )
    if ( actor->get_state() & mcSprint ) {
     return eSubstateIdleSprint;
    }
	else {
		if (actor->is_actor_running() || actor->is_actor_walking() || actor->is_actor_creeping() || actor->is_actor_crouching())
			return eSubstateIdleMoving;
	}

  return eIdle;
}


void CWeapon::UpdateCL		()
{
	inherited::UpdateCL		();
	UpdateHUDAddonsVisibility();
	//подсветка от выстрела
	UpdateLight				();

	if (ParentIsActor())
		UpdateWeaponParams();	// параметры для рендера оружия в режиме тепловидения

	//нарисовать партиклы
	UpdateFlameParticles	();
	UpdateFlameParticles2	();
	
	VERIFY(smart_cast<CKinematics*>(Visual()));

        if ( GetState() == eIdle ) {
          auto state = idle_state();
          if ( m_idle_state != state ) {
            m_idle_state = state;
            SwitchState( eIdle );
          }
        }
        else
          m_idle_state = eIdle;
}


void CWeapon::renderable_Render		()
{
	//KRodin: чтоб ствол в руках актора не был виден внутри 3D прицела.
	if (Device.m_SecondViewport.IsSVPFrame() && this->m_fZoomRotationFactor > 0.05f)
		return;

	UpdateXForm				();

	//нарисовать подсветку
	RenderLight				();	

	//если мы в режиме снайперки, то сам HUD рисовать не надо
	if(IsZoomed() && !IsRotatingToZoom() && ZoomTexture())
		m_bRenderHud = false;
	else
		m_bRenderHud = true;

	inherited::renderable_Render		();
}

void CWeapon::signal_HideComplete()
{
	if(H_Parent()) setVisible(FALSE);
	m_bPending = false;
	if(m_pHUD) m_pHUD->Hide();
}

void CWeapon::SetDefaults()
{
	bWorking2			= false;
	m_bPending			= false;

	m_flags.set			(FUsingCondition, TRUE);
	bMisfire			= false;
	m_flagsAddOnState	= 0;
	m_bZoomMode			= false;
}

void CWeapon::UpdatePosition(const Fmatrix& trans)
{
	Position().set		(trans.c);
	XFORM().mul			(trans,m_strapped_mode ? m_StrapOffset : m_Offset);
	VERIFY				(!fis_zero(DET(renderable.xform)));
}


bool CWeapon::Action(s32 cmd, u32 flags) 
{
	if(inherited::Action(cmd, flags)) return true;

	
	switch(cmd) 
	{
		case kWPN_FIRE: 
			{
				//если оружие чем-то занято, то ничего не делать
				{				
					if(flags&CMD_START) 
					{
						if(IsPending())		return false;
						FireStart			();
					}else 
						FireEnd();
				};

			} 
			return true;
		case kWPN_NEXT: 
			{
				if(IsPending() || OnClient()) 
				{
					return false;
				}
									
				if ( Core.Features.test(xrCore::Feature::lock_reload_in_sprint) && ParentIsActor() && g_actor->get_state() & mcSprint )
				  return true;

				if(flags&CMD_START) 
				{
					u32 l_newType = m_ammoType;
					bool b1,b2;
					do 
					{
						l_newType = (l_newType+1)%m_ammoTypes.size();
						b1 = l_newType != m_ammoType;
						b2 = unlimited_ammo() ? false : (!m_pCurrentInventory->GetAmmo(*m_ammoTypes[l_newType], ParentIsActor()));
					} while( b1 && b2);

					if(l_newType != m_ammoType) 
					{
						m_set_next_ammoType_on_reload = l_newType;						
/*						m_ammoType = l_newType;
						m_pAmmo = NULL;
						if (unlimited_ammo())
						{
							m_DefaultCartridge.Load(*m_ammoTypes[m_ammoType], u8(m_ammoType));
						};							
*/
						if(OnServer()) Reload();
					}
				}
			} 
            return true;

		case kWPN_ZOOM:
		{
			if (IsZoomEnabled())
			{
				if (flags&CMD_START && !IsPending())
					OnZoomIn();
				else if (IsZoomed())
					OnZoomOut();
				return true;
			}
			else
				return false;
		}
		case kWPN_ZOOM_INC:
		case kWPN_ZOOM_DEC:
		{
			if (IsZoomEnabled() && IsZoomed() && m_bScopeDynamicZoom && IsScopeAttached() && !is_second_zoom_offset_enabled && (flags&CMD_START))
			{
				// если в режиме ПГ - не будем давать использовать динамический зум
				if (IsGrenadeMode())
					return false;

				ZoomChange(cmd == kWPN_ZOOM_INC);

				return true;
			}
			else
				return false;
		}
		case kSWITCH_SCOPE: //KRodin: заюзаем эту кнопку, один хрен она только в мультиплеере нужна, а он вырезан.
		{
			if (flags&CMD_START)
			{
				this->SwitchScope();
			}
		}
	}
	return false;
}

void CWeapon::GetZoomData(const float scope_factor, float& delta, float& min_zoom_factor)
{
	float def_fov = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;
	float delta_factor_total = def_fov-scope_factor;
	VERIFY(delta_factor_total>0);
	min_zoom_factor = def_fov-delta_factor_total*m_fMinZoomK;
	delta = (delta_factor_total*(1-m_fMinZoomK) )/m_fZoomStepCount;
}

void CWeapon::ZoomChange(bool inc)
{
	bool wasChanged = false;

	if (SecondVPEnabled())
	{
		float delta, min_zoom_factor;
		GetZoomData(m_fSecondVPZoomFactor, delta, min_zoom_factor);

		const float currentZoomFactor = m_fRTZoomFactor;

		m_fRTZoomFactor += delta * (inc ? 1 : -1);
		clamp(m_fRTZoomFactor, min_zoom_factor, m_fSecondVPZoomFactor);

		wasChanged = !fsimilar(currentZoomFactor, m_fRTZoomFactor);
	}
	else
	{
		float delta, min_zoom_factor;
		GetZoomData(m_fScopeZoomFactor, delta, min_zoom_factor);

		const float currentZoomFactor = m_fZoomFactor;

		if (Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system)) {
			m_fZoomFactor += delta * (inc ? 1 : -1);
			clamp(m_fZoomFactor, min_zoom_factor, m_fScopeZoomFactor);
		}
		else {
			m_fZoomFactor -= delta * (inc ? 1 : -1);
			clamp(m_fZoomFactor, m_fScopeZoomFactor, min_zoom_factor);
		}

		wasChanged = !fsimilar(currentZoomFactor, m_fZoomFactor);

		if (H_Parent() && !IsRotatingToZoom() && !SecondVPEnabled())
			m_fRTZoomFactor = m_fZoomFactor; //store current
	}

	if (wasChanged)
	{
		OnZoomChanged();
	}
}

void CWeapon::SpawnAmmo(u32 boxCurr, LPCSTR ammoSect, u32 ParentID) 
{
	if(!m_ammoTypes.size())			return;
	if (OnClient())					return;
	m_bAmmoWasSpawned				= true;
	
	int l_type						= 0;
	l_type							%= m_ammoTypes.size();

	if(!ammoSect) ammoSect			= *m_ammoTypes[l_type]; 
	
	++l_type; 
	l_type							%= m_ammoTypes.size();

	CSE_Abstract *D					= F_entity_Create(ammoSect);

	if (D->m_tClassID==CLSID_OBJECT_AMMO	||
		D->m_tClassID==CLSID_OBJECT_A_M209	||
		D->m_tClassID==CLSID_OBJECT_A_VOG25	||
		D->m_tClassID==CLSID_OBJECT_A_OG7B)
	{	
		CSE_ALifeItemAmmo *l_pA		= smart_cast<CSE_ALifeItemAmmo*>(D);
		R_ASSERT					(l_pA);
		l_pA->m_boxSize				= (u16)pSettings->r_s32(ammoSect, "box_size");
		D->s_name					= ammoSect;
		D->set_name_replace			("");
		D->s_gameid					= u8(GameID());
		D->s_RP						= 0xff;
		D->ID						= 0xffff;
		if (ParentID == 0xffffffff)	
			D->ID_Parent			= (u16)H_Parent()->ID();
		else
			D->ID_Parent			= (u16)ParentID;

		D->ID_Phantom				= 0xffff;
		D->s_flags.assign			(M_SPAWN_OBJECT_LOCAL);
		D->RespawnTime				= 0;
		l_pA->m_tNodeID				= ai_location().level_vertex_id();

		if(boxCurr == 0xffffffff) 	
			boxCurr					= l_pA->m_boxSize;

		while(boxCurr) 
		{
			l_pA->a_elapsed			= (u16)(boxCurr > l_pA->m_boxSize ? l_pA->m_boxSize : boxCurr);
			NET_Packet				P;
			D->Spawn_Write			(P, TRUE);
			Level().Send			(P,net_flags(TRUE));

			if(boxCurr > l_pA->m_boxSize) 
				boxCurr				-= l_pA->m_boxSize;
			else 
				boxCurr				= 0;
		}
	};
	F_entity_Destroy				(D);
}

int CWeapon::GetAmmoCurrent(bool use_item_to_spawn) const
{
	int l_count = iAmmoElapsed;
	if(!m_pCurrentInventory) return l_count;

	//чтоб не делать лишних пересчетов
	if(m_pCurrentInventory->ModifyFrame()<=m_dwAmmoCurrentCalcFrame)
		return l_count + iAmmoCurrent;

 	m_dwAmmoCurrentCalcFrame = Device.dwFrame;
	iAmmoCurrent = 0;

	for(int i = 0; i < (int)m_ammoTypes.size(); ++i) 
	{
		LPCSTR l_ammoType = *m_ammoTypes[i];

		for(TIItemContainer::iterator l_it = m_pCurrentInventory->m_belt.begin(); m_pCurrentInventory->m_belt.end() != l_it; ++l_it) 
		{
			CWeaponAmmo *l_pAmmo = smart_cast<CWeaponAmmo*>(*l_it);

			if(l_pAmmo && !xr_strcmp(l_pAmmo->cNameSect(), l_ammoType)) 
			{
				iAmmoCurrent = iAmmoCurrent + l_pAmmo->m_boxCurr;
			}
		}

		bool include_ruck = true;

		auto parent = const_cast<CObject*>(H_Parent());
		auto pActor = smart_cast<CActor*>(parent);
		include_ruck = !psActorFlags.test(AF_AMMO_ON_BELT) || !pActor;

		if (include_ruck)
		{
			for (TIItemContainer::iterator l_it = m_pCurrentInventory->m_ruck.begin(); m_pCurrentInventory->m_ruck.end() != l_it; ++l_it)
			{
				CWeaponAmmo *l_pAmmo = smart_cast<CWeaponAmmo*>(*l_it);

				if (l_pAmmo && !xr_strcmp(l_pAmmo->cNameSect(), l_ammoType))
				{
					iAmmoCurrent = iAmmoCurrent + l_pAmmo->m_boxCurr;
				}
			}
		}

		if (!use_item_to_spawn)
			continue;

		if (!inventory_owner().item_to_spawn())
			continue;

		iAmmoCurrent += inventory_owner().ammo_in_box_to_spawn();
	}
	return l_count + iAmmoCurrent;
}

float CWeapon::GetConditionMisfireProbability() const
{
	if( GetCondition()>0.95f ) return 0.0f;

	float mis = misfireProbability+powf(1.f-GetCondition(), 3.f)*misfireConditionK;
	clamp(mis,0.0f,0.99f);
	return mis;
}

BOOL CWeapon::CheckForMisfire	()
{
	if (OnClient()) return FALSE;

	if ( Core.Features.test( xrCore::Feature::npc_simplified_shooting ) ) {
	  CActor *actor = smart_cast<CActor*>( H_Parent() );
	  if ( !actor ) return FALSE;
	}

	float rnd = ::Random.randF(0.f,1.f);
	float mp = GetConditionMisfireProbability();
	if(rnd < mp)
	{
		FireEnd();

		bMisfire = true;
		SwitchState(eMisfire);		
		
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

BOOL CWeapon::IsMisfire() const
{	
	return bMisfire;
}
void CWeapon::Reload()
{
	OnZoomOut();
}


bool CWeapon::IsGrenadeLauncherAttached() const
{
	return (CSE_ALifeItemWeapon::eAddonAttachable == m_eGrenadeLauncherStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)) || 
			CSE_ALifeItemWeapon::eAddonPermanent == m_eGrenadeLauncherStatus;
}

bool CWeapon::IsScopeAttached() const
{
	return (CSE_ALifeItemWeapon::eAddonAttachable == m_eScopeStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonScope)) || 
			CSE_ALifeItemWeapon::eAddonPermanent == m_eScopeStatus;

}

bool CWeapon::IsSilencerAttached() const
{
	return (CSE_ALifeItemWeapon::eAddonAttachable == m_eSilencerStatus &&
			0 != (m_flagsAddOnState&CSE_ALifeItemWeapon::eWeaponAddonSilencer)) || 
			CSE_ALifeItemWeapon::eAddonPermanent == m_eSilencerStatus;
}

bool CWeapon::GrenadeLauncherAttachable() const
{
	return (CSE_ALifeItemWeapon::eAddonAttachable == m_eGrenadeLauncherStatus);
}
bool CWeapon::ScopeAttachable() const
{
	return (CSE_ALifeItemWeapon::eAddonAttachable == m_eScopeStatus);
}
bool CWeapon::SilencerAttachable() const
{
	return (CSE_ALifeItemWeapon::eAddonAttachable == m_eSilencerStatus);
}

LPCSTR wpn_grenade_launcher = "wpn_grenade_launcher";

void CWeapon::UpdateHUDAddonsVisibility()
{//actor only
	if( H_Parent() != Level().CurrentEntity() )				return;
	if(m_pHUD->IsHidden())									return;
//	if(IsZoomed() && )


	CKinematics* pHudVisual									= smart_cast<CKinematics*>(m_pHUD->Visual());
	VERIFY(pHudVisual);
	if (H_Parent() != Level().CurrentEntity()) pHudVisual	= NULL;


	if (!pHudVisual)return;
	u16  bone_id;

	callback(GameObject::eOnUpdateHUDAddonsVisibiility)();

	bone_id = pHudVisual->LL_BoneID(*m_sWpn_scope_bone);
	if(ScopeAttachable())
	{
		VERIFY2(bone_id!=BI_NONE,"there are no scope bone.");
		if(IsScopeAttached())
		{
			if(FALSE==pHudVisual->LL_GetBoneVisible		(bone_id))
				pHudVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if(pHudVisual->LL_GetBoneVisible			(bone_id))
				pHudVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eScopeStatus==CSE_ALifeItemWeapon::eAddonDisabled && bone_id!=BI_NONE && 
		pHudVisual->LL_GetBoneVisible(bone_id) )
		pHudVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
	else
	if(m_eScopeStatus==CSE_ALifeItemWeapon::eAddonPermanent && bone_id!=BI_NONE && 
		!pHudVisual->LL_GetBoneVisible(bone_id) )
		pHudVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);


	bone_id = pHudVisual->LL_BoneID(*m_sWpn_silencer_bone);
	if(SilencerAttachable())
	{
		VERIFY2(bone_id!=BI_NONE,"there are no silencer bone.");
		if(IsSilencerAttached())
		{
			if(FALSE==pHudVisual->LL_GetBoneVisible		(bone_id))
				pHudVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if(pHudVisual->LL_GetBoneVisible			(bone_id))
				pHudVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eSilencerStatus==CSE_ALifeItemWeapon::eAddonDisabled && bone_id!=BI_NONE && 
		pHudVisual->LL_GetBoneVisible(bone_id) )
		pHudVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
	else
	if(m_eSilencerStatus==CSE_ALifeItemWeapon::eAddonPermanent && bone_id!=BI_NONE && 
		!pHudVisual->LL_GetBoneVisible(bone_id) )
		pHudVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);


	bone_id = pHudVisual->LL_BoneID(*m_sWpn_launcher_bone);
	if(GrenadeLauncherAttachable())
	{
		if(bone_id==BI_NONE)
			bone_id = pHudVisual->LL_BoneID(wpn_grenade_launcher);

		VERIFY2(bone_id!=BI_NONE,"there are no grenade launcher bone.");
		if(IsGrenadeLauncherAttached())
		{
			if(FALSE==pHudVisual->LL_GetBoneVisible		(bone_id))
				pHudVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if(pHudVisual->LL_GetBoneVisible			(bone_id))
				pHudVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eGrenadeLauncherStatus==CSE_ALifeItemWeapon::eAddonDisabled && bone_id!=BI_NONE && 
		pHudVisual->LL_GetBoneVisible(bone_id) )
		pHudVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
	else
	if(m_eGrenadeLauncherStatus==CSE_ALifeItemWeapon::eAddonPermanent && bone_id!=BI_NONE && 
		!pHudVisual->LL_GetBoneVisible(bone_id) )
		pHudVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);


}

void CWeapon::UpdateAddonsVisibility()
{
	CKinematics* pWeaponVisual = smart_cast<CKinematics*>(Visual()); R_ASSERT(pWeaponVisual);

	u16  bone_id;
	UpdateHUDAddonsVisibility								();	

	callback(GameObject::eOnUpdateAddonsVisibiility)();

	bone_id = pWeaponVisual->LL_BoneID					(*m_sWpn_scope_bone);
	if(ScopeAttachable())
	{
		if(IsScopeAttached())
		{
			if(FALSE==pWeaponVisual->LL_GetBoneVisible		(bone_id))
			pWeaponVisual->LL_SetBoneVisible				(bone_id,TRUE,TRUE);
		}else{
			if(pWeaponVisual->LL_GetBoneVisible				(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eScopeStatus==CSE_ALifeItemWeapon::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )

		pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);

	bone_id = pWeaponVisual->LL_BoneID					(*m_sWpn_silencer_bone);
	if(SilencerAttachable())
	{
		if(IsSilencerAttached()){
			if(FALSE==pWeaponVisual->LL_GetBoneVisible		(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if( pWeaponVisual->LL_GetBoneVisible				(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eSilencerStatus==CSE_ALifeItemWeapon::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )

		pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);

	bone_id = pWeaponVisual->LL_BoneID					(*m_sWpn_launcher_bone);
	if(GrenadeLauncherAttachable())
	{
		if(IsGrenadeLauncherAttached())
		{
			if(FALSE==pWeaponVisual->LL_GetBoneVisible		(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,TRUE,TRUE);
		}else{
			if(pWeaponVisual->LL_GetBoneVisible				(bone_id))
				pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
		}
	}
	if(m_eGrenadeLauncherStatus==CSE_ALifeItemWeapon::eAddonDisabled && bone_id!=BI_NONE && 
		pWeaponVisual->LL_GetBoneVisible(bone_id) )

		pWeaponVisual->LL_SetBoneVisible			(bone_id,FALSE,TRUE);
	
	pWeaponVisual->CalculateBones_Invalidate				();
	pWeaponVisual->CalculateBones							();
}

bool CWeapon::Activate() 
{
	UpdateAddonsVisibility();
	return inherited::Activate();
}

void CWeapon::InitAddons()
{
}

float CWeapon::CurrentZoomFactor()
{
	if (Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system)) {
		if (is_second_zoom_offset_enabled)
			return m_fSecondScopeZoomFactor;
		else if (SecondVPEnabled())
			return 1; // no change to main fov zoom when use second vp
		else if (IsScopeAttached())
			return m_fScopeZoomFactor;
		else
			return m_fIronSightZoomFactor;
	}
	else {
		return IsScopeAttached() ? m_fScopeZoomFactor : m_fIronSightZoomFactor;
	}
}

void CWeapon::OnZoomIn()
{
	m_bZoomMode = true;

	// если в режиме ПГ - не будем давать включать динамический зум
	if ( m_bScopeDynamicZoom && !IsGrenadeMode() && !SecondVPEnabled())
		m_fZoomFactor = m_fRTZoomFactor;
	else
		m_fZoomFactor = CurrentZoomFactor();

	if (!m_bZoomInertionAllow)
	{
		StopHudInertion();
	}

	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if ( pActor )
		pActor->callback(GameObject::eOnActorWeaponZoomIn)(lua_game_object());
}

void CWeapon::OnZoomOut()
{
	m_fZoomFactor = Core.Features.test(xrCore::Feature::ogse_wpn_zoom_system) ? 1.f : g_fov;

	if ( m_bZoomMode ) {

		m_bZoomMode = false;

		CActor* pActor = smart_cast<CActor*>(H_Parent());
		if ( pActor ) {
			w_states.set( 0.f, 0.f, 0.f, 1.f );
			pActor->callback(GameObject::eOnActorWeaponZoomOut)(lua_game_object());
		}
	}

	StartHudInertion();
}

bool CWeapon::UseScopeTexture() {
	return (( GetAddonsState() & CSE_ALifeItemWeapon::eForcedNotexScope ) == 0) 
		&& !is_second_zoom_offset_enabled
		&& !SecondVPEnabled()
		&& m_UIScope; // только если есть текстура прицела - для простого создания коллиматоров
};

CUIStaticItem* CWeapon::ZoomTexture()
{
	if (UseScopeTexture())
		return m_UIScope;
	else
		return NULL;
}

void CWeapon::SwitchState(u32 S)
{
	if (OnClient()) return;

	SetNextState		( S );	// Very-very important line of code!!! :)
	if (CHudItem::object().Local() && !CHudItem::object().getDestroy()/* && (S!=NEXT_STATE)*/ 
		&& m_pCurrentInventory && OnServer())	
	{
		// !!! Just single entry for given state !!!
		NET_Packet		P;
		CHudItem::object().u_EventGen		(P,GE_WPN_STATE_CHANGE,CHudItem::object().ID());
		P.w_u8			(u8(S));
		P.w_u8			(u8(m_sub_state));
		P.w_u8			(u8(m_ammoType& 0xff));
		P.w_u8			(u8(iAmmoElapsed & 0xff));
		P.w_u8			(u8(m_set_next_ammoType_on_reload & 0xff));
		CHudItem::object().u_EventSend		(P, net_flags(TRUE, TRUE, FALSE, TRUE));
	}
}

void CWeapon::OnMagazineEmpty	()
{
	VERIFY((u32)iAmmoElapsed == m_magazine.size());
}


void CWeapon::reinit			()
{
	CShootingObject::reinit		();
	CHudItemObject::reinit			();
}

void CWeapon::reload			(LPCSTR section)
{
	CShootingObject::reload		(section);
	CHudItemObject::reload			(section);
	
	m_can_be_strapped			= true;
	m_strapped_mode				= false;
	
	if (pSettings->line_exist(section,"strap_bone0"))
		m_strap_bone0			= pSettings->r_string(section,"strap_bone0");
	else
		m_can_be_strapped		= false;
	
	if (pSettings->line_exist(section,"strap_bone1"))
		m_strap_bone1			= pSettings->r_string(section,"strap_bone1");
	else
		m_can_be_strapped		= false;

	if (m_eScopeStatus == ALife::eAddonAttachable) {
		m_addon_holder_range_modifier	= READ_IF_EXISTS(pSettings,r_float,m_sScopeName,"holder_range_modifier",m_holder_range_modifier);
		m_addon_holder_fov_modifier		= READ_IF_EXISTS(pSettings,r_float,m_sScopeName,"holder_fov_modifier",m_holder_fov_modifier);
	}
	else {
		m_addon_holder_range_modifier	= m_holder_range_modifier;
		m_addon_holder_fov_modifier		= m_holder_fov_modifier;
	}


	{
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"position");
		ypr					= pSettings->r_fvector3		(section,"orientation");
		ypr.mul				(PI/180.f);

		m_Offset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_Offset.translate_over	(pos);
	}

	m_StrapOffset			= m_Offset;
	if (pSettings->line_exist(section,"strap_position") && pSettings->line_exist(section,"strap_orientation")) {
		Fvector				pos,ypr;
		pos					= pSettings->r_fvector3		(section,"strap_position");
		ypr					= pSettings->r_fvector3		(section,"strap_orientation");
		ypr.mul				(PI/180.f);

		m_StrapOffset.setHPB			(ypr.x,ypr.y,ypr.z);
		m_StrapOffset.translate_over	(pos);
	}
	else
		m_can_be_strapped	= false;

	m_ef_main_weapon_type	= READ_IF_EXISTS(pSettings,r_u32,section,"ef_main_weapon_type",u32(-1));
	m_ef_weapon_type		= READ_IF_EXISTS(pSettings,r_u32,section,"ef_weapon_type",u32(-1));
}

void CWeapon::create_physic_shell()
{
	CPhysicsShellHolder::create_physic_shell();
}

void CWeapon::activate_physic_shell()
{
	CPhysicsShellHolder::activate_physic_shell();
}

void CWeapon::setup_physic_shell()
{
	CPhysicsShellHolder::setup_physic_shell();
}

int		g_iWeaponRemove = 1;

bool CWeapon::NeedToDestroyObject()	const
{
	if (H_Parent()) return false;
	if (g_iWeaponRemove == -1) return false;
	if (g_iWeaponRemove == 0) return true;
	return (TimePassedAfterIndependant() > m_dwWeaponRemoveTime);
}

ALife::_TIME_ID	 CWeapon::TimePassedAfterIndependant()	const
{
	if(!H_Parent() && m_dwWeaponIndependencyTime != 0)
		return Level().timeServer() - m_dwWeaponIndependencyTime;
	else
		return 0;
}

bool CWeapon::can_kill	() const
{
	if (GetAmmoCurrent(true) || m_ammoTypes.empty())
		return				(true);

	return					(false);
}

CInventoryItem *CWeapon::can_kill	(CInventory *inventory) const
{
	if (GetAmmoElapsed() || m_ammoTypes.empty())
		return				(const_cast<CWeapon*>(this));

	TIItemContainer::iterator I = inventory->m_all.begin();
	TIItemContainer::iterator E = inventory->m_all.end();
	for ( ; I != E; ++I) {
		CInventoryItem	*inventory_item = smart_cast<CInventoryItem*>(*I);
		if (!inventory_item)
			continue;
		
		xr_vector<shared_str>::const_iterator	i = std::find(m_ammoTypes.begin(),m_ammoTypes.end(),inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return			(inventory_item);
	}

	return					(0);
}

const CInventoryItem *CWeapon::can_kill	(const xr_vector<const CGameObject*> &items) const
{
	if (m_ammoTypes.empty())
		return				(this);

	xr_vector<const CGameObject*>::const_iterator I = items.begin();
	xr_vector<const CGameObject*>::const_iterator E = items.end();
	for ( ; I != E; ++I) {
		const CInventoryItem	*inventory_item = smart_cast<const CInventoryItem*>(*I);
		if (!inventory_item)
			continue;

		xr_vector<shared_str>::const_iterator	i = std::find(m_ammoTypes.begin(),m_ammoTypes.end(),inventory_item->object().cNameSect());
		if (i != m_ammoTypes.end())
			return			(inventory_item);
	}

	return					(0);
}

bool CWeapon::ready_to_kill	() const
{
	return					(
		!IsMisfire() && 
		((GetState() == eIdle) || (GetState() == eFire) || (GetState() == eFire2)) && 
		GetAmmoElapsed()
	);
}


void CWeapon::UpdateHudAdditonal		(Fmatrix& trans)
{
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if(!pActor) return;

	if(		(pActor->IsZoomAimingMode() && m_fZoomRotationFactor<=1.f) ||
			(!pActor->IsZoomAimingMode() && m_fZoomRotationFactor>0.f))
	{
		Fmatrix hud_rotation;
		hud_rotation.identity();
		hud_rotation.rotateX(m_pHUD->ZoomRotateX()*m_fZoomRotationFactor);

		Fmatrix hud_rotation_y;
		hud_rotation_y.identity();
		hud_rotation_y.rotateY(m_pHUD->ZoomRotateY()*m_fZoomRotationFactor);
		hud_rotation.mulA_43(hud_rotation_y);

		Fvector offset = m_pHUD->ZoomOffset();
		offset.mul					(m_fZoomRotationFactor);
		hud_rotation.translate_over	(offset);
		trans.mulB_43				(hud_rotation);

		if(pActor->IsZoomAimingMode())
		{
			m_fZoomRotationFactor += Device.fTimeDelta/m_fZoomRotateTime;
		}
		else
		{
			m_fZoomRotationFactor -= Device.fTimeDelta/m_fZoomRotateTime;
		}
		clamp(m_fZoomRotationFactor, 0.f, 1.f);
	}
}

void	CWeapon::SetAmmoElapsed	(int ammo_count)
{
	iAmmoElapsed				= ammo_count;

	u32 uAmmo					= u32(iAmmoElapsed);

	if (uAmmo != m_magazine.size())
	{
		if (uAmmo > m_magazine.size())
		{
			CCartridge			l_cartridge; 
			l_cartridge.Load	(*m_ammoTypes[m_ammoType], u8(m_ammoType));
			while (uAmmo > m_magazine.size())
				m_magazine.push_back(l_cartridge);
		}
		else
		{
			while (uAmmo < m_magazine.size())
				m_magazine.pop_back();
		};
	};
}

u32	CWeapon::ef_main_weapon_type	() const
{
	VERIFY	(m_ef_main_weapon_type != u32(-1));
	return	(m_ef_main_weapon_type);
}

u32	CWeapon::ef_weapon_type	() const
{
	VERIFY	(m_ef_weapon_type != u32(-1));
	return	(m_ef_weapon_type);
}

bool CWeapon::IsNecessaryItem	    (const shared_str& item_sect)
{
	return (std::find(m_ammoTypes.begin(), m_ammoTypes.end(), item_sect) != m_ammoTypes.end() );
}

void CWeapon::modify_holder_params		(float &range, float &fov) const
{
	if (!IsScopeAttached()) {
		inherited::modify_holder_params	(range,fov);
		return;
	}
	range	*= m_addon_holder_range_modifier;
	fov		*= m_addon_holder_fov_modifier;
}

void CWeapon::OnDrawUI()
{
	if(IsZoomed() && ZoomHideCrosshair()){
		if(ZoomTexture() && !IsRotatingToZoom()){
			ZoomTexture()->SetPos	(0,0);
			ZoomTexture()->SetRect	(0,0,UI_BASE_WIDTH, UI_BASE_HEIGHT);
			ZoomTexture()->Render	();

//			m_UILens.Draw();
		}
	}
}

bool CWeapon::unlimited_ammo() 
{ 
	if (m_pCurrentInventory)
		return inventory_owner().unlimited_ammo() && m_DefaultCartridge.m_flags.test(CCartridge::cfCanBeUnlimited);
	else
		return false;
};

LPCSTR	CWeapon::GetCurrentAmmo_ShortName	()
{
	if (m_magazine.empty()) return ("");
	CCartridge &l_cartridge = m_magazine.back();
	return *(l_cartridge.m_InvShortName);
}

float CWeapon::GetMagazineWeight(const decltype(CWeapon::m_magazine)& mag) const
{
    float res = 0;
    const char* last_type = nullptr;
    float last_ammo_weight = 0;
    for (auto& c : mag)
    {
        // Usually ammos in mag have same type, use this fact to improve performance
        if (last_type != c.m_ammoSect.c_str())
        {
            last_type = c.m_ammoSect.c_str();
            last_ammo_weight = c.Weight();
        }
        res += last_ammo_weight;
    }
    return res;
}

float CWeapon::Weight() const
{
	float res = CInventoryItemObject::Weight();
	if ( GrenadeLauncherAttachable() && IsGrenadeLauncherAttached() )
		res += pSettings->r_float(GetGrenadeLauncherName(),"inv_weight");
	if ( ScopeAttachable() && IsScopeAttached() )
		res += pSettings->r_float(GetScopeName(),"inv_weight");
	if ( SilencerAttachable() && IsSilencerAttached() )
		res += pSettings->r_float(GetSilencerName(),"inv_weight");
	res += GetMagazineWeight(m_magazine);

	return res;
}

u32 CWeapon::Cost() const
{
	u32 res = m_cost;
	
	if (Core.Features.test(xrCore::Feature::wpn_cost_include_addons)) {
		if (GrenadeLauncherAttachable() && IsGrenadeLauncherAttached())
			res += pSettings->r_u32(GetGrenadeLauncherName(), "cost");
		if (ScopeAttachable() && IsScopeAttached())
			res += pSettings->r_u32(GetScopeName(), "cost");
		if (SilencerAttachable() && IsSilencerAttached())
			res += pSettings->r_u32(GetSilencerName(), "cost");
	}
	return res;
}

void CWeapon::Hide		()
{
	SwitchState(eHiding);

	OnZoomOut();
}

void CWeapon::Show		()
{
	SwitchState(eShowing);
}

bool CWeapon::show_crosshair()
{
	return ! ( IsZoomed() && ZoomHideCrosshair() );
}

bool CWeapon::show_indicators()
{
	return ! ( IsZoomed() && ZoomTexture() );
}

float CWeapon::GetConditionToShow	() const
{
	return	(GetCondition());//powf(GetCondition(),4.0f));
}

BOOL CWeapon::ParentMayHaveAimBullet	()
{
	CObject* O=H_Parent();
	if (!O) return FALSE;
	CEntityAlive* EA=smart_cast<CEntityAlive*>(O);
	return EA->cast_actor()!=0;
}

BOOL CWeapon::ParentIsActor	()
{
	CObject* O=H_Parent();
	if (!O) return FALSE;
	CEntityAlive* EA=smart_cast<CEntityAlive*>(O);
	if (!EA) return FALSE;
	return EA->cast_actor()!=0;
}

const float &CWeapon::hit_probability	() const
{
	VERIFY					((g_SingleGameDifficulty >= egdNovice) && (g_SingleGameDifficulty <= egdMaster)); 
	return					(m_hit_probability[egdNovice]);
}

// Function for callbacks added by Cribbledirge.
void CWeapon::StateSwitchCallback(GameObject::ECallbackType actor_type, GameObject::ECallbackType npc_type)
{
	if (g_actor)
	{
		if (smart_cast<CActor*>(H_Parent()))  // This is an actor.
		{
			Actor()->callback(actor_type)(
				lua_game_object()  // The weapon as a game object.
			);
		}
		else if (smart_cast<CEntityAlive*>(H_Parent()))  // This is an NPC.
		{
			Actor()->callback(npc_type)(
				smart_cast<CEntityAlive*>(H_Parent())->lua_game_object(),       // The owner of the weapon.
				lua_game_object()                                              // The weapon itself.
			);
		}
	}
}

// Обновление необходимости включения второго вьюпорта +SecondVP+
// Вызывается только для активного оружия игрока
void CWeapon::UpdateSecondVP()
{
	// + CActor::UpdateCL();
	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if (!pActor)
		return;

	CInventoryOwner* inv_owner = pActor->cast_inventory_owner();

	bool b_is_active_item = inv_owner && (inv_owner->m_inventory->ActiveItem() == this);
	R_ASSERT(b_is_active_item); // Эта функция должна вызываться только для оружия в руках нашего игрока

	bool bCond_1 = m_fZoomRotationFactor > 0.05f;    // Мы должны целиться
	bool bCond_3 = pActor->cam_Active() == pActor->cam_FirstEye(); // Мы должны быть от 1-го лица

	Device.m_SecondViewport.SetSVPActive(bCond_1 && bCond_3 && SecondVPEnabled());
}

bool CWeapon::SecondVPEnabled() const
{	
	bool bCond_2 = m_fSecondVPZoomFactor > 0.0f;     // В конфиге должен быть прописан фактор зума (scope_lense_fov_factor) больше чем 0
	bool bCond_4 = !IsGrenadeMode();     // Мы не должны быть в режиме подствольника
	bool bCond_5 = !is_second_zoom_offset_enabled; // Мы не должны быть в режиме второго прицеливания.
	bool bcond_6 = psActorFlags.test(AF_3D_SCOPES);

	return bCond_2 && bCond_4 && bCond_5 && bcond_6;
}

// Чувствительность мышкии с оружием в руках во время прицеливания
float CWeapon::GetControlInertionFactor() const
{
	float fInertionFactor = inherited::GetControlInertionFactor();

	if (IsZoomed() && SecondVPEnabled() && !IsRotatingToZoom())
	{
		if (m_bScopeDynamicZoom)
		{
			const float delta_factor_total = 1 - m_fSecondVPZoomFactor;
			float min_zoom_factor = 1 + delta_factor_total * m_fMinZoomK;
			float k = (m_fRTZoomFactor - min_zoom_factor) / (m_fSecondVPZoomFactor - min_zoom_factor);
			return (m_fScopeInertionFactor - fInertionFactor) * k + fInertionFactor;
		}
		else
			return m_fScopeInertionFactor;
	}

	return fInertionFactor;
}

float CWeapon::GetSecondVPFov() const
{
	float fov_factor = m_fSecondVPZoomFactor;
	if (m_bScopeDynamicZoom)
	{
		fov_factor = m_fRTZoomFactor;
	}
	return float(atan(tan(g_fov * (0.5 * PI / 180)) / fov_factor) / (0.5 * PI / 180)); //-V595
}

bool CWeapon::IsGrenadeMode() const
{
	const auto wpn_w_gl = smart_cast<const CWeaponMagazinedWGrenade*>(this);
	return wpn_w_gl && wpn_w_gl->m_bGrenadeMode;
}

// Получить HUD FOV от текущего оружия игрока
float CWeapon::GetHudFov()
{
	// Рассчитываем HUD FOV от бедра (с учётом упирания в стены)
	if (m_nearwall_on && ParentIsActor() && Level().CurrentViewEntity() == H_Parent())
	{
		// Получаем расстояние от камеры до точки в прицеле
		collide::rq_result& RQ = HUD().GetCurrentRayQuery();
		float dist = RQ.range;

		// Интерполируем расстояние в диапазон от 0 (min) до 1 (max)
		clamp(dist, m_nearwall_dist_min, m_nearwall_dist_max);
		float fDistanceMod = ((dist - m_nearwall_dist_min) / (m_nearwall_dist_max - m_nearwall_dist_min)); // 0.f ... 1.f

		 // Рассчитываем базовый HUD FOV от бедра
		float fBaseFov = psHUD_FOV_def;
		clamp(fBaseFov, 0.0f, FLT_MAX);

		// Плавно высчитываем итоговый FOV от бедра
		float src = m_nearwall_speed_mod * Device.fTimeDelta;
		clamp(src, 0.f, 1.f);

		float fTrgFov = m_nearwall_target_hud_fov + fDistanceMod * (fBaseFov - m_nearwall_target_hud_fov);
		m_nearwall_last_hud_fov = m_nearwall_last_hud_fov * (1 - src) + fTrgFov * src;
	}

	if (m_fZoomRotationFactor > 0.0f)
	{
		if (SecondVPEnabled() && m_fSecondVPHudFov > 0.0f)
		{
			// В линзе зума
			float fDiff = m_nearwall_last_hud_fov - m_fSecondVPHudFov;
			return m_fSecondVPHudFov + (fDiff * (1 - m_fZoomRotationFactor));
		}
		if (!UseScopeTexture() && m_fZoomHudFov > 0.0f)
		{
			// В процессе зума
			float fDiff = m_nearwall_last_hud_fov - m_fZoomHudFov;
			return m_fZoomHudFov + (fDiff * (1 - m_fZoomRotationFactor));
		}
	}

	// От бедра	 
	return m_nearwall_last_hud_fov;
}
