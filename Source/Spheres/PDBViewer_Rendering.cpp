// PDBViewer_Rendering.cpp - Mesh rendering functions (spheres, bonds, colors)

#include "PDBViewer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

void APDBViewer::DrawSphere(float X, float Y, float Z, const FLinearColor &Col, USceneComponent *Par, TArray<UStaticMeshComponent *> &Out)
{
    if (!SphereMeshAsset || !SphereMaterialAsset || !Par)
        return;

    auto *Sph = NewObject<UStaticMeshComponent>(this);
    Sph->SetStaticMesh(SphereMeshAsset);
    Sph->SetWorldScale3D(FVector(PDB::SPHERE_SIZE));
    Sph->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // OPTIMIZATION #19: Use pooled material instance
    auto *Mat = GetOrCreateMaterial(Col);
    if (Mat)
    {
        Mat->SetScalarParameterValue(TEXT("EmissiveIntensity"), 5.0f);
        Sph->SetMaterial(0, Mat);
    }

    Sph->AttachToComponent(Par, FAttachmentTransformRules::KeepWorldTransform);
    Sph->SetRelativeLocation(Par->GetComponentTransform().InverseTransformPosition(FVector(X, Y, Z)));
    Sph->RegisterComponent();

    Out.Add(Sph);
    AllAtomMeshes.Add(Sph);
}

void APDBViewer::DrawSphere(const FVector& Position, const FLinearColor& Color, USceneComponent* Parent, TArray<UStaticMeshComponent*>& OutArray)
{
    DrawSphere(Position.X, Position.Y, Position.Z, Color, Parent, OutArray);
}

void APDBViewer::DrawBond(const FVector &S, const FVector &E, int32 Ord, const FString &Element1, const FString &Element2, USceneComponent *Par, TArray<UStaticMeshComponent *> &Out)
{
    if (!CylinderMeshAsset || !SphereMaterialAsset || !Par)
        return;

    FVector V = E - S;
    float Len = V.Size();
    if (Len < KINDA_SMALL_NUMBER)
        return;

    FRotator Rot = FRotationMatrix::MakeFromZ(V).Rotator();
    float Scale = Len / 100.0f;

    FLinearColor Color1 = GetElementColor(Element1);
    FLinearColor Color2 = GetElementColor(Element2);

    auto MakeCyl = [&](const FVector &Pos, float ScaleZ, const FLinearColor &Color)
    {
        auto *Cyl = NewObject<UStaticMeshComponent>(this);
        Cyl->SetStaticMesh(CylinderMeshAsset);
        Cyl->SetWorldLocation(Pos);
        Cyl->SetWorldRotation(Rot);
        Cyl->SetWorldScale3D(FVector(PDB::CYLINDER_SIZE, PDB::CYLINDER_SIZE, ScaleZ));
        Cyl->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        // OPTIMIZATION #19: Use pooled material instance
        auto *Mat = GetOrCreateMaterial(Color);
        if (Mat)
        {
            Mat->SetScalarParameterValue(TEXT("EmissiveIntensity"), 2.0f);
            Cyl->SetMaterial(0, Mat);
        }
        Cyl->AttachToComponent(Par, FAttachmentTransformRules::KeepWorldTransform);
        Cyl->RegisterComponent();
        Out.Add(Cyl);
        AllBondMeshes.Add(Cyl);
    };

    FVector HalfDir = V * 0.5f;
    FVector FirstMid = S + V * 0.25f;
    FVector SecondMid = S + V * 0.75f;
    float HalfScale = Scale * 0.5f;

    if (Ord <= 1)
    {
        MakeCyl(FirstMid, HalfScale, Color1);
        MakeCyl(SecondMid, HalfScale, Color2);
        return;
    }

    FVector Perp = FVector::CrossProduct(V.GetSafeNormal(), FVector::UpVector);
    if (Perp.SizeSquared() < KINDA_SMALL_NUMBER)
        Perp = FVector::CrossProduct(V.GetSafeNormal(), FVector::RightVector);
    Perp.Normalize();
    FVector Off = Perp * PDB::BOND_OFFSET;

    if (Ord == 2)
    {
        MakeCyl(FirstMid + Off, HalfScale, Color1);
        MakeCyl(SecondMid + Off, HalfScale, Color2);
        MakeCyl(FirstMid - Off, HalfScale, Color1);
        MakeCyl(SecondMid - Off, HalfScale, Color2);
    }
    else if (Ord == 3)
    {
        MakeCyl(FirstMid, HalfScale, Color1);
        MakeCyl(SecondMid, HalfScale, Color2);
        MakeCyl(FirstMid + Off, HalfScale, Color1);
        MakeCyl(SecondMid + Off, HalfScale, Color2);
        MakeCyl(FirstMid - Off, HalfScale, Color1);
        MakeCyl(SecondMid - Off, HalfScale, Color2);
    }
    else
    {
        MakeCyl(FirstMid, HalfScale, Color1);
        MakeCyl(SecondMid, HalfScale, Color2);
    }
}

FLinearColor APDBViewer::GetElementColor(const FString &E) const
{
    // OPTIMIZATION #7: Static result cache to avoid repeated ToUpper() calls
    static TMap<FString, FLinearColor> ResultCache;

    // Check cache first
    const FLinearColor* Cached = ResultCache.Find(E);
    if (Cached)
    {
        return *Cached;
    }

    // Cache miss - compute and store result
    static const TMap<FString, FLinearColor> Colors = {
        {TEXT("C"), FLinearColor(0.1f, 0.1f, 0.1f)}, {TEXT("O"), FLinearColor::Red}, {TEXT("H"), FLinearColor::White}, {TEXT("D"), FLinearColor::White}, {TEXT("N"), FLinearColor::Blue}, {TEXT("S"), FLinearColor::Yellow}, {TEXT("CL"), FLinearColor(0, 1, 0)}, {TEXT("P"), FLinearColor(1, 0.5f, 0)}, {TEXT("F"), FLinearColor(0, 1, 0)}, {TEXT("BR"), FLinearColor(0.6f, 0.2f, 0.2f)}, {TEXT("I"), FLinearColor(0.4f, 0, 0.8f)}, {TEXT("FE"), FLinearColor(0.8f, 0.4f, 0)}, {TEXT("MG"), FLinearColor(0, 0.8f, 0)}, {TEXT("ZN"), FLinearColor(0.5f, 0.5f, 0.5f)}, {TEXT("CA"), FLinearColor(0.2f, 0.6f, 1)}, {TEXT("NA"), FLinearColor(0, 0, 1)}, {TEXT("K"), FLinearColor(0.5f, 0, 1)}, {TEXT("CU"), FLinearColor(1, 0.5f, 0)}, {TEXT("B"), FLinearColor(1, 0.7f, 0.7f)}};
    const auto *C = Colors.Find(E.ToUpper());
    FLinearColor Result = C ? *C : FLinearColor::Gray;

    // Store in cache for future lookups
    ResultCache.Add(E, Result);
    return Result;
}

void APDBViewer::ClearOverlapMarkers()
{
    for (auto *M : OverlapMarkers)
    {
        if (M && IsValid(M))
            M->DestroyComponent();
    }
    OverlapMarkers.Empty();
}
