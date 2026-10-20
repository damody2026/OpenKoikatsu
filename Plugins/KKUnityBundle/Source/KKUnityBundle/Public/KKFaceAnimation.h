#pragma once
#include "CoreMinimal.h"

// FaceBlendShape.LateUpdate + MathfEx.LerpAccel (Mathf.Lerp with sqrt(t)).
inline float OKEyeCorrectOpenMax(float OpenMax,float Horizontal,float Vertical,float Up,float Down,float Side)
{
	const auto Accel=[](float Value,float Rate) { return Value*FMath::Clamp(FMath::Sqrt(FMath::Max(0.f,Rate)),0.f,1.f); };
	float Delta=Vertical>0.f ? Accel(Up,Vertical) : -Accel(Down,-Vertical);
	Delta-=Accel(Side,FMath::Abs(Horizontal));
	Delta=FMath::Clamp(Delta,-FMath::Max(Down,Side),Up);
	Delta*=1.f-(1.f-OpenMax);
	return FMath::Min(1.f-Up,OpenMax)+Delta;
}

// FBSBase.ChangePtn + FBSAssist.TimeProgressCtrl. Interrupted changes copy
// dictNowFace (the old destination), not the current interpolated result.
struct FOKFbsTransition
{
	int32 Back = 0, Now = 0;
	float Elapsed = .15f;
	void Reset(int32 Pattern) { Back=Now=Pattern; Elapsed=.15f; }
	void Change(int32 Pattern, bool bBlend)
	{
		if (Now==Pattern) return;
		Back=Now; Now=Pattern; Elapsed=bBlend ? 0.f : .15f;
	}
	float Alpha() const { return FMath::Clamp(Elapsed/.15f,0.f,1.f); }
	bool Advance(float Dt)
	{
		const float Old=Elapsed;
		Elapsed=FMath::Min(.15f,Elapsed+Dt);
		return Old!=Elapsed;
	}
};

// FBSBlinkControl. Random inputs can be injected by native trace tests;
// production uses the same uniform domains, not an invented periodic wave.
struct FOKFbsBlink
{
	uint8 FixedFlags = 0;
	int32 Frequency = 30, Mode = 0, Count = 0;
	float BaseSpeed=.15f, CalcSpeed=0.f, Deadline=0.f, OpenRate=1.f;
	void SetSpeed(float Value) { BaseSpeed=FMath::Max(1.f,Value); }
	template<class FloatRandom, class IntRandom>
	void ForceClose(float Time, FloatRandom FloatRng, IntRandom IntRng)
	{
		CalcSpeed=BaseSpeed+FloatRng(0.f,.05f);
		Deadline=Time+CalcSpeed; Count=IntRng(0,3)+1; Mode=1;
	}
	template<class FloatRandom>
	void ForceOpen(float Time, FloatRandom FloatRng)
	{
		CalcSpeed=BaseSpeed+FloatRng(0.f,.05f);
		Deadline=Time+CalcSpeed; Mode=-1;
	}
	template<class IntRandom>
	void SetFrequency(int32 Value, float Time, IntRandom IntRng)
	{
		Frequency=Value;
		if(Mode==0) Deadline=Time+.2f*(Frequency>0?IntRng(0,Frequency):0);
	}
	template<class FloatRandom, class IntRandom>
	void Calculate(float Time, FloatRandom FloatRng, IntRandom IntRng)
	{
		const float Remaining=FMath::Max(0.f,Deadline-Time);
		const float Value=Mode==0 ? 1.f : (Mode==1 ? FMath::Clamp(Remaining/CalcSpeed,0.f,1.f)
			: FMath::Clamp(1.f-Remaining/CalcSpeed,0.f,1.f));
		if(FixedFlags==0) OpenRate=Value;
		if(FixedFlags!=0 || Time<=Deadline) return;
		if(Mode==0) ForceClose(Time,FloatRng,IntRng);
		else if(Mode==1) { if(--Count<=0) ForceOpen(Time,FloatRng); }
		else { Deadline=Time+.2f*(Frequency>0?IntRng(0,Frequency):0); Mode=0; }
	}
};
