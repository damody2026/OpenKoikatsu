#include "KKSharedAnimation.h"
float UKKSharedAnimation::GetPhase(float Seconds) const {
 if(!FMath::IsFinite(Seconds)||!FMath::IsFinite(Duration)||Duration<=0.f)return 0.f;
 const float Time=FMath::Max(Seconds,0.f);
 return Loop?FMath::Fmod(Time,Duration):FMath::Min(Time,Duration);
}
static FKKSharedDriverFactory GSharedFactory;
void OKRegisterSharedAnimationDriver(FKKSharedDriverFactory Factory){GSharedFactory=MoveTemp(Factory);}
TUniquePtr<IKKSharedAnimationDriver> OKCreateSharedAnimationDriver(){return GSharedFactory?GSharedFactory():nullptr;}
