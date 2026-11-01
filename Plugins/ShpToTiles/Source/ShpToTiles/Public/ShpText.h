// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "ShpTypes.h"

namespace ShpText
{
	/** True when the byte range is a valid UTF-8 sequence (pure ASCII counts as valid). */
	bool LooksLikeUtf8(const uint8* Data, int32 Len);

	/** Decode a byte range as UTF-8 (bUtf8) or GBK/CP936. */
	FString Decode(const uint8* Data, int32 Len, bool bUtf8);

	/** Decode honouring the requested mode; Auto sniffs UTF-8 first, then falls back to GBK. */
	FString DecodeAuto(const uint8* Data, int32 Len, EShpEncoding Mode);

	/** Trim trailing spaces / NULs / control chars that dBASE uses for padding. */
	FString TrimDbText(const FString& In);
}
