/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/generated/client_data.h>
#include <game/generated/protocol.h>

#include <game/layers.h>
#include <game/localization.h>

#include <cmath>

#include "binds.h"
#include "camera.h"
#include "controls.h"
#include "hud.h"
#include "voting.h"

CHud::CHud()
{
	// won't work if zero
	m_FrameTimeAvg = 0.0f;
	m_FPSTextContainerIndex.Reset();
	m_DDRaceEffectsTextContainerIndex.Reset();
}

void CHud::ResetHudContainers()
{
	for(auto &ScoreInfo : m_aScoreInfo)
	{
		TextRender()->DeleteTextContainer(ScoreInfo.m_OptionalNameTextContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextRankContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextScoreContainerIndex);
		Graphics()->DeleteQuadContainer(ScoreInfo.m_RoundRectQuadContainerIndex);

		ScoreInfo.Reset();
	}

	TextRender()->DeleteTextContainer(m_FPSTextContainerIndex);
	TextRender()->DeleteTextContainer(m_DDRaceEffectsTextContainerIndex);
}

void CHud::OnWindowResize()
{
	ResetHudContainers();
}

void CHud::OnReset()
{
	m_TimeCpDiff = 0.0f;
	m_DDRaceTime = 0;
	m_FinishTimeLastReceivedTick = 0;
	m_TimeCpLastReceivedTick = 0;
	m_ShowFinishTime = false;
	m_ServerRecord = -1.0f;
	m_aPlayerRecord[0] = -1.0f;
	m_aPlayerRecord[1] = -1.0f;

	ResetHudContainers();
}

void CHud::OnInit()
{
	OnReset();

	m_HudQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	PrepareAmmoHealthAndArmorQuads();

	// all cursors for the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		float ScaleX, ScaleY;
		RenderTools()->GetSpriteScale(g_pData->m_Weapons.m_aId[i].m_pSpriteCursor, ScaleX, ScaleY);
		m_aCursorOffset[i] = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 64.f * ScaleX, 64.f * ScaleY);
	}

	// the flags
	m_FlagOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 8.f, 16.f);

	PreparePlayerStateQuads();

	Graphics()->QuadContainerUpload(m_HudQuadContainerIndex);
}

void CHud::RenderGameTimer()
{
	float Half = m_Width / 2.0f;

	if(!(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH))
	{
		char aBuf[32];
		int Time = 0;
		if(m_pClient->m_Snap.m_pGameInfoObj->m_TimeLimit && (m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			Time = m_pClient->m_Snap.m_pGameInfoObj->m_TimeLimit * 60 - ((Client()->GameTick(g_Config.m_ClDummy) - m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed());

			if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
				Time = 0;
		}
		else if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
		{
			// The Warmup timer is negative in this case to make sure that incompatible clients will not see a warmup timer
			Time = (Client()->GameTick(g_Config.m_ClDummy) + m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();
		}
		else
			Time = (Client()->GameTick(g_Config.m_ClDummy) - m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed();

		str_time((int64_t)Time * 100, TIME_DAYS, aBuf, sizeof(aBuf));
		float FontSize = 10.0f;
		static float s_TextWidthM = TextRender()->TextWidth(FontSize, "00:00", -1, -1.0f);
		static float s_TextWidthH = TextRender()->TextWidth(FontSize, "00:00:00", -1, -1.0f);
		static float s_TextWidth0D = TextRender()->TextWidth(FontSize, "0d 00:00:00", -1, -1.0f);
		static float s_TextWidth00D = TextRender()->TextWidth(FontSize, "00d 00:00:00", -1, -1.0f);
		static float s_TextWidth000D = TextRender()->TextWidth(FontSize, "000d 00:00:00", -1, -1.0f);
		float w = Time >= 3600 * 24 * 100 ? s_TextWidth000D : Time >= 3600 * 24 * 10 ? s_TextWidth00D : Time >= 3600 * 24 ? s_TextWidth0D : Time >= 3600 ? s_TextWidthH : s_TextWidthM;
		// last 60 sec red, last 10 sec blink
		if(m_pClient->m_Snap.m_pGameInfoObj->m_TimeLimit && Time <= 60 && (m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			float Alpha = Time <= 10 && (2 * time() / time_freq()) % 2 ? 0.5f : 1.0f;
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, Alpha);
		}
		TextRender()->Text(Half - w / 2, 2, FontSize, aBuf, -1.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CHud::RenderPauseNotification()
{
	if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED &&
		!(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		const char *pText = Localize("Game paused");
		float FontSize = 20.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(150.0f * Graphics()->ScreenAspect() + -w / 2.0f, 50.0f, FontSize, pText, -1.0f);
	}
}

void CHud::RenderSuddenDeath()
{
	if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH)
	{
		float Half = m_Width / 2.0f;
		const char *pText = Localize("Sudden Death");
		float FontSize = 12.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(Half - w / 2, 2, FontSize, pText, -1.0f);
	}
}

void CHud::RenderCenterLines()
{
	if(m_pClient->m_Scoreboard.Active())
		return;

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(1.0f, 0.0f, 0.0f, 1.0f);

	IGraphics::CLineItem Array[2] = {
		IGraphics::CLineItem(m_Width / 2.0, 0.0f, m_Width / 2.0, m_Height),
		IGraphics::CLineItem(0.0f, m_Height / 2.0, m_Width, m_Height / 2.0f)};

	Graphics()->LinesDraw(Array, 2);

	Graphics()->LinesEnd();
}

void CHud::RenderScoreHud()
{
	// render small score hud
	if(!(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		int GameFlags = m_pClient->m_Snap.m_pGameInfoObj->m_GameFlags;
		float StartY = 229.0f; // the height of this display is 56, so EndY is 285

		const float ScoreSingleBoxHeight = 18.0f;

		bool ForceScoreInfoInit = !m_aScoreInfo[0].m_Initialized || !m_aScoreInfo[1].m_Initialized;
		m_aScoreInfo[0].m_Initialized = m_aScoreInfo[1].m_Initialized = true;

		if(GameFlags & GAMEFLAG_TEAMS && m_pClient->m_Snap.m_pGameDataObj)
		{
			char aScoreTeam[2][16];
			str_from_int(m_pClient->m_Snap.m_pGameDataObj->m_TeamscoreRed, aScoreTeam[TEAM_RED]);
			str_from_int(m_pClient->m_Snap.m_pGameDataObj->m_TeamscoreBlue, aScoreTeam[TEAM_BLUE]);

			bool aRecreateTeamScore[2] = {str_comp(aScoreTeam[0], m_aScoreInfo[0].m_aScoreText) != 0, str_comp(aScoreTeam[1], m_aScoreInfo[1].m_aScoreText) != 0};

			const int aFlagCarrier[2] = {
				m_pClient->m_Snap.m_pGameDataObj->m_FlagCarrierRed,
				m_pClient->m_Snap.m_pGameDataObj->m_FlagCarrierBlue};

			bool RecreateRect = ForceScoreInfoInit;
			for(int t = 0; t < 2; t++)
			{
				if(aRecreateTeamScore[t])
				{
					m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(14.0f, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE], -1, -1.0f);
					mem_copy(m_aScoreInfo[t].m_aScoreText, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE], sizeof(m_aScoreInfo[t].m_aScoreText));
					RecreateRect = true;
				}
			}

			static float s_TextWidth100 = TextRender()->TextWidth(14.0f, "100", -1, -1.0f);
			float ScoreWidthMax = maximum(maximum(m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth), s_TextWidth100);
			float Split = 3.0f;
			float ImageSize = (GameFlags & GAMEFLAG_FLAGS) ? 16.0f : Split;
			for(int t = 0; t < 2; t++)
			{
				// draw box
				if(RecreateRect)
				{
					Graphics()->DeleteQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex);

					if(t == 0)
						Graphics()->SetColor(1.0f, 0.0f, 0.0f, 0.25f);
					else
						Graphics()->SetColor(0.0f, 0.0f, 1.0f, 0.25f);
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(m_Width - ScoreWidthMax - ImageSize - 2 * Split, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split, ScoreSingleBoxHeight, 5.0f, IGraphics::CORNER_L);
				}
				Graphics()->TextureClear();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);

				// draw score
				if(aRecreateTeamScore[t])
				{
					CTextCursor Cursor;
					TextRender()->SetCursor(&Cursor, m_Width - ScoreWidthMax + (ScoreWidthMax - m_aScoreInfo[t].m_ScoreTextWidth) / 2 - Split, StartY + t * 20 + (18.f - 14.f) / 2.f, 14.0f, TEXTFLAG_RENDER);
					Cursor.m_LineWidth = -1;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScoreTeam[t]);
				}
				if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
				}

				if(GameFlags & GAMEFLAG_FLAGS)
				{
					int BlinkTimer = (m_pClient->m_aFlagDropTick[t] != 0 &&
								 (Client()->GameTick(g_Config.m_ClDummy) - m_pClient->m_aFlagDropTick[t]) / Client()->GameTickSpeed() >= 25) ?
                                                                 10 :
                                                                 20;
					if(aFlagCarrier[t] == FLAG_ATSTAND || (aFlagCarrier[t] == FLAG_TAKEN && ((Client()->GameTick(g_Config.m_ClDummy) / BlinkTimer) & 1)))
					{
						// draw flag
						Graphics()->TextureSet(t == 0 ? m_pClient->m_GameSkin.m_SpriteFlagRed : m_pClient->m_GameSkin.m_SpriteFlagBlue);
						Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
						Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_FlagOffset, m_Width - ScoreWidthMax - ImageSize, StartY + 1.0f + t * 20);
					}
					else if(aFlagCarrier[t] >= 0)
					{
						// draw name of the flag holder
						int ID = aFlagCarrier[t] % MAX_CLIENTS;
						const char *pName = m_pClient->m_aClients[ID].m_aName;
						if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0 || RecreateRect)
						{
							mem_copy(m_aScoreInfo[t].m_aPlayerNameText, pName, sizeof(m_aScoreInfo[t].m_aPlayerNameText));

							float w = TextRender()->TextWidth(8.0f, pName, -1, -1.0f);

							CTextCursor Cursor;
							TextRender()->SetCursor(&Cursor, minimum(m_Width - w - 1.0f, m_Width - ScoreWidthMax - ImageSize - 2 * Split), StartY + (t + 1) * 20.0f - 2.0f, 8.0f, TEXTFLAG_RENDER);
							Cursor.m_LineWidth = -1;
							TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
						}

						if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
						{
							ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
							ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
							TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
						}

						// draw tee of the flag holder
						CTeeRenderInfo TeeInfo = m_pClient->m_aClients[ID].m_RenderInfo;
						TeeInfo.m_Size = ScoreSingleBoxHeight;

						
						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						RenderTools()->GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(m_Width - ScoreWidthMax - TeeInfo.m_Size / 2 - Split, StartY + (t * 20) + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);

						RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
					}
				}
				StartY += 8.0f;
			}
		}
		else
		{
			int Local = -1;
			int aPos[2] = {1, 2};
			const CNetObj_PlayerInfo *apPlayerInfo[2] = {0, 0};
			int i = 0;
			for(int t = 0; t < 2 && i < MAX_CLIENTS && m_pClient->m_Snap.m_apInfoByScore[i]; ++i)
			{
				if(m_pClient->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
				{
					apPlayerInfo[t] = m_pClient->m_Snap.m_apInfoByScore[i];
					if(apPlayerInfo[t]->m_ClientID == m_pClient->m_Snap.m_LocalClientID)
						Local = t;
					++t;
				}
			}
			// search local player info if not a spectator, nor within top2 scores
			if(Local == -1 && m_pClient->m_Snap.m_pLocalInfo && m_pClient->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS)
			{
				for(; i < MAX_CLIENTS && m_pClient->m_Snap.m_apInfoByScore[i]; ++i)
				{
					if(m_pClient->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
						++aPos[1];
					if(m_pClient->m_Snap.m_apInfoByScore[i]->m_ClientID == m_pClient->m_Snap.m_LocalClientID)
					{
						apPlayerInfo[1] = m_pClient->m_Snap.m_apInfoByScore[i];
						Local = 1;
						break;
					}
				}
			}
			char aScore[2][16];
			for(int t = 0; t < 2; ++t)
			{
				if(apPlayerInfo[t])
				{
					if(m_pClient->m_GameInfo.m_TimeScore && g_Config.m_ClDDRaceScoreBoard)
					{
						if(apPlayerInfo[t]->m_Score != -9999)
							str_time((int64_t)absolute(apPlayerInfo[t]->m_Score) * 100, TIME_HOURS, aScore[t], sizeof(aScore[t]));
						else
							aScore[t][0] = 0;
					}
					else
						str_from_int(apPlayerInfo[t]->m_Score, aScore[t]);
				}
				else
					aScore[t][0] = 0;
			}

			static int LocalClientID = -1;
			bool RecreateScores = str_comp(aScore[0], m_aScoreInfo[0].m_aScoreText) != 0 || str_comp(aScore[1], m_aScoreInfo[1].m_aScoreText) != 0 || LocalClientID != m_pClient->m_Snap.m_LocalClientID;
			LocalClientID = m_pClient->m_Snap.m_LocalClientID;

			bool RecreateRect = ForceScoreInfoInit;
			for(int t = 0; t < 2; t++)
			{
				if(RecreateScores)
				{
					m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(14.0f, aScore[t], -1, -1.0f);
					mem_copy(m_aScoreInfo[t].m_aScoreText, aScore[t], sizeof(m_aScoreInfo[t].m_aScoreText));
					RecreateRect = true;
				}

				if(apPlayerInfo[t])
				{
					int ID = apPlayerInfo[t]->m_ClientID;
					if(ID >= 0 && ID < MAX_CLIENTS)
					{
						const char *pName = m_pClient->m_aClients[ID].m_aName;
						if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0)
							RecreateRect = true;
					}
				}
				else
				{
					if(m_aScoreInfo[t].m_aPlayerNameText[0] != 0)
						RecreateRect = true;
				}

				char aBuf[16];
				str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
				if(str_comp(aBuf, m_aScoreInfo[t].m_aRankText) != 0)
					RecreateRect = true;
			}

			static float s_TextWidth10 = TextRender()->TextWidth(14.0f, "10", -1, -1.0f);
			float ScoreWidthMax = maximum(maximum(m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth), s_TextWidth10);
			float Split = 3.0f, ImageSize = 16.0f, PosSize = 16.0f;

			for(int t = 0; t < 2; t++)
			{
				// draw box
				if(RecreateRect)
				{
					Graphics()->DeleteQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex);

					if(t == Local)
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.25f);
					else
						Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.25f);
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(m_Width - ScoreWidthMax - ImageSize - 2 * Split - PosSize, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split + PosSize, ScoreSingleBoxHeight, 5.0f, IGraphics::CORNER_L);
				}
				Graphics()->TextureClear();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);

				if(RecreateScores)
				{
					CTextCursor Cursor;
					TextRender()->SetCursor(&Cursor, m_Width - ScoreWidthMax + (ScoreWidthMax - m_aScoreInfo[t].m_ScoreTextWidth) - Split, StartY + t * 20 + (18.f - 14.f) / 2.f, 14.0f, TEXTFLAG_RENDER);
					Cursor.m_LineWidth = -1;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScore[t]);
				}
				// draw score
				if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
				}

				if(apPlayerInfo[t])
				{
					// draw name
					int ID = apPlayerInfo[t]->m_ClientID;
					if(ID >= 0 && ID < MAX_CLIENTS)
					{
						const char *pName = m_pClient->m_aClients[ID].m_aName;
						if(RecreateRect)
						{
							mem_copy(m_aScoreInfo[t].m_aPlayerNameText, pName, sizeof(m_aScoreInfo[t].m_aPlayerNameText));

							CTextCursor Cursor;
							float w = TextRender()->TextWidth(8.0f, pName, -1, -1.0f);
							TextRender()->SetCursor(&Cursor, minimum(m_Width - w - 1.0f, m_Width - ScoreWidthMax - ImageSize - 2 * Split - PosSize), StartY + (t + 1) * 20.0f - 2.0f, 8.0f, TEXTFLAG_RENDER);
							Cursor.m_LineWidth = -1;
							TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
						}

						if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
						{
							ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
							ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
							TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
						}

						// draw tee
						CTeeRenderInfo TeeInfo = m_pClient->m_aClients[ID].m_RenderInfo;
						TeeInfo.m_Size = ScoreSingleBoxHeight;

						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						RenderTools()->GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(m_Width - ScoreWidthMax - TeeInfo.m_Size / 2 - Split, StartY + (t * 20) + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);

						RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
					}
				}
				else
				{
					m_aScoreInfo[t].m_aPlayerNameText[0] = 0;
				}

				// draw position
				char aBuf[16];
				str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
				if(RecreateRect)
				{
					mem_copy(m_aScoreInfo[t].m_aRankText, aBuf, sizeof(m_aScoreInfo[t].m_aRankText));

					CTextCursor Cursor;
					TextRender()->SetCursor(&Cursor, m_Width - ScoreWidthMax - ImageSize - Split - PosSize, StartY + t * 20 + (18.f - 10.f) / 2.f, 10.0f, TEXTFLAG_RENDER);
					Cursor.m_LineWidth = -1;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, &Cursor, aBuf);
				}
				if(m_aScoreInfo[t].m_TextRankContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, TColor, TOutlineColor);
				}

				StartY += 8.0f;
			}
		}
	}
}

void CHud::RenderWarmupTimer()
{
	// render warmup timer
	if(m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer > 0 && !(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME))
	{
		char aBuf[256];
		float FontSize = 20.0f;
		float w = TextRender()->TextWidth(FontSize, Localize("Warmup"), -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() + -w / 2, 50, FontSize, Localize("Warmup"), -1.0f);

		int Seconds = m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer / Client()->GameTickSpeed();
		if(Seconds < 5)
			str_format(aBuf, sizeof(aBuf), "%d.%d", Seconds, (m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer * 10 / Client()->GameTickSpeed()) % 10);
		else
			str_from_int(Seconds, aBuf);
		w = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() + -w / 2, 75, FontSize, aBuf, -1.0f);
	}
}

void CHud::RenderTextInfo()
{
	if(g_Config.m_ClShowfps)
	{
		// calculate avg. fps
		m_FrameTimeAvg = m_FrameTimeAvg * 0.9f + Client()->RenderFrameTime() * 0.1f;
		char aBuf[64];
		int FrameTime = (int)(1.0f / m_FrameTimeAvg + 0.5f);
		str_from_int(FrameTime, aBuf);

		static float s_TextWidth0 = TextRender()->TextWidth(12.f, "0", -1, -1.0f);
		static float s_TextWidth00 = TextRender()->TextWidth(12.f, "00", -1, -1.0f);
		static float s_TextWidth000 = TextRender()->TextWidth(12.f, "000", -1, -1.0f);
		static float s_TextWidth0000 = TextRender()->TextWidth(12.f, "0000", -1, -1.0f);
		static float s_TextWidth00000 = TextRender()->TextWidth(12.f, "00000", -1, -1.0f);
		static const float s_aTextWidth[5] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000};

		int DigitIndex = GetDigitsIndex(FrameTime, 4);

		CTextCursor Cursor;
		TextRender()->SetCursor(&Cursor, m_Width - 10 - s_aTextWidth[DigitIndex], 5, 12, TEXTFLAG_RENDER);
		Cursor.m_LineWidth = -1;
		auto OldFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		if(m_FPSTextContainerIndex.Valid())
			TextRender()->RecreateTextContainerSoft(m_FPSTextContainerIndex, &Cursor, aBuf);
		else
			TextRender()->CreateTextContainer(m_FPSTextContainerIndex, &Cursor, "0");
		TextRender()->SetRenderFlags(OldFlags);
		if(m_FPSTextContainerIndex.Valid())
		{
			TextRender()->RenderTextContainer(m_FPSTextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor());
		}
	}
	if(g_Config.m_ClShowpred && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		char aBuf[64];
		str_from_int(Client()->GetPredictionTime(), aBuf);
		TextRender()->Text(m_Width - 10 - TextRender()->TextWidth(12, aBuf, -1, -1.0f), g_Config.m_ClShowfps ? 20 : 5, 12, aBuf, -1.0f);
	}

	if(g_Config.m_ClMiniDebug)
	{
		float FontSize = 8;
		float TextHeight = 11;
		char aBuf[64];
		float yOff = 3;

		int PlayerId = m_pClient->m_Snap.m_LocalClientID;
		if(m_pClient->m_Snap.m_SpecInfo.m_Active)
			PlayerId = m_pClient->m_Snap.m_SpecInfo.m_SpectatorID;

		if(g_Config.m_ClShowhudDDRace && m_pClient->m_Snap.m_aCharacters[PlayerId].m_HasExtendedData && m_pClient->m_Snap.m_SpecInfo.m_SpectatorID != SPEC_FREEVIEW)
			yOff += 50;
		else if(g_Config.m_ClShowhudHealthAmmo && m_pClient->m_Snap.m_SpecInfo.m_SpectatorID != SPEC_FREEVIEW)
			yOff += 27;

		vec2 Pos;
		if(m_pClient->m_Snap.m_SpecInfo.m_SpectatorID == SPEC_FREEVIEW)
			Pos = vec2(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x, GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y);
		else
			Pos = m_pClient->m_aClients[PlayerId].m_RenderPos;

		str_format(aBuf, sizeof(aBuf), "X: %.2f", Pos.x / 32.0f);
		TextRender()->Text(4, yOff, FontSize, aBuf, -1.0f);

		yOff += TextHeight;
		str_format(aBuf, sizeof(aBuf), "Y: %.2f", Pos.y / 32.0f);
		TextRender()->Text(4, yOff, FontSize, aBuf, -1.0f);
		if(m_pClient->m_Snap.m_SpecInfo.m_SpectatorID != SPEC_FREEVIEW)
		{
			yOff += TextHeight;
			str_format(aBuf, sizeof(aBuf), "Angle: %d", m_pClient->m_aClients[PlayerId].m_RenderCur.m_Angle);
			TextRender()->Text(4, yOff, FontSize, aBuf, -1.0f);

			yOff += TextHeight;
			str_format(aBuf, sizeof(aBuf), "VelY: %.2f", m_pClient->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelY / 256.0f * 50.0 / 32.0f);
			TextRender()->Text(4, yOff, FontSize, aBuf, -1.0f);

			yOff += TextHeight;

			str_format(aBuf, sizeof(aBuf), "VelX: %.2f", m_pClient->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelX / 256.0f * 50.0 / 32.0f);
			TextRender()->Text(4, yOff, FontSize, aBuf, -1.0f);
		}
	}

	//render team in freeze text and last notify
	if((g_Config.m_ClShowFrozenText > 0 || g_Config.m_ClShowFrozenHud > 0 || g_Config.m_ClNotifyWhenLast) && GameClient()->m_GameInfo.m_EntitiesDDRace)
	{
		int NumInTeam = 0;
		int NumFrozen = 0;
		int LocalTeamID = m_pClient->m_Snap.m_SpecInfo.m_Active == 1 && m_pClient->m_Snap.m_SpecInfo.m_SpectatorID != -1 ?
					  m_pClient->m_Teams.Team(m_pClient->m_Snap.m_SpecInfo.m_SpectatorID) :
					  m_pClient->m_Teams.Team(m_pClient->m_Snap.m_LocalClientID);

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_pClient->m_Snap.m_apPlayerInfos[i])
				continue;

			if(m_pClient->m_Teams.Team(i) == LocalTeamID)
			{
				NumInTeam++;
				if(m_pClient->m_aClients[i].m_FreezeEnd > 0 || m_pClient->m_aClients[i].m_DeepFrozen)
					NumFrozen++;
			}
		}

		//Notify when last
		if(g_Config.m_ClNotifyWhenLast)
		{
			if(NumInTeam > 1 && NumInTeam - NumFrozen == 1)
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "%s", g_Config.m_ClNotifyWhenLastText);
                TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClNotifyWhenLastColor)));
				TextRender()->Text(170, 4, 14, aBuf, -1.0f);
                TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
		//Show freeze text
		char aBuf[64];
		if(g_Config.m_ClShowFrozenText == 1)
			str_format(aBuf, sizeof(aBuf), "%d / %d", NumInTeam - NumFrozen, NumInTeam);
		else if(g_Config.m_ClShowFrozenText == 2)
			str_format(aBuf, sizeof(aBuf), "%d / %d", NumFrozen, NumInTeam);
		if(g_Config.m_ClShowFrozenText > 0)
			TextRender()->Text(m_Width / 2 - TextRender()->TextWidth(10, aBuf, -1, -1.0f) / 2, 12, 10, aBuf, -1.0f);

		//str_format(aBuf, sizeof(aBuf), "%d", m_pClient->m_aClients[m_pClient->m_Snap.m_LocalClientID].m_PrevPredicted.m_FreezeEnd);
		//str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_ClWhatsMyPing);
		//TextRender()->Text(0, m_Width / 2 - TextRender()->TextWidth(0, 10, aBuf, -1, -1.0f) / 2, 20, 10, aBuf, -1.0f);

		if(g_Config.m_ClShowFrozenHud > 0 && !m_pClient->m_Scoreboard.Active() && !(LocalTeamID == 0 && g_Config.m_ClFrozenHudTeamOnly))
		{
			CTeeRenderInfo FreezeInfo;
			const CSkin *pSkin = m_pClient->m_Skins.Find("x_ninja");
			FreezeInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
			FreezeInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
			FreezeInfo.m_BloodColor = pSkin->m_BloodColor;
			FreezeInfo.m_SkinMetrics = pSkin->m_Metrics;
			FreezeInfo.m_ColorBody = ColorRGBA(1, 1, 1);
			FreezeInfo.m_ColorFeet = ColorRGBA(1, 1, 1);
			FreezeInfo.m_CustomColoredSkin = false;
			

			float progressiveOffset = 0.0f;
			float TeeSize = g_Config.m_ClFrozenHudTeeSize;
			int MaxTees = (int)(8.3 * (m_Width / m_Height) * 13.0f / TeeSize);
			if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred)
				MaxTees = (int)(9.5 * (m_Width / m_Height) * 13.0f / TeeSize);
			int MaxRows = g_Config.m_ClFrozenMaxRows;
			float StartPos = m_Width / 2 + 38.0f * (m_Width / m_Height) / 1.78;

			int TotalRows = std::min(MaxRows, (NumInTeam + MaxTees - 1) / MaxTees);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.4f);
			Graphics()->DrawRectExt(StartPos - TeeSize / 2, 0.0f, TeeSize * std::min(NumInTeam, MaxTees), TeeSize + 3.0f + (TotalRows - 1) * TeeSize, 5.0f, IGraphics::CORNER_B);
			Graphics()->QuadsEnd();

			bool Overflow = NumInTeam > MaxTees * MaxRows;

			int NumDisplayed = 0;
			int NumInRow = 0;
			int CurrentRow = 0;

			for(int OverflowIndex = 0; OverflowIndex < 1 + Overflow; OverflowIndex++)
			{
				for(int i = 0; i < MAX_CLIENTS && NumDisplayed < MaxTees * MaxRows; i++)
				{
					if(!m_pClient->m_Snap.m_apPlayerInfos[i])
						continue;
					if(m_pClient->m_Teams.Team(i) == LocalTeamID)
					{
						bool Frozen = false;
						CTeeRenderInfo TeeInfo = m_pClient->m_aClients[i].m_RenderInfo;
						if(m_pClient->m_aClients[i].m_FreezeEnd > 0 || m_pClient->m_aClients[i].m_DeepFrozen)
						{
							if(!g_Config.m_ClShowFrozenHudSkins)
								TeeInfo = FreezeInfo;
							Frozen = true;
						}

						if(Overflow && Frozen && OverflowIndex == 0)
							continue;
						if(Overflow && !Frozen && OverflowIndex == 1)
							continue;

						NumDisplayed++;
						NumInRow++;
						if(NumInRow > MaxTees)
						{
							NumInRow = 1;
							progressiveOffset = 0.0f;
							CurrentRow++;
						}

						TeeInfo.m_Size = TeeSize;
						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						RenderTools()->GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(StartPos + progressiveOffset, TeeSize * (0.7f) + CurrentRow * TeeSize);
						float Alpha = 1.0f;
						CNetObj_Character CurChar = m_pClient->m_aClients[i].m_RenderCur;
						if(g_Config.m_ClShowFrozenHudSkins && Frozen)
						{
							Alpha = 0.6f;
							TeeInfo.m_ColorBody.r *= 0.4;
							TeeInfo.m_ColorBody.g *= 0.4;
							TeeInfo.m_ColorBody.b *= 0.4;
							TeeInfo.m_ColorFeet.r *= 0.4;
							TeeInfo.m_ColorFeet.g *= 0.4;
							TeeInfo.m_ColorFeet.b *= 0.4;
						}
						if(Frozen)
							RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_PAIN, vec2(1.0f, 0.0f), TeeRenderPos, Alpha);
						else
							RenderTools()->RenderTee(pIdleState, &TeeInfo, CurChar.m_Emote, vec2(1.0f, 0.0f), TeeRenderPos);
						progressiveOffset += TeeSize;
					}
				}
			}
		}
	}
}

void CHud::RenderConnectionWarning()
{
	if(Client()->ConnectionProblems())
	{
		const char *pText = Localize("Connection Problems...");
		float w = TextRender()->TextWidth(24, pText, -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() - w / 2, 50, 24, pText, -1.0f);
	}
}

void CHud::RenderTeambalanceWarning()
{
	// render prompt about team-balance
	bool Flash = time() / (time_freq() / 2) % 2 == 0;
	if(m_pClient->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_TEAMS)
	{
		int TeamDiff = m_pClient->m_Snap.m_aTeamSize[TEAM_RED] - m_pClient->m_Snap.m_aTeamSize[TEAM_BLUE];
		if(g_Config.m_ClWarningTeambalance && (TeamDiff >= 2 || TeamDiff <= -2))
		{
			const char *pText = Localize("Please balance teams!");
			if(Flash)
				TextRender()->TextColor(1, 1, 0.5f, 1);
			else
				TextRender()->TextColor(0.7f, 0.7f, 0.2f, 1.0f);
			TextRender()->Text(5, 50, 6, pText, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderVoting()
{
	bool kickvote = str_startswith(m_pClient->m_Voting.VoteDescription(), "Kick ") != 0 ? true : false;
	bool specvote = str_startswith(m_pClient->m_Voting.VoteDescription(), "Pause ") != 0 ? true : false;

	if(g_Config.m_ClVoteDontShow && (kickvote || specvote))
	{ // only show votes
		// check if the is a playervote and if he is in your team.
	}

	if((!g_Config.m_ClShowVotesAfterVoting && !m_pClient->m_Scoreboard.Active() && m_pClient->m_Voting.TakenChoice()) || !m_pClient->m_Voting.IsVoting() || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	Graphics()->DrawRect(-10, 60 - 2, 100 + 10 + 4 + 5, 46, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_ALL, 5.0f);

	TextRender()->TextColor(TextRender()->DefaultTextColor());

	CTextCursor Cursor;
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), m_pClient->m_Voting.SecondsLeft());
	float tw = TextRender()->TextWidth(6, aBuf, -1, -1.0f);
	TextRender()->SetCursor(&Cursor, 5.0f + 100.0f - tw, 60.0f, 6.0f, TEXTFLAG_RENDER);
	TextRender()->TextEx(&Cursor, aBuf, -1);

	TextRender()->SetCursor(&Cursor, 5.0f, 60.0f, 6.0f, TEXTFLAG_RENDER);
	Cursor.m_LineWidth = 100.0f - tw;
	Cursor.m_MaxLines = 3;
	TextRender()->TextEx(&Cursor, m_pClient->m_Voting.VoteDescription(), -1);

	// reason
	str_format(aBuf, sizeof(aBuf), "%s %s", Localize("Reason:"), m_pClient->m_Voting.VoteReason());
	TextRender()->SetCursor(&Cursor, 5.0f, 79.0f, 6.0f, TEXTFLAG_RENDER | TEXTFLAG_STOP_AT_END);
	Cursor.m_LineWidth = 100.0f;
	TextRender()->TextEx(&Cursor, aBuf, -1);

	CUIRect Base = {5, 88, 100, 4};
	m_pClient->m_Voting.RenderBars(Base, false);

	char aKey[64];
	m_pClient->m_Binds.GetKey("vote yes", aKey, sizeof(aKey));

	str_format(aBuf, sizeof(aBuf), "%s - %s", aKey, Localize("Vote yes"));
	Base.y += Base.h;
	Base.h = 12.0f;
	if(m_pClient->m_Voting.TakenChoice() == 1)
		TextRender()->TextColor(ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f));
	UI()->DoLabel(&Base, aBuf, 6.0f, TEXTALIGN_ML);

	TextRender()->TextColor(TextRender()->DefaultTextColor());

	m_pClient->m_Binds.GetKey("vote no", aKey, sizeof(aKey));
	str_format(aBuf, sizeof(aBuf), "%s - %s", Localize("Vote no"), aKey);
	if(m_pClient->m_Voting.TakenChoice() == -1)
		TextRender()->TextColor(ColorRGBA(0.9f, 0.2f, 0.2f, 0.85f));
	UI()->DoLabel(&Base, aBuf, 6.0f, TEXTALIGN_MR);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CHud::RenderCursor()
{
	if((!m_pClient->m_Snap.m_pLocalCharacter && !(g_Config.m_ClRenderCursorSpec && m_pClient->m_Snap.m_SpecInfo.m_SpectatorID == SPEC_FREEVIEW)) || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	int CurWeapon = 1;
	RenderTools()->MapScreenToInterface(m_pClient->m_Camera.m_Center.x, m_pClient->m_Camera.m_Center.y);

	// render cursor
	if(m_pClient->m_Snap.m_SpecInfo.m_SpectatorID != SPEC_FREEVIEW)
		CurWeapon = m_pClient->m_Snap.m_pLocalCharacter->m_Weapon % NUM_WEAPONS;
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->TextureSet(m_pClient->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], m_pClient->m_Controls.m_aTargetPos[g_Config.m_ClDummy].x, m_pClient->m_Controls.m_aTargetPos[g_Config.m_ClDummy].y);
}

void CHud::PrepareAmmoHealthAndArmorQuads()
{
	float x = 5;
	float y = 5;
	IGraphics::CQuadItem Array[10];

	// ammo of the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		// 0.6
		for(int n = 0; n < 10; n++)
			Array[n] = IGraphics::CQuadItem(x + n * 12, y, 10, 10);

		m_aAmmoOffset[i] = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

		// 0.7
		if(i == WEAPON_GRENADE)
		{
			// special case for 0.7 grenade
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(1 + x + n * 12, y, 10, 10);
		}
		else
		{
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(x + n * 12, y, 12, 12);
		}

		Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
	}

	// health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_HealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_EmptyHealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_ArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_EmptyArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
}

void CHud::RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter)
{
	if(!pCharacter)
		return;

	bool IsSixupGameSkin = m_pClient->m_GameSkin.IsSixup();
	int QuadOffsetSixup = (IsSixupGameSkin ? 10 : 0);

	if(GameClient()->m_GameInfo.m_HudAmmo)
	{
		// ammo display
		float AmmoOffsetY = GameClient()->m_GameInfo.m_HudHealthArmor ? 24 : 0;
		int CurWeapon = pCharacter->m_Weapon % NUM_WEAPONS;
		if(m_pClient->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon].IsValid())
		{
			Graphics()->TextureSet(m_pClient->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon]);
			if(AmmoOffsetY > 0)
			{
				Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, minimum(pCharacter->m_AmmoCount, 10), 0, AmmoOffsetY);
			}
			else
			{
				Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, minimum(pCharacter->m_AmmoCount, 10));
			}
		}
	}

	if(GameClient()->m_GameInfo.m_HudHealthArmor)
	{
		// health display
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteHealthFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_HealthOffset + QuadOffsetSixup, minimum(pCharacter->m_Health, 10));
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteHealthEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_EmptyHealthOffset + QuadOffsetSixup + minimum(pCharacter->m_Health, 10), 10 - minimum(pCharacter->m_Health, 10));

		// armor display
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteArmorFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup, minimum(pCharacter->m_Armor, 10));
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteArmorEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup + minimum(pCharacter->m_Armor, 10), 10 - minimum(pCharacter->m_Armor, 10));
	}
}

void CHud::PreparePlayerStateQuads()
{
	float x = 5;
	float y = 5 + 24;
	IGraphics::CQuadItem Array[10];

	// Quads for displaying the available and used jumps
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpEmptyOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// Quads for displaying weapons
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
	{
		const CDataWeaponspec &WeaponSpec = g_pData->m_Weapons.m_aId[Weapon];
		float ScaleX, ScaleY;
		RenderTools()->GetSpriteScale(WeaponSpec.m_pSpriteBody, ScaleX, ScaleY);
		constexpr float HudWeaponScale = 0.25f;
		float Width = WeaponSpec.m_VisualSize * ScaleX * HudWeaponScale;
		float Height = WeaponSpec.m_VisualSize * ScaleY * HudWeaponScale;
		m_aWeaponOffset[Weapon] = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, Width, Height);
	}

	// Quads for displaying capabilities
	m_EndlessJumpOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_EndlessHookOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_JetpackOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGrenadeOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGunOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportLaserOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying prohibited capabilities
	m_SoloOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_CollisionDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HookHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HammerHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GunHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_ShotgunHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GrenadeHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LaserHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying freeze status
	m_DeepFrozenOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LiveFrozenOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying dummy actions
	m_DummyHammerOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_DummyCopyOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quad for displaying practice mode
	m_PracticeModeOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
}

void CHud::RenderPlayerState(const int ClientID)
{
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);

	// pCharacter contains the predicted character for local players or the last snap for players who are spectated
	CCharacterCore *pCharacter = &m_pClient->m_aClients[ClientID].m_Predicted;
	CNetObj_Character *pPlayer = &m_pClient->m_aClients[ClientID].m_RenderCur;
	int TotalJumpsToDisplay = 0;
	if(g_Config.m_ClShowhudJumpsIndicator)
	{
		int AvailableJumpsToDisplay;
		if(m_pClient->m_Snap.m_aCharacters[ClientID].m_HasExtendedDisplayInfo)
		{
			bool Grounded = false;
			if(Collision()->CheckPoint(pPlayer->m_X + CCharacterCore::PhysicalSize() / 2,
				   pPlayer->m_Y + CCharacterCore::PhysicalSize() / 2 + 5))
			{
				Grounded = true;
			}
			if(Collision()->CheckPoint(pPlayer->m_X - CCharacterCore::PhysicalSize() / 2,
				   pPlayer->m_Y + CCharacterCore::PhysicalSize() / 2 + 5))
			{
				Grounded = true;
			}

			int UsedJumps = pCharacter->m_JumpedTotal;
			if(pCharacter->m_Jumps > 1)
			{
				UsedJumps += !Grounded;
			}
			else if(pCharacter->m_Jumps == 1)
			{
				// If the player has only one jump, each jump is the last one
				UsedJumps = pPlayer->m_Jumped & 2;
			}
			else if(pCharacter->m_Jumps == -1)
			{
				// The player has only one ground jump
				UsedJumps = !Grounded;
			}

			if(pCharacter->m_EndlessJump && UsedJumps >= absolute(pCharacter->m_Jumps))
			{
				UsedJumps = absolute(pCharacter->m_Jumps) - 1;
			}

			int UnusedJumps = absolute(pCharacter->m_Jumps) - UsedJumps;
			if(!(pPlayer->m_Jumped & 2) && UnusedJumps <= 0)
			{
				// In some edge cases when the player just got another number of jumps, UnusedJumps is not correct
				UnusedJumps = 1;
			}
			TotalJumpsToDisplay = maximum(minimum(absolute(pCharacter->m_Jumps), 10), 0);
			AvailableJumpsToDisplay = maximum(minimum(UnusedJumps, TotalJumpsToDisplay), 0);
		}
		else
		{
			TotalJumpsToDisplay = AvailableJumpsToDisplay = absolute(m_pClient->m_Snap.m_aCharacters[ClientID].m_ExtendedData.m_Jumps);
		}

		// render available and used jumps
		int JumpsOffsetY = ((GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
				    (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));
		if(JumpsOffsetY > 0)
		{
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay, 0, JumpsOffsetY);
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay, 0, JumpsOffsetY);
		}
		else
		{
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay);
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay);
		}
	}

	float x = 5 + 12;
	float y = (5 + 12 + (GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
		   (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));

	// render weapons
	{
		constexpr float aWeaponWidth[NUM_WEAPONS] = {16, 12, 12, 12, 12, 12};
		constexpr float aWeaponInitialOffset[NUM_WEAPONS] = {-3, -4, -1, -1, -2, -4};
		bool InitialOffsetAdded = false;
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		{
			if(!pCharacter->m_aWeapons[Weapon].m_Got)
				continue;
			if(!InitialOffsetAdded)
			{
				x += aWeaponInitialOffset[Weapon];
				InitialOffsetAdded = true;
			}
			if(pPlayer->m_Weapon != Weapon)
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
			Graphics()->QuadsSetRotation(pi * 7 / 4);
			Graphics()->TextureSet(m_pClient->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
			Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], x, y);
			Graphics()->QuadsSetRotation(0);
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			x += aWeaponWidth[Weapon];
		}
		if(pCharacter->m_aWeapons[WEAPON_NINJA].m_Got)
		{
			const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
			float NinjaProgress = clamp(pCharacter->m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000 - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
			if(NinjaProgress > 0.0f && m_pClient->m_Snap.m_aCharacters[ClientID].m_HasExtendedDisplayInfo)
			{
				RenderNinjaBarPos(x, y - 12, 6.f, 24.f, NinjaProgress);
			}
		}
	}

	// render capabilities
	x = 5;
	y += 12;
	if(TotalJumpsToDisplay > 0)
	{
		y += 12;
	}
	bool HasCapabilities = false;
	if(pCharacter->m_EndlessJump)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudEndlessJump);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessJumpOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_EndlessHook)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudEndlessHook);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessHookOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_Jetpack)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudJetpack);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_JetpackOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudTeleportGun);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGunOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGrenade && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudTeleportGrenade);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGrenadeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunLaser && pCharacter->m_aWeapons[WEAPON_LASER].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudTeleportLaser);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportLaserOffset, x, y);
	}

	// render prohibited capabilities
	x = 5;
	if(HasCapabilities)
	{
		y += 12;
	}
	bool HasProhibitedCapabilities = false;
	if(pCharacter->m_Solo)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudSolo);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_SoloOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_CollisionDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudCollisionDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_CollisionDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HookHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudHookHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HookHitDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HammerHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudHammerHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HammerHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudGunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_ShotgunHitDisabled && pCharacter->m_aWeapons[WEAPON_SHOTGUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudShotgunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_ShotgunHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudGrenadeHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_GrenadeHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_LaserHitDisabled && pCharacter->m_aWeapons[WEAPON_LASER].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudLaserHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
	}

	// render dummy actions and freeze state
	x = 5;
	if(HasProhibitedCapabilities)
	{
		y += 12;
	}
	if(m_pClient->m_Snap.m_aCharacters[ClientID].m_HasExtendedDisplayInfo && m_pClient->m_Snap.m_aCharacters[ClientID].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudPracticeMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_PracticeModeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_DeepFrozen)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudDeepFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DeepFrozenOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_LiveFrozen)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudLiveFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LiveFrozenOffset, x, y);
	}
}

void CHud::RenderNinjaBarPos(const float x, float y, const float width, const float height, float Progress, const float Alpha)
{
	Progress = clamp(Progress, 0.0f, 1.0f);

	// what percentage of the end pieces is used for the progress indicator and how much is the rest
	// half of the ends are used for the progress display
	const float RestPct = 0.5f;
	const float ProgPct = 0.5f;

	const float EndHeight = width; // to keep the correct scale - the width of the sprite is as long as the height
	const float BarWidth = width;
	const float WholeBarHeight = height;
	const float MiddleBarHeight = WholeBarHeight - (EndHeight * 2.0f);
	const float EndProgressHeight = EndHeight * ProgPct;
	const float EndRestHeight = EndHeight * RestPct;
	const float ProgressBarHeight = WholeBarHeight - (EndProgressHeight * 2.0f);
	const float EndProgressProportion = EndProgressHeight / ProgressBarHeight;
	const float MiddleProgressProportion = MiddleBarHeight / ProgressBarHeight;

	// beginning piece
	float BeginningPieceProgress = 1;
	if(Progress <= 1)
	{
		if(Progress <= (EndProgressProportion + MiddleProgressProportion))
		{
			BeginningPieceProgress = 0;
		}
		else
		{
			BeginningPieceProgress = (Progress - EndProgressProportion - MiddleProgressProportion) / EndProgressProportion;
		}
	}
	// empty
	Graphics()->WrapClamp();
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 1);
	IGraphics::CQuadItem QuadEmptyBeginning(x, y, BarWidth, EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress));
	Graphics()->QuadsDrawTL(&QuadEmptyBeginning, 1);
	Graphics()->QuadsEnd();
	// full
	if(BeginningPieceProgress > 0.0f)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * (1.0f - BeginningPieceProgress), 1, RestPct + ProgPct * (1.0f - BeginningPieceProgress), 0, 1, 0, 1, 1);
		IGraphics::CQuadItem QuadFullBeginning(x, y + (EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress)), BarWidth, EndProgressHeight * BeginningPieceProgress);
		Graphics()->QuadsDrawTL(&QuadFullBeginning, 1);
		Graphics()->QuadsEnd();
	}

	// middle piece
	y += EndHeight;

	float MiddlePieceProgress = 1;
	if(Progress <= EndProgressProportion + MiddleProgressProportion)
	{
		if(Progress <= EndProgressProportion)
		{
			MiddlePieceProgress = 0;
		}
		else
		{
			MiddlePieceProgress = (Progress - EndProgressProportion) / MiddleProgressProportion;
		}
	}

	const float FullMiddleBarHeight = MiddleBarHeight * MiddlePieceProgress;
	const float EmptyMiddleBarHeight = MiddleBarHeight - FullMiddleBarHeight;

	// empty ninja bar
	if(EmptyMiddleBarHeight > 0.0f)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarEmpty);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// select the middle portion of the sprite so we don't get edge bleeding
		if(EmptyMiddleBarHeight <= EndHeight)
		{
			// prevent pixel puree, select only a small slice
			// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 1);
		}
		else
		{
			// Subset: btm_r, top_r, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 0, 0, 0, 1);
		}
		IGraphics::CQuadItem QuadEmpty(x, y, BarWidth, EmptyMiddleBarHeight);
		Graphics()->QuadsDrawTL(&QuadEmpty, 1);
		Graphics()->QuadsEnd();
	}

	// full ninja bar
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarFull);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// select the middle portion of the sprite so we don't get edge bleeding
	if(FullMiddleBarHeight <= EndHeight)
	{
		// prevent pixel puree, select only a small slice
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(1.0f - (FullMiddleBarHeight / EndHeight), 1, 1.0f - (FullMiddleBarHeight / EndHeight), 0, 1, 0, 1, 1);
	}
	else
	{
		// Subset: btm_l, top_l, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, 1, 0, 1, 1);
	}
	IGraphics::CQuadItem QuadFull(x, y + EmptyMiddleBarHeight, BarWidth, FullMiddleBarHeight);
	Graphics()->QuadsDrawTL(&QuadFull, 1);
	Graphics()->QuadsEnd();

	// ending piece
	y += MiddleBarHeight;
	float EndingPieceProgress = 1;
	if(Progress <= EndProgressProportion)
	{
		EndingPieceProgress = Progress / EndProgressProportion;
	}
	// empty
	if(EndingPieceProgress < 1.0f)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_l, top_l, top_m, btm_m | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, ProgPct - ProgPct * EndingPieceProgress, 0, ProgPct - ProgPct * EndingPieceProgress, 1);
		IGraphics::CQuadItem QuadEmptyEnding(x, y, BarWidth, EndProgressHeight * (1.0f - EndingPieceProgress));
		Graphics()->QuadsDrawTL(&QuadEmptyEnding, 1);
		Graphics()->QuadsEnd();
	}
	// full
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_m, top_m, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * EndingPieceProgress, 1, RestPct + ProgPct * EndingPieceProgress, 0, 0, 0, 0, 1);
	IGraphics::CQuadItem QuadFullEnding(x, y + (EndProgressHeight * (1.0f - EndingPieceProgress)), BarWidth, EndRestHeight + EndProgressHeight * EndingPieceProgress);
	Graphics()->QuadsDrawTL(&QuadFullEnding, 1);
	Graphics()->QuadsEnd();

	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->WrapNormal();
}

void CHud::RenderDummyActions()
{
	if(!g_Config.m_ClShowhudDummyActions || (m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) || !Client()->DummyConnected())
	{
		return;
	}
	// render small dummy actions hud
	const float BoxHeight = 29.0f;
	const float BoxWidth = 16.0f;

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		StartY -= 4;
	}
	StartY -= GetMovementInformationBoxHeight();

	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

	float y = StartY + 2;
	float x = StartX + 2;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyHammer)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudDummyHammer);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyHammerOffset, x, y);
	y += 13;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyCopyMoves)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudDummyCopy);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyCopyOffset, x, y);
}

inline int CHud::GetDigitsIndex(int Value, int Max)
{
	if(Value < 0)
	{
		Value *= -1;
	}
	int DigitsIndex = std::log10((Value ? Value : 1));
	if(DigitsIndex > Max)
	{
		DigitsIndex = Max;
	}
	if(DigitsIndex < 0)
	{
		DigitsIndex = 0;
	}
	return DigitsIndex;
}

inline float CHud::GetMovementInformationBoxHeight()
{
	float BoxHeight = 3 * MOVEMENT_INFORMATION_LINE_HEIGHT * (g_Config.m_ClShowhudPlayerPosition + g_Config.m_ClShowhudPlayerSpeed) + 2 * MOVEMENT_INFORMATION_LINE_HEIGHT * g_Config.m_ClShowhudPlayerAngle;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		BoxHeight += 2;
	}
	return BoxHeight;
}

void CHud::RenderMovementInformation(const int ClientID)
{
	// Draw the infomations depending on settings: Position, speed and target angle
	// This display is only to present the available information from the last snapshot, not to interpolate or predict
	if(!g_Config.m_ClShowhudPlayerPosition && !g_Config.m_ClShowhudPlayerSpeed && !g_Config.m_ClShowhudPlayerAngle)
	{
		return;
	}
	const float LineSpacer = 1.0f; // above and below each entry
	const float Fontsize = 6.0f;

	float BoxHeight = GetMovementInformationBoxHeight();
	const float BoxWidth = 62.0f;

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

	const CNetObj_Character *pPrevChar = &m_pClient->m_Snap.m_aCharacters[ClientID].m_Prev;
	const CNetObj_Character *pCurChar = &m_pClient->m_Snap.m_aCharacters[ClientID].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);

	// To make the player position relative to blocks we need to divide by the block size
	const vec2 Pos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick) / 32.0f;

	const vec2 Vel = mix(vec2(pPrevChar->m_VelX, pPrevChar->m_VelY), vec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

	float VelspeedX = Vel.x / 256.0f * Client()->GameTickSpeed();
	if(Vel.x >= -1 && Vel.x <= 1)
	{
		VelspeedX = 0;
	}
	float VelspeedY = Vel.y / 256.0f * Client()->GameTickSpeed();
	if(Vel.y >= -128 && Vel.y <= 128)
	{
		VelspeedY = 0;
	}
	// We show the speed in Blocks per Second (Bps) and therefore have to divide by the block size
	float DisplaySpeedX = VelspeedX / 32;
	float VelspeedLength = length(vec2(Vel.x, Vel.y) / 256.0f) * Client()->GameTickSpeed();
	// Todo: Use Velramp tuning of each individual player
	// Since these tuning parameters are almost never changed, the default values are sufficient in most cases
	float Ramp = VelocityRamp(VelspeedLength, m_pClient->m_aTuning[g_Config.m_ClDummy].m_VelrampStart, m_pClient->m_aTuning[g_Config.m_ClDummy].m_VelrampRange, m_pClient->m_aTuning[g_Config.m_ClDummy].m_VelrampCurvature);
	DisplaySpeedX *= Ramp;
	float DisplaySpeedY = VelspeedY / 32;

	float Angle = m_pClient->m_Players.GetPlayerTargetAngle(pPrevChar, pCurChar, ClientID, IntraTick);
	if(Angle < 0)
	{
		Angle += 2.0f * pi;
	}
	float DisplayAngle = Angle * 180.0f / pi;

	char aBuf[128];
	float w;

	float y = StartY + LineSpacer * 2;
	float xl = StartX + 2;
	float xr = m_Width - 2;
	int DigitsIndex;

	static float s_TextWidth0 = TextRender()->TextWidth(Fontsize, "0.00", -1, -1.0f);
	static float s_TextWidth00 = TextRender()->TextWidth(Fontsize, "00.00", -1, -1.0f);
	static float s_TextWidth000 = TextRender()->TextWidth(Fontsize, "000.00", -1, -1.0f);
	static float s_TextWidth0000 = TextRender()->TextWidth(Fontsize, "0000.00", -1, -1.0f);
	static float s_TextWidth00000 = TextRender()->TextWidth(Fontsize, "00000.00", -1, -1.0f);
	static float s_TextWidth000000 = TextRender()->TextWidth(Fontsize, "000000.00", -1, -1.0f);
	static float s_aTextWidth[6] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000, s_TextWidth000000};
	static float s_TextWidthMinus0 = TextRender()->TextWidth(Fontsize, "-0.00", -1, -1.0f);
	static float s_TextWidthMinus00 = TextRender()->TextWidth(Fontsize, "-00.00", -1, -1.0f);
	static float s_TextWidthMinus000 = TextRender()->TextWidth(Fontsize, "-000.00", -1, -1.0f);
	static float s_TextWidthMinus0000 = TextRender()->TextWidth(Fontsize, "-0000.00", -1, -1.0f);
	static float s_TextWidthMinus00000 = TextRender()->TextWidth(Fontsize, "-00000.00", -1, -1.0f);
	static float s_TextWidthMinus000000 = TextRender()->TextWidth(Fontsize, "-000000.00", -1, -1.0f);
	static float s_aTextWidthMinus[6] = {s_TextWidthMinus0, s_TextWidthMinus00, s_TextWidthMinus000, s_TextWidthMinus0000, s_TextWidthMinus00000, s_TextWidthMinus000000};

	if(g_Config.m_ClShowhudPlayerPosition)
	{
		TextRender()->Text(xl, y, Fontsize, Localize("Position:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(xl, y, Fontsize, "X:", -1.0f);
		str_format(aBuf, sizeof(aBuf), "%.2f", Pos.x);
		DigitsIndex = GetDigitsIndex(Pos.x, 5);
		w = (Pos.x < 0) ? s_aTextWidthMinus[DigitsIndex] : s_aTextWidth[DigitsIndex];
		TextRender()->Text(xr - w, y, Fontsize, aBuf, -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(xl, y, Fontsize, "Y:", -1.0f);
		str_format(aBuf, sizeof(aBuf), "%.2f", Pos.y);
		DigitsIndex = GetDigitsIndex(Pos.y, 5);
		w = (Pos.y < 0) ? s_aTextWidthMinus[DigitsIndex] : s_aTextWidth[DigitsIndex];
		TextRender()->Text(xr - w, y, Fontsize, aBuf, -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;
	}

	if(g_Config.m_ClShowhudPlayerSpeed)
	{
		TextRender()->Text(xl, y, Fontsize, Localize("Speed:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(xl, y, Fontsize, "X:", -1.0f);
		str_format(aBuf, sizeof(aBuf), "%.2f", DisplaySpeedX);
		DigitsIndex = GetDigitsIndex(DisplaySpeedX, 5);
		w = (DisplaySpeedX < 0) ? s_aTextWidthMinus[DigitsIndex] : s_aTextWidth[DigitsIndex];
		TextRender()->Text(xr - w, y, Fontsize, aBuf, -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(xl, y, Fontsize, "Y:", -1.0f);
		str_format(aBuf, sizeof(aBuf), "%.2f", DisplaySpeedY);
		DigitsIndex = GetDigitsIndex(DisplaySpeedY, 5);
		w = (DisplaySpeedY < 0) ? s_aTextWidthMinus[DigitsIndex] : s_aTextWidth[DigitsIndex];
		TextRender()->Text(xr - w, y, Fontsize, aBuf, -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;
	}

	if(g_Config.m_ClShowhudPlayerAngle)
	{
		TextRender()->Text(xl, y, Fontsize, Localize("Angle:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;
		str_format(aBuf, sizeof(aBuf), "%.2f", DisplayAngle);
		DigitsIndex = GetDigitsIndex(DisplayAngle, 5);
		w = (DisplayAngle < 0) ? s_aTextWidthMinus[DigitsIndex] : s_aTextWidth[DigitsIndex];
		TextRender()->Text(xr - w, y, Fontsize, aBuf, -1.0f);
	}
}

void CHud::RenderSpectatorHud()
{
	// draw the box
	Graphics()->DrawRect(m_Width - 180.0f, m_Height - 15.0f, 180.0f, 15.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_TL, 5.0f);

	// draw the text
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Spectate"), GameClient()->m_MultiViewActivated ? Localize("Multi-View") : m_pClient->m_Snap.m_SpecInfo.m_SpectatorID != SPEC_FREEVIEW ? m_pClient->m_aClients[m_pClient->m_Snap.m_SpecInfo.m_SpectatorID].m_aName : Localize("Free-View"));
	TextRender()->Text(m_Width - 174.0f, m_Height - 15.0f + (15.f - 8.f) / 2.f, 8.0f, aBuf, -1.0f);
}

void CHud::RenderLocalTime(float x)
{
	if(!g_Config.m_ClShowLocalTimeAlways && !m_pClient->m_Scoreboard.Active())
		return;

	// draw the box
	Graphics()->DrawRect(x - 30.0f, 0.0f, 25.0f, 12.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);

	// draw the text
	char aTimeStr[6];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), "%H:%M");
	TextRender()->Text(x - 25.0f, (12.5f - 5.f) / 2.f, 5.0f, aTimeStr, -1.0f);
}

void CHud::OnRender()
{
	if(!m_pClient->m_Snap.m_pGameInfoObj)
		return;

	m_Width = 300.0f * Graphics()->ScreenAspect();
	m_Height = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);

#if defined(CONF_VIDEORECORDER)
	if((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud))
#else
	if(g_Config.m_ClShowhud)
#endif
	{
		if(m_pClient->m_Snap.m_pLocalCharacter && !m_pClient->m_Snap.m_SpecInfo.m_Active && !(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			if(g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(m_pClient->m_Snap.m_pLocalCharacter);
			}
			if(m_pClient->m_Snap.m_aCharacters[m_pClient->m_Snap.m_LocalClientID].m_HasExtendedData && g_Config.m_ClShowhudDDRace && GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(m_pClient->m_Snap.m_LocalClientID);
			}
			RenderMovementInformation(m_pClient->m_Snap.m_LocalClientID);
			RenderDDRaceEffects();
		}
		else if(m_pClient->m_Snap.m_SpecInfo.m_Active)
		{
			int SpectatorID = m_pClient->m_Snap.m_SpecInfo.m_SpectatorID;
			if(SpectatorID != SPEC_FREEVIEW && g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(&m_pClient->m_Snap.m_aCharacters[SpectatorID].m_Cur);
			}
			if(SpectatorID != SPEC_FREEVIEW &&
				m_pClient->m_Snap.m_aCharacters[SpectatorID].m_HasExtendedData &&
				g_Config.m_ClShowhudDDRace &&
				(!GameClient()->m_MultiViewActivated || GameClient()->m_MultiViewShowHud) &&
				GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(SpectatorID);
			}
			if(SpectatorID != SPEC_FREEVIEW)
			{
				RenderMovementInformation(SpectatorID);
			}
			RenderSpectatorHud();
		}

		if(g_Config.m_ClShowhudTimer)
			RenderGameTimer();
		RenderPauseNotification();
		RenderSuddenDeath();
		if(g_Config.m_ClShowhudScore)
			RenderScoreHud();
		RenderDummyActions();
		RenderWarmupTimer();
		RenderTextInfo();
		if(g_Config.m_ClShowCenterLines)
			RenderCenterLines();
		RenderLocalTime((m_Width / 7) * 3);
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
			RenderConnectionWarning();
		RenderTeambalanceWarning();
		RenderVoting();
		if(g_Config.m_ClShowRecord)
			RenderRecord();
	}
	RenderCursor();
}

void CHud::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_DDRACETIME || MsgType == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		CNetMsg_Sv_DDRaceTime *pMsg = (CNetMsg_Sv_DDRaceTime *)pRawMsg;

		m_DDRaceTime = pMsg->m_Time;

		m_ShowFinishTime = pMsg->m_Finish != 0;

		if(!m_ShowFinishTime)
		{
			m_TimeCpDiff = (float)pMsg->m_Check / 100;
			m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
		else
		{
			m_FinishTimeDiff = (float)pMsg->m_Check / 100;
			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RECORD || MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_Record *pMsg = (CNetMsg_Sv_Record *)pRawMsg;

		// NETMSGTYPE_SV_RACETIME on old race servers
		if(MsgType == NETMSGTYPE_SV_RECORDLEGACY && m_pClient->m_GameInfo.m_DDRaceRecordMessage)
		{
			m_DDRaceTime = pMsg->m_ServerTimeBest; // First value: m_Time

			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);

			if(pMsg->m_PlayerTimeBest) // Second value: m_Check
			{
				m_TimeCpDiff = (float)pMsg->m_PlayerTimeBest / 100;
				m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
			}
		}
		else if(MsgType == NETMSGTYPE_SV_RECORD || m_pClient->m_GameInfo.m_RaceRecordMessage)
		{
			m_ServerRecord = (float)pMsg->m_ServerTimeBest / 100;
			m_aPlayerRecord[g_Config.m_ClDummy] = (float)pMsg->m_PlayerTimeBest / 100;
		}
	}
}

void CHud::RenderDDRaceEffects()
{
	if(m_DDRaceTime)
	{
		char aBuf[64];
		char aTime[32];
		if(m_ShowFinishTime && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			str_time(m_DDRaceTime, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "Finish time: %s", aTime);

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			TextRender()->TextColor(1, 1, 1, Alpha);
			CTextCursor Cursor;
			TextRender()->SetCursor(&Cursor, 150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(12, aBuf, -1, -1.0f) / 2, 20, 12, TEXTFLAG_RENDER);
			Cursor.m_LineWidth = -1.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);
			if(m_FinishTimeDiff != 0.0f && m_DDRaceEffectsTextContainerIndex.Valid())
			{
				if(m_FinishTimeDiff < 0)
				{
					str_time_float(-m_FinishTimeDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "-%s", aTime);
					TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
				}
				else
				{
					str_time_float(m_FinishTimeDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "+%s", aTime);
					TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
				}
				TextRender()->SetCursor(&Cursor, 150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf, -1, -1.0f) / 2, 34, 10, TEXTFLAG_RENDER);
				Cursor.m_LineWidth = -1.0f;
				TextRender()->AppendTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);
			}
			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(!m_ShowFinishTime && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			if(m_TimeCpDiff < 0)
			{
				str_time_float(-m_TimeCpDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "-%s", aTime);
			}
			else
			{
				str_time_float(m_TimeCpDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "+%s", aTime);
			}

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			if(m_TimeCpDiff > 0)
				TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
			else if(m_TimeCpDiff < 0)
				TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
			else if(!m_TimeCpDiff)
				TextRender()->TextColor(1, 1, 1, Alpha); // white

			CTextCursor Cursor;
			TextRender()->SetCursor(&Cursor, 150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf, -1, -1.0f) / 2, 20, 10, TEXTFLAG_RENDER);
			Cursor.m_LineWidth = -1.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);

			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderRecord()
{
	if(m_ServerRecord > 0.0f)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Server best:"));
		TextRender()->Text(5, 75, 6, aBuf, -1.0f);
		char aTime[32];
		str_time_float(m_ServerRecord, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", m_ServerRecord > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 75, 6, aBuf, -1.0f);
	}

	const float PlayerRecord = m_aPlayerRecord[g_Config.m_ClDummy];
	if(PlayerRecord > 0.0f)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Personal best:"));
		TextRender()->Text(5, 82, 6, aBuf, -1.0f);
		char aTime[32];
		str_time_float(PlayerRecord, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", PlayerRecord > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 82, 6, aBuf, -1.0f);
	}
}
