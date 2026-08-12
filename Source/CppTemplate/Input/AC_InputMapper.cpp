#include "AC_InputMapper.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UAC_InputMapper::UAC_InputMapper()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAC_InputMapper::BeginPlay()
{
    Super::BeginPlay();

    if (APawn* Pawn = Cast<APawn>(GetOwner()))
    {
        TrySetupInput(); // in case we are already possessed
        Pawn->ReceiveControllerChangedDelegate.AddDynamic(
            this, &UAC_InputMapper::HandleControllerChanged);
    }
}

void UAC_InputMapper::HandleControllerChanged(APawn* /*Pawn*/, AController* /*OldController*/, AController* /*NewController*/)
{
    TrySetupInput();
}

void UAC_InputMapper::TrySetupInput()
{
    if (bBound)
    {
        return;
    }

    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn)
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    if (!PC)
    {
        return; // not yet possessed by a player
    }

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
    if (!EIC)
    {
        return; // input component not created yet
    }

    // Add the mapping context (optional)
    if (MappingContext)
    {
        if (ULocalPlayer* LP = PC->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                    ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
            {
                Subsystem->AddMappingContext(MappingContext, MappingPriority);
            }
        }
    }

    // Bind each action through a lambda that marshals the value to the target function.
    TWeakObjectPtr<UAC_InputMapper> WeakThis(this);
    for (const FMappedInputBinding& B : Bindings)
    {
        if (!B.Action || B.FunctionName.IsNone())
        {
            continue;
        }

        const FName Fn = B.FunctionName;
        EIC->BindActionValueLambda(B.Action, B.TriggerEvent,
            [WeakThis, Fn](const FInputActionValue& Value)
            {
                if (UAC_InputMapper* Self = WeakThis.Get())
                {
                    Self->DispatchInput(Value, Fn);
                }
            });
    }

    bBound = true;
}

void UAC_InputMapper::DispatchInput(const FInputActionValue& Value, FName FunctionName)
{
    AActor* Target = GetOwner();
    if (!Target)
    {
        return;
    }

    UFunction* Func = Target->FindFunction(FunctionName);
    if (!Func)
    {
        return;
    }

    // Find the first non-return parameter, if any.
    FProperty* FirstParm = nullptr;
    for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        FirstParm = *It;
        break;
    }

    // Parameterless function (e.g. Jump / StopJumping) -> just invoke it.
    if (!FirstParm)
    {
        Target->ProcessEvent(Func, nullptr);
        return;
    }

    // Allocate and initialize a parameter frame sized to the function.
    void* Params = FMemory::Malloc(FMath::Max<int32>(Func->ParmsSize, 1));
    FMemory::Memzero(Params, Func->ParmsSize);
    for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        It->InitializeValue_InContainer(Params);
    }

    // Fill the first parameter, converting the action value to the expected type.
    if (FFloatProperty* FloatProp = CastField<FFloatProperty>(FirstParm))
    {
        FloatProp->SetPropertyValue_InContainer(Params, Value.Get<float>());
    }
    else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(FirstParm))
    {
        DoubleProp->SetPropertyValue_InContainer(Params, static_cast<double>(Value.Get<float>()));
    }
    else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(FirstParm))
    {
        BoolProp->SetPropertyValue_InContainer(Params, Value.Get<bool>());
    }
    else if (FStructProperty* StructProp = CastField<FStructProperty>(FirstParm))
    {
        if (StructProp->Struct == FInputActionValue::StaticStruct())
        {
            *StructProp->ContainerPtrToValuePtr<FInputActionValue>(Params) = Value;
        }
        else if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
        {
            *StructProp->ContainerPtrToValuePtr<FVector2D>(Params) = Value.Get<FVector2D>();
        }
        else if (StructProp->Struct == TBaseStructure<FVector>::Get())
        {
            *StructProp->ContainerPtrToValuePtr<FVector>(Params) = Value.Get<FVector>();
        }
    }

    Target->ProcessEvent(Func, Params);

    // Tear down the parameter frame.
    for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        It->DestroyValue_InContainer(Params);
    }
    FMemory::Free(Params);
}

TArray<FString> UAC_InputMapper::GetBindableFunctionNames() const
{
    TArray<FString> Names;

    const UClass* OwnerClass = ResolveOwnerClass();
    if (!OwnerClass)
    {
        return Names;
    }

    for (TFieldIterator<UFunction> It(OwnerClass); It; ++It)
    {
        Names.Add(It->GetName());
    }

    Names.Sort();
    return Names;
}

const UClass* UAC_InputMapper::ResolveOwnerClass() const
{
    // Runtime: the component has a real owning actor.
    if (const AActor* Owner = GetOwner())
    {
        return Owner->GetClass();
    }

    // Edit time: the component is a template inside a Blueprint. Walk the outer
    // chain to find the owning actor's class (a UBlueprintGeneratedClass) or actor.
    for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
    {
        if (const AActor* OuterActor = Cast<AActor>(Outer))
        {
            return OuterActor->GetClass();
        }
        if (const UClass* AsClass = Cast<UClass>(Outer))
        {
            if (AsClass->IsChildOf(AActor::StaticClass()))
            {
                return AsClass;
            }
        }
    }

    return nullptr;
}