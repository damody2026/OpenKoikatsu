#include "OKCardRowButton.h"
#include "OKGalleryWidget.h"

void UOKCardRowButton::HandleClick()
{
	if (Owner)
	{
		Owner->SelectIndex(Index);
	}
}
