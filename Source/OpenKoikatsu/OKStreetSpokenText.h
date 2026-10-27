#pragma once

#include "CoreMinimal.h"

// What the voice says of a line: a stage direction in brackets is not said. Yuuka in the library read 「（小聲）」 aloud,
// and his typed 「（我壓低聲音）」 was spoken in his voice. Short bracketed parts only (16 characters), so a longer aside
// really said stays; the line on screen keeps them.
inline FString OKStreetSpokenText(FString Spoken)
{
	for (int32 Open = 0;;)
	{
		int32 A = INDEX_NONE;
		for (int32 I = Open; I < Spoken.Len(); ++I)
		{
			if (Spoken[I] == TEXT('（') || Spoken[I] == TEXT('(')) { A = I; break; }
		}
		if (A == INDEX_NONE) break;
		const TCHAR Close = Spoken[A] == TEXT('（') ? TEXT('）') : TEXT(')');
		int32 B = INDEX_NONE;
		for (int32 I = A + 1; I < Spoken.Len() && I <= A + 17; ++I)
		{
			if (Spoken[I] == Close) { B = I; break; }
		}
		if (B == INDEX_NONE) { Open = A + 1; continue; }
		Spoken.RemoveAt(A, B - A + 1);
		Open = A;
	}
	Spoken.TrimStartAndEndInline();
	return Spoken;
}
