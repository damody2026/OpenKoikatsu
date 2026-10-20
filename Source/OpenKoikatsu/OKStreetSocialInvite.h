#pragma once
#include "CoreMinimal.h"

// Plan §12 invitations. Appeal, acceptance, refusal reasons and the approach score are game rules (Tools/ok-rules
// src/invite.rs); the kinds below are the order the rules use, which indexes her rolled stance (InviteStance).
enum class EOKStreetInvite : uint8 { Light, Meal, Visit, Leisure, Private, Sleep, Count };
