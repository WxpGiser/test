// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpText.h"

#include "Containers/StringConv.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace ShpText
{
	bool LooksLikeUtf8(const uint8* Data, int32 Len)
	{
		int32 i = 0;
		while (i < Len)
		{
			const uint8 C = Data[i];
			if (C < 0x80)
			{
				++i;
				continue;
			}

			int32 Extra = 0;
			if ((C & 0xE0) == 0xC0)
			{
				Extra = 1;
				if (C < 0xC2)
				{
					return false; // overlong
				}
			}
			else if ((C & 0xF0) == 0xE0)
			{
				Extra = 2;
			}
			else if ((C & 0xF8) == 0xF0)
			{
				Extra = 3;
			}
			else
			{
				return false;
			}

			if (i + Extra >= Len)
			{
				return false;
			}
			for (int32 k = 1; k <= Extra; ++k)
			{
				if ((Data[i + k] & 0xC0) != 0x80)
				{
					return false;
				}
			}
			i += Extra + 1;
		}

		// Pure ASCII is valid UTF-8, and GBK decoding of ASCII is identical, so
		// claiming UTF-8 is harmless.
		return true;
	}

	FString Decode(const uint8* Data, int32 Len, bool bUtf8)
	{
		if (Data == nullptr || Len <= 0)
		{
			return FString();
		}

		if (bUtf8)
		{
			FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data), Len);
			return FString(Converter.Length(), Converter.Get());
		}

#if PLATFORM_WINDOWS
		const int32 WideLen = ::MultiByteToWideChar(936, 0, reinterpret_cast<const char*>(Data), Len, nullptr, 0);
		if (WideLen <= 0)
		{
			return FString();
		}
		TArray<WIDECHAR> Wide;
		Wide.SetNumUninitialized(WideLen + 1);
		::MultiByteToWideChar(936, 0, reinterpret_cast<const char*>(Data), Len, Wide.GetData(), WideLen);
		Wide[WideLen] = 0;
		return FString(WideLen, Wide.GetData());
#else
		// Non-Windows fallback: treat as Latin-1 so at least the bytes survive.
		FString Out;
		Out.Reserve(Len);
		for (int32 i = 0; i < Len; ++i)
		{
			Out.AppendChar(static_cast<TCHAR>(Data[i]));
		}
		return Out;
#endif
	}

	FString DecodeAuto(const uint8* Data, int32 Len, EShpEncoding Mode)
	{
		if (Mode == EShpEncoding::Utf8)
		{
			return Decode(Data, Len, true);
		}
		if (Mode == EShpEncoding::Gbk)
		{
			return Decode(Data, Len, false);
		}
		return Decode(Data, Len, LooksLikeUtf8(Data, Len));
	}

	FString TrimDbText(const FString& In)
	{
		int32 End = In.Len();
		while (End > 0)
		{
			const TCHAR C = In[End - 1];
			if (C == TEXT(' ') || C == 0 || C == TEXT('\r') || C == TEXT('\n') || C == TEXT('\t'))
			{
				--End;
			}
			else
			{
				break;
			}
		}
		int32 Start = 0;
		while (Start < End && (In[Start] == TEXT(' ') || In[Start] == 0))
		{
			++Start;
		}
		return In.Mid(Start, End - Start);
	}
}
