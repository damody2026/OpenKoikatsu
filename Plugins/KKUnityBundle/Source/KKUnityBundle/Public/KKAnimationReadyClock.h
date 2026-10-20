#pragma once
#include "CoreMinimal.h"

// A newly selected clip already publishes pose zero. The first subsequent
// world delta can include synchronous loading that happened before selection.
// Rebase that one world tick; explicit sampling and later frame deltas retain
// their supplied time, including genuine slow frames during playback.
struct FKKAnimationReadyClock
{
    bool Pending=false;
    uint64 ReadyFrame=0,Rebases=0;
    double DiscardedSeconds=0.;
    void Ready(uint64 Frame){ReadyFrame=Frame;Pending=true;}
    void Stop(){Pending=false;}
    float Delta(float Seconds,uint64 Frame,bool AutomaticWorldTick)
    {
        if(Pending&&AutomaticWorldTick&&Frame!=ReadyFrame){Pending=false;++Rebases;DiscardedSeconds+=FMath::Max(0.f,Seconds);return 0.f;}
        return Seconds;
    }
};
