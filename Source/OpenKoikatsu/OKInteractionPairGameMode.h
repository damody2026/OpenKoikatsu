#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HttpRouteHandle.h"
#include "OKInteractionPairGameMode.generated.h"

// Loads one female card and one male card at startup and enters the shared one-to-one interaction scene (paired
// motions, voice, touch, camera) with the PhysX secondary dynamics, for checking and measuring the pair physics.
// Start: Tools/run_interaction_pair.lua (map /Engine/Maps/Templates/Template_Default?game=/Script/OpenKoikatsu.OKInteractionPairGameMode).
// Formerly OK_Demo's AOKDemoGameMode (E:/PhysX/UE5.8/Samples/OK_Demo, retired).
UCLASS()
class OPENKOIKATSU_API AOKInteractionPairGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AOKInteractionPairGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Defaults: Content/CharacterCards/Demo/C050.png and Male01.png.
	// Command line overrides: -PairFemale=<png> -PairMale=<png>
	FString FemaleCard;
	FString MaleCard;

private:
	void StartInteraction();
	void ReportCatalogue();
	void TickSelfTest();
	void TickPairAudit();
	void EnableHeadLook();
public:
	/** Both characters turn their heads to the camera (on) or play the animation as is (off). Panel checkbox, console ok.Interaction.FaceCamera 0/1, -PairFaceCamera=0. */
	void SetFaceCamera(bool bOn);
	/** Plain stage for physics viewing: uniform gray background (Gray = linear value) and an invisible floor. */
	void SetBackground(float Gray);
	void SetFloorVisible(bool bVisible);
	void OrbitCamera(float DeltaX, float DeltaY);
	void CameraMouse(uint8 Buttons, float DeltaX, float DeltaY, float WheelNotches);
private:
	FTimerHandle HeadLookTimer;
	FTimerHandle StageTimer;
	void ApplyStage();
	TWeakObjectPtr<class AExponentialHeightFog> BackgroundFog;
	float BackgroundGray = 0.5f;
	bool bFloorVisible = false;
	int32 HeadLookAttempts = 0;
	int32 SelfTestStep = 0;
	double SelfTestAt = 0.;
	// -PairAudit [-PairAuditFirst=N -PairAuditLast=M]: plays every two-person pose for 10 s and logs the PhysX clipping audit.
	int32 PairAuditIndex = 0;
	int32 PairAuditLast = 0;
	int32 PairAuditPhase = 0; // 0 select, 1 wait for playback, 2 settle, 3 measure
	TArray<int32> PairAuditKeys; // PairCombo rows to walk
	FString PairAuditFilter;
	FString PairAuditStateName; // /pose?state=<name>: measure this state
	int32 PairAuditState = 0; // -PairAuditAllStates: 1-based PairStateCombo option being measured (0 = choose)
	double PairAuditAt = 0.;
	TWeakObjectPtr<class AOKInteractionMode> ActiveMode;
	// -PairControl[=port] (default 47871; StreetSocial uses 47870; listeners bind 127.0.0.1, Config/DefaultEngine.ini):
	//   GET /exec?cmd=<console command>[;<command>...]   run console commands (CVars, OKPhysXSecondary.Shot ...)
	//   GET /reset                                        restart the clipping/jitter and frame-time measurement
	//   GET /stats                                        measurement since the last reset (same lines as the audit)
	//   GET /pose?filter=<text>[&state=<n>]               switch the paired pose (first row containing the text)
	void StartControl(int32 Port);
	FString MeasurementLines(double Since) const;
	TArray<FHttpRouteHandle> ControlRoutes;
	TSharedPtr<class IHttpRouter> ControlRouter;
	double MeasureSince = 0.;
	bool bControl = false;
	FTimerHandle StartTimer;
};
