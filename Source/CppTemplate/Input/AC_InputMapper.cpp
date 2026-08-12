#include "AC_InputMapper.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

// Writes the action value into a function parameter, converting to its type.
static void WriteActionValue(FProperty* Param, void* Frame, const FInputActionValue& Value)
{
    if (const FFloatProperty* P = CastField<FFloatProperty>(Param))
    {
        P->SetPropertyValue_InContainer(Frame, Value.Get<float>());
        return;
    }
    if (const FDoubleProperty* P = CastField<FDoubleProperty>(Param))
    {
        P->SetPropertyValue_InContainer(Frame, Value.Get<float>());
        return;
    }
    if (const FBoolProperty* P = CastField<FBoolProperty>(Param))
    {
        P->SetPropertyValue_InContainer(Frame, Value.Get<bool>());
        return;
    }
    if (const FStructProperty* P = CastField<FStructProperty>(Param))
    {
        void* Dest = P->ContainerPtrToValuePtr<void>(Frame);
        if (P->Struct == FInputActionValue::StaticStruct())     *static_cast<FInputActionValue*>(Dest) = Value;
        else if (P->Struct == TBaseStructure<FVector2D>::Get()) *static_cast<FVector2D*>(Dest) = Value.Get<FVector2D>();
        else if (P->Struct == TBaseStructure<FVector>::Get())   *static_cast<FVector*>(Dest) = Value.Get<FVector>();
    }
}

UAC_InputMapper::UAC_InputMapper()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAC_InputMapper::BeginPlay()
{
    Super::BeginPlay();

    if (APawn* Pawn = Cast<APawn>(GetOwner()))
    {
        TrySetupInput();  // already possessed?
        Pawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UAC_InputMapper::HandleControllerChanged);
    }
}

void UAC_InputMapper::HandleControllerChanged(APawn*, AController*, AController*)
{
    TrySetupInput();
}

void UAC_InputMapper::TrySetupInput()
{
    if (bBound) return;

    APawn* Pawn = Cast<APawn>(GetOwner());
    APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    UEnhancedInputComponent* EIC = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
    if (!PC || !EIC) return;   // not possessed / no input component yet

    if (MappingContext)
    {
        if (ULocalPlayer* LP = PC->GetLocalPlayer())
        {
            if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
                Subsystem->AddMappingContext(MappingContext, MappingPriority);
        }
    }

    TWeakObjectPtr<UAC_InputMapper> WeakThis(this);
    for (const FMappedInputBinding& B : Bindings)
    {
        if (!B.Action || B.FunctionName.IsNone()) continue;

        const FName Fn = B.FunctionName;
        EIC->BindActionValueLambda(B.Action, B.TriggerEvent,
            [WeakThis, Fn](const FInputActionValue& Value)
            {
                if (UAC_InputMapper* Self = WeakThis.Get())
                    Self->DispatchInput(Value, Fn);
            });
    }

    bBound = true;
}

void UAC_InputMapper::DispatchInput(const FInputActionValue& Value, FName FunctionName)
{
    AActor* Target = GetOwner();
    UFunction* Func = Target ? Target->FindFunction(FunctionName) : nullptr;
    if (!Func) return;

    if (Func->NumParms == 0)   // parameterless, e.g. Jump
    {
        Target->ProcessEvent(Func, nullptr);
        return;
    }

    void* Frame = FMemory_Alloca(Func->ParmsSize);
    FMemory::Memzero(Frame, Func->ParmsSize);

    for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            WriteActionValue(*It, Frame, Value);
            break;
        }
    }

    Target->ProcessEvent(Func, Frame);
}

TArray<FString> UAC_InputMapper::GetBindableFunctionNames() const
{
    TArray<FString> Names;
    if (const UClass* OwnerClass = ResolveOwnerClass())
    {
        for (TFieldIterator<UFunction> It(OwnerClass); It; ++It)
            Names.Add(It->GetName());
        Names.Sort();
    }
    return Names;
}

const UClass* UAC_InputMapper::ResolveOwnerClass() const
{
    if (const AActor* Owner = GetOwner())
        return Owner->GetClass();

    // Edit time: walk the outer chain to the owning Blueprint/actor class.
    for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
    {
        if (const AActor* OuterActor = Cast<AActor>(Outer))
            return OuterActor->GetClass();
        if (const UClass* AsClass = Cast<UClass>(Outer))
            if (AsClass->IsChildOf(AActor::StaticClass()))
                return AsClass;
    }
    return nullptr;
}