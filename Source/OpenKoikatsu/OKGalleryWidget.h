#pragma once
#include "OKCharacterController.h"
#include "OKGalleryWidget.generated.h"

// Card-browser/editor presentation. All character and interaction services are shared.
UCLASS()
class OPENKOIKATSU_API UOKGalleryWidget : public UOKCharacterController {
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
};
