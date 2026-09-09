// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

/**
 * Shared HUD palette for every GRT runtime panel.
 *
 * IMPORTANT -- these are LINEAR values, deliberately much smaller than the hex/sRGB numbers they
 * correspond to. Slate tints (BorderBackgroundColor, ColorAndOpacity, brush colours) are linear
 * multipliers, so authoring a "dark" panel with an sRGB-looking 0.11 actually renders around sRGB
 * 0.37 -- a washed-out mid grey, which is exactly how the panels looked before this existed. Each
 * constant below lists the sRGB colour it is meant to APPEAR as; the literal is that colour
 * converted to linear via ((s + 0.055) / 1.055) ^ 2.4.
 *
 * Mirrors the design tokens in the reference HUD stylesheet (--hud-panel, --hud-line, --hud-text,
 * --hud-dim, --hud-locked, --hud-accent, --hud-control...). Kept in one header so the main tool
 * panel and the file picker cannot drift apart.
 */
namespace GRTHud
{
	/** Panel surface. Appears ~#16161a. */
	inline const FLinearColor PanelBG(0.0075f, 0.0078f, 0.0095f, 0.97f);

	/** Recessed surface (list bodies, wells). Appears ~#121215. */
	inline const FLinearColor SurfaceBG(0.0050f, 0.0052f, 0.0062f, 0.98f);

	/** Hairline divider / outline. White at 10%, matching --hud-line. */
	inline const FLinearColor Line(1.0f, 1.0f, 1.0f, 0.10f);

	/** Stronger hairline for outer panel outlines. White at 18%. */
	inline const FLinearColor LineStrong(1.0f, 1.0f, 1.0f, 0.18f);

	/** Primary text. Appears ~#e8e8ea. */
	inline const FLinearColor Text(0.807f, 0.807f, 0.822f);

	/** Secondary/label text. Appears ~#9a9aa1. */
	inline const FLinearColor Dim(0.318f, 0.318f, 0.340f);

	/** Disabled/locked text. Appears ~#5f5f66. */
	inline const FLinearColor Locked(0.113f, 0.113f, 0.126f);

	/** Amber accent. Appears ~#e0a33a. */
	inline const FLinearColor Accent(0.744f, 0.366f, 0.042f);

	/** Cyan accent (mode badge, informational). Appears ~#5fc8d8. */
	inline const FLinearColor Cyan(0.115f, 0.580f, 0.686f);

	/** Control fill. Appears ~#3a3a40. */
	inline const FLinearColor Control(0.043f, 0.043f, 0.052f, 0.95f);

	/** Control fill, hovered. Appears ~#4d4d55. */
	inline const FLinearColor ControlHover(0.075f, 0.075f, 0.088f, 0.97f);

	/** Control fill, pressed. Appears ~#5a5a63. */
	inline const FLinearColor ControlPressed(0.104f, 0.104f, 0.122f, 0.97f);

	/** Control fill, disabled. Appears ~#2a2a2e. */
	inline const FLinearColor ControlDisabled(0.024f, 0.024f, 0.028f, 0.85f);

	/** Row hover wash for list entries. */
	inline const FLinearColor RowHover(1.0f, 1.0f, 1.0f, 0.055f);

	/** Row pressed/selected wash for list entries. */
	inline const FLinearColor RowPressed(1.0f, 1.0f, 1.0f, 0.09f);

	inline const FLinearColor Transparent(0.0f, 0.0f, 0.0f, 0.0f);

	/** Faint amber wash behind an active/selected control. */
	inline const FLinearColor AccentSoft(0.744f, 0.366f, 0.042f, 0.16f);

	/**
	 * Tints for SButton::ButtonColorAndOpacity, which multiplies the whole brush -- fill AND outline,
	 * colour AND alpha. Unlike ButtonStyle (a raw pointer, fixed at construction) this CAN be bound to
	 * an attribute, so it is how a button shows active state at runtime.
	 *
	 * Inactive is plain white: a neutral multiply that leaves the authored style exactly as-is. (The
	 * previous value here was FLinearColor::Gray, which quietly halved every inactive button's fill
	 * and outline and was a large part of why the panel looked flat and muddy.)
	 *
	 * Active deliberately uses components above 1. Multiplying is the only lever available, and the
	 * base fill/outline are very dark and very transparent respectively, so values <= 1 can only ever
	 * darken -- there is no way to reach an amber-on-dark active state without over-driving. The >1
	 * alpha is what lifts the hairline outline (Line, 10% alpha) to a clearly visible amber edge;
	 * Slate clamps the result, so the fill simply saturates to opaque rather than overflowing.
	 */
	inline const FLinearColor InactiveButtonTint(1.0f, 1.0f, 1.0f, 1.0f);
	inline const FLinearColor ActiveButtonTint(2.6f, 1.3f, 0.35f, 3.5f);

	/**
	 * Compact control with a hairline outline -- the reference's buttons are outlined, not bare
	 * fills, and that outline is most of what separates them from Slate's default look.
	 * FSlateRoundedBoxBrush is used purely for its outline support; the corner radius is kept small
	 * so controls stay rectangular rather than pill-shaped.
	 */
	inline void MakeButtonStyle(FButtonStyle& OutStyle)
	{
		OutStyle.SetNormal(FSlateRoundedBoxBrush(Control, 2.0f, Line, 1.0f));
		OutStyle.SetHovered(FSlateRoundedBoxBrush(ControlHover, 2.0f, LineStrong, 1.0f));
		OutStyle.SetPressed(FSlateRoundedBoxBrush(ControlPressed, 2.0f, LineStrong, 1.0f));
		OutStyle.SetDisabled(FSlateRoundedBoxBrush(ControlDisabled, 2.0f, Line, 1.0f));
		OutStyle.NormalPadding = FMargin(8.0f, 4.0f);
		OutStyle.PressedPadding = FMargin(8.0f, 4.0f);
	}

	/**
	 * Active/selected control: amber outline over a faint amber wash. Used for the currently-chosen
	 * brush, dynamic layer, road mode and building mode, matching how the reference marks state --
	 * an outline change rather than a filled highlight.
	 */
	inline void MakeAccentButtonStyle(FButtonStyle& OutStyle)
	{
		OutStyle.SetNormal(FSlateRoundedBoxBrush(AccentSoft, 2.0f, Accent, 1.0f));
		OutStyle.SetHovered(FSlateRoundedBoxBrush(AccentSoft, 2.0f, Accent, 1.0f));
		OutStyle.SetPressed(FSlateRoundedBoxBrush(AccentSoft, 2.0f, Accent, 1.0f));
		OutStyle.SetDisabled(FSlateRoundedBoxBrush(ControlDisabled, 2.0f, Line, 1.0f));
		OutStyle.NormalPadding = FMargin(8.0f, 4.0f);
		OutStyle.PressedPadding = FMargin(8.0f, 4.0f);
	}

	/** Badge button for header (outlined in Cyan). */
	inline void MakeBadgeButtonStyle(FButtonStyle& OutStyle)
	{
		OutStyle.SetNormal(FSlateRoundedBoxBrush(Transparent, 2.0f, Cyan, 1.0f));
		OutStyle.SetHovered(FSlateRoundedBoxBrush(ControlHover, 2.0f, Cyan, 1.0f));
		OutStyle.SetPressed(FSlateRoundedBoxBrush(ControlPressed, 2.0f, Cyan, 1.0f));
		OutStyle.SetDisabled(FSlateRoundedBoxBrush(Transparent, 2.0f, Locked, 1.0f));
		OutStyle.NormalPadding = FMargin(8.0f, 2.0f);
		OutStyle.PressedPadding = FMargin(8.0f, 2.0f);
	}

	/** Borderless row, visible only on hover -- for file/directory listings and similar lists. */
	inline void MakeRowStyle(FButtonStyle& OutStyle)
	{
		OutStyle.SetNormal(FSlateColorBrush(Transparent));
		OutStyle.SetHovered(FSlateColorBrush(RowHover));
		OutStyle.SetPressed(FSlateColorBrush(RowPressed));
		OutStyle.SetDisabled(FSlateColorBrush(Transparent));
		OutStyle.NormalPadding = FMargin(6.0f, 3.0f);
		OutStyle.PressedPadding = FMargin(6.0f, 3.0f);
	}
}
