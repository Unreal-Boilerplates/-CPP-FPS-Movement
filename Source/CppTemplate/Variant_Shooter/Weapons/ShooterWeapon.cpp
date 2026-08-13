// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/HitResult.h"
#include "DrawDebugHelpers.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Components/LineBatchComponent.h"
#include "Components/CapsuleComponent.h"

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;

	// default hitscan line color: mid gray at half opacity
	HitscanLineColor = FLinearColor(FColor::FromHex("#6B6B6B"));
	HitscanLineColor.A = 0.5f;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	// subscribe to the owner's destroyed delegate
	GetOwner()->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	// fill the first ammo clip
	CurrentBullets = MagazineSize;

	// configure runtime attenuation so the firing sound is quieter the farther the listener is from the shot
	FiringSoundAttenuation = NewObject<USoundAttenuation>(this);
	FiringSoundAttenuation->Attenuation.bAttenuate = true;
	FiringSoundAttenuation->Attenuation.bSpatialize = true;
	FiringSoundAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	FiringSoundAttenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	FiringSoundAttenuation->Attenuation.AttenuationShapeExtents = FVector(FiringSoundFullVolumeRange, 0.0f, 0.0f);
	FiringSoundAttenuation->Attenuation.FalloffDistance = FiringSoundFalloffRange;

	// configure runtime attenuation for reload sound
	ReloadingSoundAttenuation = NewObject<USoundAttenuation>(this);
	ReloadingSoundAttenuation->Attenuation.bAttenuate = true;
	ReloadingSoundAttenuation->Attenuation.bSpatialize = true;
	ReloadingSoundAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	ReloadingSoundAttenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	ReloadingSoundAttenuation->Attenuation.AttenuationShapeExtents = FVector(ReloadingSoundFullVolumeRange, 0.0f, 0.0f);
	ReloadingSoundAttenuation->Attenuation.FalloffDistance = ReloadingSoundFalloffRange;

	// attach the meshes to the owner
	WeaponOwner->AttachWeaponMeshes(this);
}

void AShooterWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateHitscanLineFade(DeltaSeconds);

	if (!bIsReloading)
	{
		return;
	}

	ReloadElapsed += DeltaSeconds;

	// reload finished: refill the clip, reset the hands, and allow firing again
	if (ReloadElapsed >= ActiveReloadDuration)
	{
		bIsReloading = false;
		CurrentBullets = MagazineSize;

		WeaponOwner->SetHandsOffset(FVector::ZeroVector);
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);

		EndNPCReloadCover();
		ActiveReloadDuration = 0.0f;

		return;
	}

	const float HandsAlpha = GetReloadHandsAlpha();
	const float CoverAlpha = GetNPCReloadCoverAlpha();

	WeaponOwner->SetHandsOffset(ReloadLoweredOffset * HandsAlpha);
	UpdateNPCReloadCover(CoverAlpha, DeltaSeconds);
}

void AShooterWeapon::UpdateHitscanLineFade(float DeltaSeconds)
{
	if (ActiveHitscanFadeLines.Num() == 0)
	{
		return;
	}

	ULineBatchComponent* LineBatcher = GetWorld()->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent);

	// redraw each fading line with a diminished alpha, and drop it once it's fully faded
	// re-drawn with a short lifetime each tick since ULineBatchComponent has no built-in fade
	for (int32 Index = ActiveHitscanFadeLines.Num() - 1; Index >= 0; --Index)
	{
		FShooterHitscanFadeLine& FadeLine = ActiveHitscanFadeLines[Index];
		FadeLine.ElapsedTime += DeltaSeconds;

		const float FadeAlpha = 1.0f - (FadeLine.ElapsedTime / HitscanLineDuration);
		if (FadeAlpha <= 0.0f)
		{
			ActiveHitscanFadeLines.RemoveAtSwap(Index);
			continue;
		}

		if (LineBatcher)
		{
			FLinearColor FadedColor = HitscanLineColor;
			FadedColor.A *= FadeAlpha;

			// give the line a bit of headroom past this tick so it survives until the next redraw
			const float LineLifeTime = FMath::Max(DeltaSeconds * 2.0f, 0.05f);
			LineBatcher->DrawLine(FadeLine.Start, FadeLine.End, FadedColor, SDPG_World, 1.0f, LineLifeTime);
		}
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	EndNPCReloadCover();

	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon(const FName& OwnerTag)
{
	// save the owner tag for perception noise detection
	NoiseOwnerTag = OwnerTag;

	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring()
{
	// can't start firing while reloading
	if (bIsReloading)
	{
		return;
	}

	// raise the firing flag
	bIsFiring = true;

	// check how much time remains before we're allowed to fire again
	// this may already be in the past if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeUntilNextShot = NextFireTime - GetWorld()->GetTimeSeconds();

	if (TimeUntilNextShot <= 0.0f)
	{
		// fire the weapon right away
		Fire();

	} else {

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeUntilNextShot, false);
		}

	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	// ensure the player still wants to fire. They may have let go of the trigger
	// also refuse to fire while the reload sequence is playing
	if (!bIsFiring || bIsReloading)
	{
		return;
	}

	// fire a projectile at the target
	FireProjectile(WeaponOwner->GetWeaponTargetLocation());

	// pick this shot's jittered refire delay so multiple actors sharing this weapon's values don't stay in sync, and record when we're next allowed to fire
	const float JitteredRefireRate = GetJitteredRefireRate();
	NextFireTime = GetWorld()->GetTimeSeconds() + JitteredRefireRate;

	// play the firing sound at the muzzle location
	if (FiringSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FiringSound, FirstPersonMesh->GetSocketLocation(MuzzleSocketName), 1.0f, 1.0f, 0.0f, FiringSoundAttenuation);
	}

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, NoiseOwnerTag);

	// that shot may have emptied the clip and started a reload; don't schedule another shot if so
	if (bIsReloading)
	{
		return;
	}

	// are we full auto?
	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, JitteredRefireRate, false);
	} else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, JitteredRefireRate, false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::StartReload()
{
	if (bIsReloading)
	{
		return;
	}

	if (ReloadingSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReloadingSound, FirstPersonMesh->GetSocketLocation(MuzzleSocketName), 1.0f, 1.0f, 0.0f, ReloadingSoundAttenuation);
	}

	// block firing and start timing the reload animation
	bIsReloading = true;
	ReloadElapsed = 0.0f;
	ActiveReloadDuration = ReloadDuration + FMath::FRandRange(0.0f, ReloadDurationRandomExtra);

	// stop firing so a held trigger doesn't queue up more shots during the reload
	StopFiring();

	// NPC-only temporary cover behavior
	BeginNPCReloadCover();
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	// hitscan weapons trace instantly instead of spawning a projectile actor
	if (bHitscan)
	{
		FireHitscan(TargetLocation);
		return;
	}

	// get the projectile transform
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);
	
	// spawn the projectile
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	AShooterProjectile* Projectile = GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);

	// set the noise tag on the projectile
	if (Projectile)
	{
		Projectile->SetNoiseTag(NoiseOwnerTag);
	}

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// add recoil
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	// consume bullets
	--CurrentBullets;

	// update the weapon HUD
	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);

	// if the clip is depleted, start the reload sequence
	if (CurrentBullets <= 0)
	{
		StartReload();
	}
}

void AShooterWeapon::FireHitscan(const FVector& TargetLocation)
{
	// get the trace start transform (same muzzle-based calculation projectiles use)
	const FTransform TraceTransform = CalculateProjectileSpawnTransform(TargetLocation);
	const FVector TraceStart = TraceTransform.GetLocation();
	const FVector TraceEnd = TraceStart + ((TargetLocation - TraceStart).GetSafeNormal() * MaxRange);

	// ignore the weapon owner so we don't hit ourselves
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	const bool bHitSomething = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Pawn, QueryParams);

	if (bDrawHitscanDebugLine)
	{
		DrawDebugLine(GetWorld(), TraceStart, bHitSomething ? Hit.ImpactPoint : TraceEnd, bHitSomething ? FColor::Green : FColor::Red, false, 3.0f, 0, 1.0f);
		UE_LOG(LogTemp, Warning, TEXT("FireHitscan: Start=%s End=%s bHit=%d HitActor=%s HitComp=%s"),
			*TraceStart.ToString(), *TraceEnd.ToString(), bHitSomething,
			bHitSomething && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("None"),
			bHitSomething && Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("None"));
	}
	else
	{
		ActiveHitscanFadeLines.Add(FShooterHitscanFadeLine{ TraceStart, bHitSomething ? Hit.ImpactPoint : TraceEnd, 0.0f });
	}

	// apply damage if we hit a character
	bool bDidDamage = false;
	if (bHitSomething)
	{
		if (ACharacter* HitCharacter = Cast<ACharacter>(Hit.GetActor()))
		{
			const float DamageApplied = UGameplayStatics::ApplyDamage(HitCharacter, HitDamage, PawnOwner->GetController(), this, HitDamageType);
			bDidDamage = DamageApplied > 0.0f;
			if (bDrawHitscanDebugLine)
			{
				UE_LOG(LogTemp, Warning, TEXT("FireHitscan: ApplyDamage to %s returned %f"), *HitCharacter->GetName(), DamageApplied);
			}
		}
		else if (bDrawHitscanDebugLine)
		{
			UE_LOG(LogTemp, Warning, TEXT("FireHitscan: hit actor %s is not an ACharacter, no damage applied"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("None"));
		}
	}

	// let Blueprint handle tracer/impact effects, whether we hit something or not
	BP_OnHitscanFire(Hit, TraceEnd, bDidDamage);

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// add recoil
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	// consume bullets
	--CurrentBullets;

	// update the weapon HUD
	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);

	// if the clip is depleted, start the reload sequence
	if (CurrentBullets <= 0)
	{
		StartReload();
	}
}

float AShooterWeapon::GetJitteredRefireRate() const
{
	return FMath::Max(0.01f, RefireRate + FMath::FRandRange(-RefireRateVariance, RefireRateVariance));
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// find the muzzle location
	const FVector MuzzleLoc = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	// find the aim rotation vector while applying some variance to the target 
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance));

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}

float AShooterWeapon::GetReloadHandsAlpha() const
{
	const float SafeReloadDuration = FMath::Max(ActiveReloadDuration, 0.01f);
	const float HalfDuration = SafeReloadDuration * 0.5f;

	if (ReloadElapsed <= HalfDuration)
	{
		return FMath::Clamp(ReloadElapsed / HalfDuration, 0.0f, 1.0f);
	}

	return FMath::Clamp(1.0f - ((ReloadElapsed - HalfDuration) / HalfDuration), 0.0f, 1.0f);
}

float AShooterWeapon::GetNPCReloadCoverAlpha() const
{
	const float SafeReloadDuration = FMath::Max(ActiveReloadDuration, 0.01f);

	const float LowerDuration = FMath::Clamp(NPCReloadCoverLowerDuration, 0.01f, SafeReloadDuration);
	const float RaiseDuration = FMath::Clamp(NPCReloadCoverRaiseDuration, 0.01f, SafeReloadDuration);

	const float LowerEndTime = LowerDuration;
	const float RaiseStartTime = SafeReloadDuration - RaiseDuration;

	// If reload is too short for separate lower / hold / raise phases,
	// fall back to a triangular movement.
	if (RaiseStartTime <= LowerEndTime)
	{
		const float HalfDuration = SafeReloadDuration * 0.5f;

		if (ReloadElapsed <= HalfDuration)
		{
			return FMath::Clamp(ReloadElapsed / HalfDuration, 0.0f, 1.0f);
		}

		return FMath::Clamp(1.0f - ((ReloadElapsed - HalfDuration) / HalfDuration), 0.0f, 1.0f);
	}

	// Phase 1: move down.
	if (ReloadElapsed < LowerEndTime)
	{
		return FMath::Clamp(ReloadElapsed / LowerDuration, 0.0f, 1.0f);
	}

	// Phase 2: stay fully hidden.
	if (ReloadElapsed < RaiseStartTime)
	{
		return 1.0f;
	}

	// Phase 3: move back up.
	return FMath::Clamp((SafeReloadDuration - ReloadElapsed) / RaiseDuration, 0.0f, 1.0f);
}

void AShooterWeapon::PickNextNPCReloadLookAroundTarget()
{
	ActiveReloadLookAroundTargetYaw = FMath::FRandRange(-NPCReloadNaturalLookYawRange, NPCReloadNaturalLookYawRange);

	const float MinTurnSpeed = FMath::Min(NPCReloadLookAroundMinTurnSpeed, NPCReloadLookAroundMaxTurnSpeed);
	const float MaxTurnSpeed = FMath::Max(NPCReloadLookAroundMinTurnSpeed, NPCReloadLookAroundMaxTurnSpeed);
	ActiveReloadLookAroundTurnSpeed = FMath::FRandRange(MinTurnSpeed, MaxTurnSpeed);
}

bool AShooterWeapon::ShouldApplyNPCReloadCover() const
{
	if (!bNPCEnterCoverOnReload)
	{
		return false;
	}

	if (!PawnOwner)
	{
		return false;
	}

	// Only NPCs / non-player controlled pawns should use this temporary cover behavior.
	return !PawnOwner->IsPlayerControlled();
}

void AShooterWeapon::BeginNPCReloadCover()
{
	if (!ShouldApplyNPCReloadCover())
	{
		return;
	}

	if (bNPCReloadCoverActive)
	{
		return;
	}

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	USkeletalMeshComponent* OwnerMesh = CharacterOwner->GetMesh();
	if (!OwnerMesh)
	{
		return;
	}

	if (OwnerMesh->IsSimulatingPhysics())
	{
		return;
	}

	bNPCReloadCoverActive = true;

	CachedOwnerMeshRelativeLocation = OwnerMesh->GetRelativeLocation();
	CachedOwnerMeshRelativeRotation = OwnerMesh->GetRelativeRotation();
	CachedOwnerMeshCollisionEnabled = OwnerMesh->GetCollisionEnabled();

	ActiveReloadLookAroundCurrentYaw = 0.0f;
	ActiveReloadLookAroundPauseRemaining = FMath::FRandRange(NPCReloadLookAroundMinPause, NPCReloadLookAroundMaxPause);
	PickNextNPCReloadLookAroundTarget();

	if (bDisableOwnerMeshCollisionDuringReload)
	{
		OwnerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (bDisableOwnerCapsuleCollisionDuringReload)
	{
		if (UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent())
		{
			CachedOwnerCapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void AShooterWeapon::UpdateNPCReloadCover(float Alpha, float DeltaSeconds)
{
	if (!bNPCReloadCoverActive)
	{
		return;
	}

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	USkeletalMeshComponent* OwnerMesh = CharacterOwner->GetMesh();
	if (!OwnerMesh)
	{
		return;
	}
	if (OwnerMesh->IsSimulatingPhysics())
	{
		EndNPCReloadCover();
		return;
	}

	const FBoxSphereBounds LocalBounds = OwnerMesh->GetLocalBounds();

	const float MeshHeight = LocalBounds.BoxExtent.Z * 2.0f;
	const float SinkAmount = MeshHeight * NPCReloadSinkPercent * Alpha;

	FVector NewRelativeLocation = CachedOwnerMeshRelativeLocation;
	NewRelativeLocation.Z -= SinkAmount;

	OwnerMesh->SetRelativeLocation(NewRelativeLocation, false, nullptr, ETeleportType::TeleportPhysics);

	if (bNPCReloadRotateMeshInCover)
	{
		// Rotate toward the back while going down/up.
		float CoverYaw = NPCReloadCoverBackYaw * Alpha;

		// Look-around should only be strong when the NPC is mostly hidden.
		const float LookAroundWeight = FMath::Clamp((Alpha - 0.75f) / 0.25f, 0.0f, 1.0f);

		if (bUseNaturalReloadLookAround && LookAroundWeight > 0.0f)
		{
			if (ActiveReloadLookAroundPauseRemaining > 0.0f)
			{
				ActiveReloadLookAroundPauseRemaining = FMath::Max(0.0f, ActiveReloadLookAroundPauseRemaining - DeltaSeconds);
			}
			else
			{
				ActiveReloadLookAroundCurrentYaw = FMath::FInterpConstantTo(
					ActiveReloadLookAroundCurrentYaw,
					ActiveReloadLookAroundTargetYaw,
					DeltaSeconds,
					ActiveReloadLookAroundTurnSpeed
				);

				const float DistanceToTarget = FMath::Abs(ActiveReloadLookAroundTargetYaw - ActiveReloadLookAroundCurrentYaw);
				if (DistanceToTarget <= NPCReloadLookAroundTargetTolerance)
				{
					ActiveReloadLookAroundPauseRemaining = FMath::FRandRange(NPCReloadLookAroundMinPause, NPCReloadLookAroundMaxPause);
					PickNextNPCReloadLookAroundTarget();
				}
			}

			CoverYaw += ActiveReloadLookAroundCurrentYaw * LookAroundWeight;
		}
		else
		{
			// Fallback to a simple sine motion if natural look-around is disabled.
			const float LookAroundPhase = ReloadElapsed * NPCReloadCoverLookAroundFrequency * 2.0f * PI;
			const float LookAroundYaw = FMath::Sin(LookAroundPhase) * NPCReloadCoverLookAroundYaw * LookAroundWeight;
			CoverYaw += LookAroundYaw;
		}

		FRotator NewRelativeRotation = CachedOwnerMeshRelativeRotation;
		NewRelativeRotation.Yaw += CoverYaw;

		OwnerMesh->SetRelativeRotation(NewRelativeRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AShooterWeapon::EndNPCReloadCover()
{
	if (!bNPCReloadCoverActive)
	{
		return;
	}

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		bNPCReloadCoverActive = false;
		return;
	}

	if (USkeletalMeshComponent* OwnerMesh = CharacterOwner->GetMesh())
	{
		if (!OwnerMesh->IsSimulatingPhysics())
		{
			OwnerMesh->SetRelativeLocation(CachedOwnerMeshRelativeLocation, false, nullptr, ETeleportType::TeleportPhysics);
			OwnerMesh->SetRelativeRotation(CachedOwnerMeshRelativeRotation, false, nullptr, ETeleportType::TeleportPhysics);
		}

		if (bDisableOwnerMeshCollisionDuringReload)
		{
			OwnerMesh->SetCollisionEnabled(CachedOwnerMeshCollisionEnabled);
		}
	}

	if (bDisableOwnerCapsuleCollisionDuringReload)
	{
		if (UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(CachedOwnerCapsuleCollisionEnabled);
		}
	}

	ActiveReloadLookAroundCurrentYaw = 0.0f;
	ActiveReloadLookAroundTargetYaw = 0.0f;
	ActiveReloadLookAroundTurnSpeed = 0.0f;
	ActiveReloadLookAroundPauseRemaining = 0.0f;

	bNPCReloadCoverActive = false;
}
