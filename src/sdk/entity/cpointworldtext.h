#pragma once

#include "cbasemodelentity.h"

// point_worldtext: text rendered in the world. Used by the "3D HUD" (!hud3d) which parks a couple of these in front of the
// player's eyes every tick so they behave like a freely placed HUD element.
class CPointWorldText : public CBaseModelEntity
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CPointWorldText);

	SCHEMA_FIELD_POINTER(char, m_messageText) // char[512]
	SCHEMA_FIELD_POINTER(char, m_FontName)    // char[64]
	SCHEMA_FIELD(bool, m_bEnabled)
	SCHEMA_FIELD(bool, m_bFullbright)
	SCHEMA_FIELD(float, m_flWorldUnitsPerPx)
	SCHEMA_FIELD(float, m_flFontSize)
	SCHEMA_FIELD(float, m_flDepthOffset)
	SCHEMA_FIELD(Color, m_Color)
	SCHEMA_FIELD(int, m_nJustifyHorizontal) // 0 left, 1 center, 2 right
	SCHEMA_FIELD(int, m_nJustifyVertical)   // 0 top, 1 center, 2 bottom
	SCHEMA_FIELD(int, m_nReorientMode)      // 0 none, 1 around up
	SCHEMA_FIELD(bool, m_bDrawBackground)
	SCHEMA_FIELD(float, m_flBackgroundBorderWidth)
	SCHEMA_FIELD(float, m_flBackgroundBorderHeight)
	SCHEMA_FIELD(float, m_flBackgroundWorldToUV)

	void SetMessage(const char *text)
	{
		V_strncpy(this->m_messageText(), text, 512);
		this->m_messageText.NetworkStateChanged();
	}

	void SetColor(const Color &color)
	{
		this->m_Color(color);
	}
};
