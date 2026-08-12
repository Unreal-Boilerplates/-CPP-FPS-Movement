// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerCharacter.h"
#include "Camera/CameraComponent.h"


APlayerCharacter::APlayerCharacter()
{

	PrimaryActorTick.bCanEverTick = true;

	// Keeps camera attached to Pawn
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Player Camera"));
	Camera -> SetupAttachment(RootComponent);
	Camera ->bUsePawnControlRotation = true;
}


void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}


void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}


void APlayerCharacter::MoveForward(float Value)
{
	FVector ForwardDirection = GetActorForwardVector();
	AddMovementInput(ForwardDirection, Value);
}

void APlayerCharacter::MoveRight(float Value)
{
	FVector RightDirection = GetActorRightVector();
	AddMovementInput(RightDirection, Value);
}

void APlayerCharacter::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void APlayerCharacter::LookUp(float Value)
{
	AddControllerPitchInput(-Value);
}
