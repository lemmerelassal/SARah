// PDBViewer_Lighting.cpp - Ligand atom lighting system

#include "PDBViewer.h"
#include "Components/PointLightComponent.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

void APDBViewer::CreateLigandAtomLights(FLigandInfo* LigInfo)
{
    if (!LigInfo)
        return;

    // Clear any existing lights
    ClearLigandAtomLights(LigInfo);

    // Create a light for each atom
    for (int32 i = 0; i < LigInfo->AtomPositions.Num(); ++i)
    {
        UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
        if (!Light)
            continue;

        Light->RegisterComponent();
        Light->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

        // Set position (scaled to match atom position)
        FVector ScaledPos = LigInfo->AtomPositions[i] * PDB::SCALE;
        Light->SetRelativeLocation(ScaledPos);

        // Set light color based on element
        FLinearColor LightColor = FLinearColor::White;
        if (bUseDynamicAtomColors && LigInfo->AtomElements.IsValidIndex(i))
        {
            LightColor = GetLightColorForElement(LigInfo->AtomElements[i]);
        }
        Light->SetLightColor(LightColor);

        // Set light properties
        Light->SetIntensity(LigandAtomLightIntensity);
        Light->SetAttenuationRadius(LigandAtomLightRadius);
        Light->SetCastShadows(false); // Disable shadows for performance
        Light->SetMobility(EComponentMobility::Movable);

        // Set initial visibility based on ligand visibility and global light setting
        Light->SetVisibility(bLigandAtomLightsEnabled && LigInfo->bIsVisible);

        LigInfo->AtomLights.Add(Light);
    }

    UE_LOG(LogTemp, Log, TEXT("Created %d atom lights for ligand %s"),
           LigInfo->AtomLights.Num(), *LigInfo->LigandName);
}

void APDBViewer::UpdateLigandAtomLights(FLigandInfo* LigInfo)
{
    if (!LigInfo)
        return;

    for (UPointLightComponent* Light : LigInfo->AtomLights)
    {
        if (!Light || !IsValid(Light))
            continue;

        // Update visibility
        Light->SetVisibility(bLigandAtomLightsEnabled && LigInfo->bIsVisible);

        // Update intensity and radius
        Light->SetIntensity(LigandAtomLightIntensity);
        Light->SetAttenuationRadius(LigandAtomLightRadius);
    }
}

void APDBViewer::ClearLigandAtomLights(FLigandInfo* LigInfo)
{
    if (!LigInfo)
        return;

    for (UPointLightComponent* Light : LigInfo->AtomLights)
    {
        if (Light && IsValid(Light))
        {
            Light->DestroyComponent();
        }
    }

    LigInfo->AtomLights.Empty();
}

FLinearColor APDBViewer::GetLightColorForElement(const FString& Element) const
{
    // Use the same color scheme as atoms
    return GetElementColor(Element);
}

void APDBViewer::SetLigandAtomLightsEnabled(bool bEnabled)
{
    bLigandAtomLightsEnabled = bEnabled;

    // OPTIMIZED: Early exit if no ligands
    if (LigandMap.Num() == 0)
        return;

    // OPTIMIZED: Only update lights for visible ligands
    for (auto& Pair : LigandMap)
    {
        if (Pair.Value && Pair.Value->bIsVisible)
        {
            UpdateLigandAtomLights(Pair.Value);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Ligand atom lights %s"),
           bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void APDBViewer::ToggleLigandAtomLights()
{
    SetLigandAtomLightsEnabled(!bLigandAtomLightsEnabled);
}

void APDBViewer::SetLigandAtomLightIntensity(float Intensity)
{
    LigandAtomLightIntensity = Intensity;

    if (LigandMap.Num() == 0)
        return;

    ForEachValidLigandLight([Intensity](UPointLightComponent* Light) {
        Light->SetIntensity(Intensity);
    });

    UE_LOG(LogTemp, Log, TEXT("Set ligand atom light intensity to %.1f"), Intensity);
}

void APDBViewer::SetLigandAtomLightRadius(float Radius)
{
    LigandAtomLightRadius = Radius;

    if (LigandMap.Num() == 0)
        return;

    ForEachValidLigandLight([Radius](UPointLightComponent* Light) {
        Light->SetAttenuationRadius(Radius);
    });

    UE_LOG(LogTemp, Log, TEXT("Set ligand atom light radius to %.1f"), Radius);
}

int32 APDBViewer::GetLigandAtomLightCount() const
{
    int32 Count = 0;

    for (const auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            Count += Pair.Value->AtomLights.Num();
        }
    }

    return Count;
}
