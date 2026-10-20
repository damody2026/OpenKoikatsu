#pragma once

#include "CoreMinimal.h"

/** One body or face shape parameter of a card (FKKBody::ShapeValueBody / FKKFace::ShapeValueFace), in display order. */
struct FKKShapeRow
{
	const TCHAR* Label;
	int32 Index;
	bool bBody;
	/** A visual group ends after this row. */
	bool bSep;
};

/** The shape parameters the game's character maker shows, with their labels (game gallery and OK card editor). */
KKCARD_API TConstArrayView<FKKShapeRow> KKShapeRows();
