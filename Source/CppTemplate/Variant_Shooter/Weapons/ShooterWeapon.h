// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class USkeletalMeshComponent;
class UAnimMontage;
class UAnimInstance;
class USoundBase;
class USoundAttenuation;
struct FHitResult;

/** A hitscan trace line drawn without DrawDebugLine, faded out manually over its lifetime */
struct FShooterHitscanFadeLine
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	float ElapsedTime = 0.0f;
};

/**
 *  Base class for a simple first person shooter weapon
 *  Provides both first person and third person perspective meshes
 *  Handles ammo and firing logic
 *  Interacts with the weapon owner through the ShooterWeaponHolder interface
 */
UCLASS(abstract)
class CPPTEMPLATE_API AShooterWeapon : public AActor
{
	GENERATED_BODY()
	
	/** First person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** Third person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ThirdPersonMesh;

protected:

	/** Cast pointer to the weapon owner */
	IShooterWeaponHolder* WeaponOwner;

	/** Type of projectiles this weapon will shoot */
	UPROPERTY(EditAnywhere, Category="Ammo")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** Number of bullets in a magazine */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = 0, ClampMax = 100))
	int32 MagazineSize = 10;

	/** Number of bullets in the current magazine */
	int32 CurrentBullets = 0;
	
	/** Animation montage to play when firing this weapon */
	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* FiringMontage;

	/** AnimInstance class to set for the first person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	/** AnimInstance class to set for the third person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	/** Sound played at the muzzle location each time this weapon fires */
	UPROPERTY(EditAnywhere, Category="Audio")
	USoundBase* FiringSound;

	UPROPERTY(EditAnywhere,Category="Audio")
	USoundBase* ReloadingSound;

	/** Distance from the shot within which FiringSound plays at full volume */
	UPROPERTY(EditAnywhere, Category="Audio", meta = (ClampMin = 0, Units = "cm"))
	float FiringSoundFullVolumeRange = 500.0f;

	/** Distance beyond FiringSoundFullVolumeRange over which FiringSound fades out to silence */
	UPROPERTY(EditAnywhere, Category="Audio", meta = (ClampMin = 0, Units = "cm"))
	float FiringSoundFalloffRange = 5500.0f;

	/** Runtime-constructed attenuation so FiringSound is quieter the farther the listener is from the shot */
	UPROPERTY()
	USoundAttenuation* FiringSoundAttenuation = nullptr;

	// NPC reload cover behavior

	/** If true, non-player weapon owners visually hide while reloading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover")
	bool bNPCEnterCoverOnReload = true;

	/** How much of the owner's mesh height should sink while reloading. 0.5 = 50%. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover", meta=(ClampMin="0.0", ClampMax="1.0"))
	float NPCReloadSinkPercent = 0.5f;

	/** Time it takes for NPC owner mesh to move down into cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover", meta=(ClampMin="0.01", Units="s"))
	float NPCReloadCoverLowerDuration = 0.2f;

	/** Time it takes for NPC owner mesh to move back up from cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover", meta=(ClampMin="0.01", Units="s"))
	float NPCReloadCoverRaiseDuration = 0.35f;

	/** If true, rotates the NPC mesh while it visually enters reload cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around")
	bool bNPCReloadRotateMeshInCover = true;

	/** Yaw rotation applied when the NPC is fully hidden. 180 makes the mesh look backwards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(Units="deg"))
	float NPCReloadCoverBackYaw = 180.0f;

	/** If true, the reload cover look-around uses random targets, pauses and speeds instead of a sine wave. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around")
	bool bUseNaturalReloadLookAround = true;

	/** Fallback sine-wave side-to-side yaw while hidden, used if natural look-around is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="0.0", Units="deg"))
	float NPCReloadCoverLookAroundYaw = 35.0f;

	/** Fallback sine-wave oscillations per second while hidden, used if natural look-around is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="0.0", Units="Hz"))
	float NPCReloadCoverLookAroundFrequency = 1.25f;

	/** Maximum yaw offset used while looking around naturally in cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="0.0", Units="deg"))
	float NPCReloadNaturalLookYawRange = 45.0f;

	/** Minimum angular speed while turning the mesh in cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="1.0", Units="deg/s"))
	float NPCReloadLookAroundMinTurnSpeed = 80.0f;

	/** Maximum angular speed while turning the mesh in cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="1.0", Units="deg/s"))
	float NPCReloadLookAroundMaxTurnSpeed = 220.0f;

	/** Minimum time the NPC pauses after reaching a look direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="0.0", Units="s"))
	float NPCReloadLookAroundMinPause = 0.15f;

	/** Maximum time the NPC pauses after reaching a look direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="0.0", Units="s"))
	float NPCReloadLookAroundMaxPause = 0.65f;

	/** How close the look yaw must be to the target before choosing a pause/new target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover|Look Around", meta=(ClampMin="0.1", Units="deg"))
	float NPCReloadLookAroundTargetTolerance = 2.0f;

	/** If true, disables the owner's skeletal mesh collision during reload. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover")
	bool bDisableOwnerMeshCollisionDuringReload = true;

	/**
	* Optional safety option.
	* If hitscan still damages NPCs while reloading, enable this because Character capsule may still block ECC_Pawn.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload|Cover")
	bool bDisableOwnerCapsuleCollisionDuringReload = false;

	/** Reload sound attenuation created at runtime. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> ReloadingSoundAttenuation;

	/** Full-volume range for reload sound attenuation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio|Reload")
	float ReloadingSoundFullVolumeRange = 400.0f;

	/** Falloff range for reload sound attenuation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio|Reload")
	float ReloadingSoundFalloffRange = 1600.0f;

	/** Cached mesh location before applying reload cover. */
	FVector CachedOwnerMeshRelativeLocation = FVector::ZeroVector;

	/** Cached mesh rotation before applying reload cover. */
	FRotator CachedOwnerMeshRelativeRotation = FRotator::ZeroRotator;

	/** Runtime current natural look-around yaw offset during reload cover. */
	float ActiveReloadLookAroundCurrentYaw = 0.0f;

	/** Runtime target natural look-around yaw offset during reload cover. */
	float ActiveReloadLookAroundTargetYaw = 0.0f;

	/** Runtime turning speed for current natural look-around movement. */
	float ActiveReloadLookAroundTurnSpeed = 120.0f;

	/** Runtime pause remaining before choosing a new look direction. */
	float ActiveReloadLookAroundPauseRemaining = 0.0f;

	/** Cached owner mesh collision state before reload cover. */
	ECollisionEnabled::Type CachedOwnerMeshCollisionEnabled = ECollisionEnabled::QueryAndPhysics;

	/** Cached owner capsule collision state before reload cover. */
	ECollisionEnabled::Type CachedOwnerCapsuleCollisionEnabled = ECollisionEnabled::QueryAndPhysics;

	/** Whether reload cover state is currently active. */
	bool bNPCReloadCoverActive = false;

	/** Returns true if the weapon owner is an NPC / non-player pawn. */
	bool ShouldApplyNPCReloadCover() const;

	/** Starts reload cover effects on non-player owner. */
	void BeginNPCReloadCover();

	/** Updates visual sinking amount while reloading. */
	void UpdateNPCReloadCover(float Alpha, float DeltaSeconds);

	/** Calculates the regular reload hands alpha. */
	float GetReloadHandsAlpha() const;

	/** Calculates the NPC cover alpha during reload: down, hold, up. */
	float GetNPCReloadCoverAlpha() const;

	/** Picks a new random look-around target while the NPC is hidden in reload cover. */
	void PickNextNPCReloadLookAroundTarget();

	/** Restores owner mesh/collision after reload cover. */
	void EndNPCReloadCover();

	/** Cone half-angle for variance while aiming */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float AimVariance = 0.0f;

	/** Amount of vertical recoil kick to apply to the owner's arms when firing */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 100))
	float FiringRecoil = 3.0f;

	/** Name of the first person muzzle socket where projectiles will spawn */
	UPROPERTY(EditAnywhere, Category="Aim")
	FName MuzzleSocketName;

	/** Distance ahead of the muzzle that bullets will spawn at */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float MuzzleOffset = 10.0f;

	/** If true, this weapon fires an instant hitscan trace instead of spawning a projectile actor */
	UPROPERTY(EditAnywhere, Category="Hitscan")
	bool bHitscan = false;

	/** Damage applied to a hit character when firing in hitscan mode */
	UPROPERTY(EditAnywhere, Category="Hitscan", meta = (ClampMin = 0, ClampMax = 100, EditCondition = "bHitscan"))
	float HitDamage = 25.0f;

	/** Type of damage applied by hitscan hits */
	UPROPERTY(EditAnywhere, Category="Hitscan", meta = (EditCondition = "bHitscan"))
	TSubclassOf<UDamageType> HitDamageType;

	/** Max trace distance for hitscan shots */
	UPROPERTY(EditAnywhere, Category="Hitscan", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm", EditCondition = "bHitscan"))
	float MaxRange = 10000.0f;

	/** If true, draws the hitscan trace line and logs hit info to the Output Log every shot */
	UPROPERTY(EditAnywhere, Category="Hitscan", meta = (EditCondition = "bHitscan"))
	bool bDrawHitscanDebugLine = false;

	/** Color and opacity (alpha) of the hitscan trace line drawn when the debug line is disabled */
	UPROPERTY(EditAnywhere, Category="Hitscan", meta = (EditCondition = "bHitscan && !bDrawHitscanDebugLine"))
	FLinearColor HitscanLineColor;

	/** How long the hitscan trace line stays visible when the debug line is disabled */
	UPROPERTY(EditAnywhere, Category="Hitscan", meta = (ClampMin = 0, ClampMax = 60, Units = "s", EditCondition = "bHitscan && !bDrawHitscanDebugLine"))
	float HitscanLineDuration = 3.0f;

	/** If true, this weapon will automatically fire at the refire rate */
	UPROPERTY(EditAnywhere, Category="Refire")
	bool bFullAuto = false;

	/** Time between shots for this weapon. Affects both full auto and semi auto modes */
	UPROPERTY(EditAnywhere, Category="Refire", meta = (ClampMin = 0, ClampMax = 5, Units = "s"))
	float RefireRate = 0.5f;

	/** Random +/- variance applied to the refire rate each shot, so multiple actors using the same weapon values don't stay perfectly in sync */
	UPROPERTY(EditAnywhere, Category="Refire", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RefireRateVariance = 0.0f;

	/** Game time at which the weapon is next allowed to fire (includes RefireRateVariance jitter from the last shot), used to enforce refire rate on semi auto */
	float NextFireTime = 0.0f;

	/** If true, the weapon is currently firing */
	bool bIsFiring = false;

	/** Timer to handle full auto refiring */
	FTimerHandle RefireTimer;

	/** Hitscan trace lines currently fading out, drawn and updated every tick instead of via DrawDebugLine */
	TArray<FShooterHitscanFadeLine> ActiveHitscanFadeLines;

	/** Updates and redraws the currently fading hitscan trace lines, removing any that have finished fading */
	void UpdateHitscanLineFade(float DeltaSeconds);

	/** Duration of the reload sequence, during which the weapon cannot fire */
	UPROPERTY(EditAnywhere, Category="Reload", meta = (ClampMin = 0.1, ClampMax = 10, Units = "s"))
	float ReloadDuration = 2.0f;

	/** Extra random time added to ReloadDuration. Final reload time = ReloadDuration + Random(0, ReloadDurationRandomExtra). */
	UPROPERTY(EditAnywhere, Category="Reload", meta = (ClampMin = 0.0, ClampMax = 10, Units = "s"))
	float ReloadDurationRandomExtra = 0.0f;

	/** Runtime reload duration for the current reload instance. */
	UPROPERTY(Transient)
	float ActiveReloadDuration = 0.0f;

	/** Local offset the owner's hands/arms mesh is lowered by, at the peak of the reload animation */
	UPROPERTY(EditAnywhere, Category="Reload")
	FVector ReloadLoweredOffset = FVector(-20.0f, 0.0f, 0.0f);

	/** If true, the weapon is currently playing its reload sequence and cannot fire */
	bool bIsReloading = false;

	/** Time elapsed since the reload sequence started */
	float ReloadElapsed = 0.0f;

	/** Cast pawn pointer to the owner for AI perception system interactions */
	TObjectPtr<APawn> PawnOwner;

	/** Loudness of the shot for AI perception system interactions */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100))
	float ShotLoudness = 1.0f;

	/** Max range of shot AI perception noise */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float ShotNoiseRange = 300.0f;

	/** Tag to apply to noise generated by shooting this weapon */
	UPROPERTY(EditAnywhere, Category="Perception")
	FName NoiseOwnerTag = FName("Shot");

public:	

	/** Constructor */
	AShooterWeapon();

protected:
	
	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay Cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Drives the reload sequence's mesh animation */
	virtual void Tick(float DeltaSeconds) override;

protected:

	/** Called when the weapon's owner is destroyed */
	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

public:

	/** Activates this weapon and gets it ready to fire */
	void ActivateWeapon(const FName& OwnerTag);

	/** Deactivates this weapon */
	void DeactivateWeapon();

	/** Start firing this weapon */
	void StartFiring();

	/** Stop firing this weapon */
	void StopFiring();

	/** Starts the reload sequence: blocks firing, lowers the weapon, then resets it and refills the clip */
	void StartReload();

protected:

	/** Fire the weapon */
	virtual void Fire();

	/** Called when the refire rate time has passed while shooting semi auto weapons */
	void FireCooldownExpired();



	/** Fire a projectile towards the target location */
	virtual void FireProjectile(const FVector& TargetLocation);

	/** Fire an instant hitscan trace towards the target location */
	virtual void FireHitscan(const FVector& TargetLocation);

	/** Passes control to Blueprint to implement any tracer/impact effects for a hitscan shot. Called every shot, whether it hit something or not. bDidDamage is true if the hit actually applied damage to a character. */
	UFUNCTION(BlueprintImplementableEvent, Category="Weapon", meta = (DisplayName = "On Hitscan Fire"))
	void BP_OnHitscanFire(const FHitResult& Hit, const FVector& TraceEnd, bool bDidDamage);

	/** Calculates the spawn transform for projectiles shot by this weapon */
	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation) const;

	/** Returns the refire rate with a random +/- RefireRateVariance jitter applied, so multiple actors don't fire in lockstep */
	float GetJitteredRefireRate() const;

public:

	/** Returns the first person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; };

	/** Returns the third person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetThirdPersonMesh() const { return ThirdPersonMesh; };

	/** Returns the first person anim instance class */
	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;

	/** Returns the third person anim instance class */
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	/** Returns the magazine size */
	int32 GetMagazineSize() const { return MagazineSize; };

	/** Returns the current bullet count */
	int32 GetBulletCount() const { return CurrentBullets; }
};
