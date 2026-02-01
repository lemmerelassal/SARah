#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Http.h"
#include "MMGBSA.h"
#include "Components/PointLightComponent.h"
#include "PDBViewer.generated.h"

// Interaction type enumeration
UENUM(BlueprintType)
enum class EInteractionType : uint8
{
    HydrogenBond UMETA(DisplayName = "Hydrogen Bond"),
    SaltBridge UMETA(DisplayName = "Salt Bridge"),
    PiStacking UMETA(DisplayName = "Pi-Stacking"),
    Hydrophobic UMETA(DisplayName = "Hydrophobic"),
    VanDerWaals UMETA(DisplayName = "Van der Waals"),
    Cation_Pi UMETA(DisplayName = "Cation-Pi")
};

// NEW: Tree node type enumeration for hierarchical structure
UENUM(BlueprintType)
enum class EPDBNodeType : uint8
{
    Chain UMETA(DisplayName = "Chain"),
    ResiduesCategory UMETA(DisplayName = "Residues Category"),
    HeteroatomsCategory UMETA(DisplayName = "Heteroatoms Category"),
    WaterCategory UMETA(DisplayName = "Water Category"),
    LigandsCategory UMETA(DisplayName = "Ligands Category"),
    Residue UMETA(DisplayName = "Residue"),
    Water UMETA(DisplayName = "Water"),
    Ligand UMETA(DisplayName = "Ligand")
};

// Structure to store molecular interactions
USTRUCT(BlueprintType)
struct FMolecularInteraction
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadOnly)
    EInteractionType Type;
    
    UPROPERTY(BlueprintReadOnly)
    FString Residue1;  // e.g., "ALA_42_A"
    
    UPROPERTY(BlueprintReadOnly)
    FString Residue2;  // e.g., "LYS_108_A" or ligand key
    
    UPROPERTY(BlueprintReadOnly)
    FString Atom1;     // Atom name in residue1
    
    UPROPERTY(BlueprintReadOnly)
    FString Atom2;     // Atom name in residue2
    
    UPROPERTY(BlueprintReadOnly)
    FVector Position1; // 3D position of atom1
    
    UPROPERTY(BlueprintReadOnly)
    FVector Position2; // 3D position of atom2
    
    UPROPERTY(BlueprintReadOnly)
    float Distance;    // Distance in Angstroms
    
    UPROPERTY(BlueprintReadOnly)
    float Angle;       // For H-bonds: donor-H-acceptor angle
    
    UPROPERTY(BlueprintReadOnly)
    float Energy;      // Estimated interaction energy (kcal/mol)
    
    UPROPERTY(BlueprintReadOnly)
    bool bIsProteinLigand; // True if interaction is between protein and ligand


        // Network analysis fields
    UPROPERTY(BlueprintReadWrite, Category = "Interaction")
    bool bIsPartOfNetwork = false;
    
    UPROPERTY(BlueprintReadWrite, Category = "Interaction")
    int32 NetworkID = -1;

    
    
    FMolecularInteraction()
        : Type(EInteractionType::VanDerWaals)
        , Distance(0.0f)
        , Angle(0.0f)
        , Energy(0.0f)
        , bIsProteinLigand(false)
    {}
};

USTRUCT()
struct FResidueMetadata
{
    GENERATED_BODY()
    FString ResidueName, ResidueSeq, Chain, RecordType;
};

USTRUCT()
struct FLigandMetadata {
    GENERATED_BODY()
    FString LigandName;
};

USTRUCT()
struct FResidueInfo
{
    GENERATED_BODY()
    FString RecordType, Chain, ResidueName, ResidueSeq;
    TArray<UStaticMeshComponent*> AtomMeshes, BondMeshes;
    // Store UNSCALED raw atom positions and element symbols
    TArray<FVector> AtomPositions;
    TArray<FString> AtomElements;
    TArray<FString> AtomNames;      // Full atom names like "CA", "CB", "N", etc.
    TArray<TPair<int32, int32>> BondPairs;
    TArray<int32> BondOrders;
    bool bIsVisible = true;

    // OPTIMIZED: Cached data
    int32 CachedSequenceNumber = 0;  // Parsed from ResidueSeq to avoid repeated Atoi() calls
    FVector CachedCenterOfMass;
    bool bCenterOfMassCached = false;
    FVector CachedAromaticCenter;
    FVector CachedAromaticNormal;
    bool bAromaticCenterCached = false;
};

USTRUCT()
struct FLigandInfo
{
    GENERATED_BODY()
    FString LigandName;
    TArray<UStaticMeshComponent*> AtomMeshes, BondMeshes;
    TArray<FString> AtomElements; // Element symbol per atom (aligned with AtomMeshes)
    TArray<FVector> AtomPositions; // Store UNSCALED positions for accurate calculations
    TArray<FString> AtomNames;      // Full atom names
    TArray<TPair<int32, int32>> BondPairs; // Store bond connectivity
    TArray<int32> BondOrders; // Bond order for each bond (aligned with BondPairs)
    bool bIsVisible = false;
    
    // NEW: Light components for each atom
    TArray<UPointLightComponent*> AtomLights;
    
    // NEW: Flag to distinguish water from ligands
    bool bIsWater = false;
    
    // OPTIMIZED: Cached data
    FVector CachedCenterOfMass;
    bool bCenterOfMassCached = false;
    FVector CachedAromaticCenter;
    FVector CachedAromaticNormal;
    bool bAromaticCenterCached = false;
};

// TreeView Node Object (must be UObject for TreeView)
UCLASS(BlueprintType)
class SPHERES_API UPDBTreeNode : public UObject
{
    GENERATED_BODY()
    
public:
    TMap<FString, FLigandInfo*> LigandMap;
    TMap<FString, FResidueInfo*> ResidueMap;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    FString DisplayName;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    FString NodeKey;
    
    // NEW: Node type for hierarchical tree
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    EPDBNodeType NodeType = EPDBNodeType::Residue;
    
    // Kept for backward compatibility
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    bool bIsChain = false;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    bool bIsVisible = true;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    FString ChainID;
    
    // NEW: Helper to check if node can have children
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    bool bCanExpand = false;
    
    void Initialize(const FString& InDisplayName, const FString& InNodeKey, bool bInIsChain, const FString& InChainID = TEXT(""))
    {
        DisplayName = InDisplayName;
        NodeKey = InNodeKey;
        bIsChain = bInIsChain;
        bIsVisible = true;
        ChainID = InChainID;
        
        // Set node type based on legacy flag
        if (bInIsChain)
        {
            NodeType = EPDBNodeType::Chain;
            bCanExpand = true;
        }
    }
    
    // NEW: Extended initializer with node type
    void InitializeWithType(const FString& InDisplayName, const FString& InNodeKey, EPDBNodeType InNodeType, const FString& InChainID = TEXT(""))
    {
        DisplayName = InDisplayName;
        NodeKey = InNodeKey;
        NodeType = InNodeType;
        ChainID = InChainID;
        bIsVisible = true;
        
        // Set legacy flag and expansion capability
        bIsChain = (InNodeType == EPDBNodeType::Chain);
        bCanExpand = (InNodeType == EPDBNodeType::Chain ||
                      InNodeType == EPDBNodeType::ResiduesCategory ||
                      InNodeType == EPDBNodeType::HeteroatomsCategory ||
                      InNodeType == EPDBNodeType::WaterCategory ||
                      InNodeType == EPDBNodeType::LigandsCategory);
    }
};

// ListView Node Object for SDF Molecules
UCLASS(BlueprintType)
class SPHERES_API UPDBMoleculeNode : public UObject
{
    GENERATED_BODY()
    
public:
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    FString MoleculeName;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    FString MoleculeKey;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    bool bIsVisible = true;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    int32 AtomCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "PDB Viewer")
    int32 BondCount = 0;
    
    void Initialize(const FString& InName, const FString& InKey, bool bInIsVisible, int32 InAtomCount, int32 InBondCount)
    {
        MoleculeName = InName;
        MoleculeKey = InKey;
        bIsVisible = bInIsVisible;
        AtomCount = InAtomCount;
        BondCount = InBondCount;
    }
};

UCLASS()
class SPHERES_API APDBViewer : public AActor
{
    GENERATED_BODY()

public:
    APDBViewer();

    // OPTIMIZATION #17: Enable ticking for LOD system
    virtual void Tick(float DeltaTime) override;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnResiduesLoaded);
    UPROPERTY(BlueprintAssignable, Category = "PDB Viewer") FOnResiduesLoaded OnResiduesLoaded;
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLigandsLoaded);
    UPROPERTY(BlueprintAssignable, Category = "PDB Viewer") FOnLigandsLoaded OnLigandsLoaded;
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInteractionsCalculated);
    UPROPERTY(BlueprintAssignable, Category = "PDB Viewer") FOnInteractionsCalculated OnInteractionsCalculated;
    
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void SaveStructureToFile(const FString& FilePath);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void LoadStructureFromFile(const FString& FilePath);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ClearCurrentStructure();
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void LoadPDBFromString(const FString& PDBContent);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void LoadPDB(const FString& PDB_ID);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ToggleResidueVisibility(const FString& ResidueKey);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ToggleLigandVisibility(const FString& LigandKey);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ToggleChainVisibility(const FString& ChainID);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void PopulateTreeView(class UTreeView* TreeView);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<UObject*> GetChildrenForNode(UPDBTreeNode* Node);
    
    // Internal callback for TreeView - UE 5.6 compatible signature with reference parameter
    void GetChildrenForNodeInternal(UObject* Item, TArray<UObject*>& OutChildren);
    
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<UPDBTreeNode*> GetChainNodes();
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<UPDBTreeNode*> GetResidueNodesForChain(const FString& ChainID);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ToggleNodeVisibility(UPDBTreeNode* Node);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void LoadSDFFromString(const FString& SDFContent);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void OpenSaveDialog();
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void OpenLoadDialog();
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<UPDBMoleculeNode*> GetMoleculeNodes();
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void PopulateMoleculeListView(class UListView* ListView);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ToggleMoleculeVisibility(const FString& MoleculeKey);
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ToggleMoleculeNodeVisibility(UPDBMoleculeNode* Node);
    
    // NEW: Get water nodes for a chain
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<UPDBTreeNode*> GetWaterNodesForChain(const FString& ChainID);
    
    // NEW: Get ligand nodes for a chain
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<UPDBTreeNode*> GetLigandNodesForChain(const FString& ChainID);
    
    // NEW: Toggle visibility for category nodes
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") void ToggleCategoryVisibility(UPDBTreeNode* Node);
    
    // Helper to check if a ligand key represents water
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") bool IsWaterKey(const FString& LigandKey) const;
    
    // Hydrogen generation functions
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer")
    void AddExplicitHydrogens();
    
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer")
    void RemoveExplicitHydrogens();
    
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer")
    void ToggleHydrogens();
    
    // ===== NEW: LIGAND ATOM LIGHTING FUNCTIONS =====
    
    // Enable/disable ligand atom lights
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Lighting")
    void SetLigandAtomLightsEnabled(bool bEnabled);
    
    // Toggle ligand atom lights on/off
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Lighting")
    void ToggleLigandAtomLights();
    
    // Set light intensity for all ligand atom lights
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Lighting")
    void SetLigandAtomLightIntensity(float Intensity);
    
    // Set light radius for all ligand atom lights
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Lighting")
    void SetLigandAtomLightRadius(float Radius);
    
    // Get whether ligand atom lights are enabled
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Lighting")
    bool GetLigandAtomLightsEnabled() const { return bLigandAtomLightsEnabled; }
    
    // Get total number of ligand atom lights
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Lighting")
    int32 GetLigandAtomLightCount() const;
    
    // Configure lighting parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Lighting")
    bool bLigandAtomLightsEnabled = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Lighting")
    float LigandAtomLightIntensity = 5000.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Lighting")
    float LigandAtomLightRadius = 500.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Lighting")
    bool bUseDynamicAtomColors = true; // Use atom element colors for lights
    
    // ===== INTERACTION DETECTION FUNCTIONS =====
    
    // Calculate all molecular interactions
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Interactions")
    void CalculateAllInteractions(bool bProteinProtein = true, bool bProteinLigand = true);
    
    // Toggle visibility of specific interaction types
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Interactions")
    void ToggleInteractionType(EInteractionType Type, bool bVisible);
    
    // Show/hide all interactions
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Interactions")
    void ShowAllInteractions(bool bVisible);
    
    // Get list of interactions by type
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Interactions")
    TArray<FMolecularInteraction> GetInteractionsByType(EInteractionType Type) const;
    
    // Get all calculated interactions
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Interactions")
    TArray<FMolecularInteraction> GetAllInteractions() const { return DetectedInteractions; }
    
    // Get interactions involving a specific residue or ligand
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Interactions")
    TArray<FMolecularInteraction> GetInteractionsForResidue(const FString& ResidueKey) const;
    
    // Clear all interactions and their visual representations
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Interactions")
    void ClearAllInteractions();
    
    // Configure interaction detection parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Interactions")
    float HBondMaxDistance = 3.5f; // Angstroms
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Interactions")
    float HBondMinAngle = 120.0f; // Degrees
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Interactions")
    float SaltBridgeMaxDistance = 4.0f; // Angstroms
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Interactions")
    float PiStackingMaxDistance = 5.5f; // Angstroms
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Interactions")
    float HydrophobicMaxDistance = 5.0f; // Angstroms
    
    // Debug functions
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Debug")
    void DebugPrintLigandInfo();

    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Debug")
    int32 GetHydrogenCount() const;
    
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Debug")
    void DebugPrintInteractions();
    
    // Legacy functions (kept for backwards compatibility)
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<FString> GetResidueList() const;
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") TArray<FString> GetLigandList() const;
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") FString GetResidueDisplayName(const FString& ResidueKey) const;
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer") FString GetLigandDisplayName(const FString& LigandKey) const;

    // Get the currently visible ligand (if any)
    FLigandInfo* GetVisibleLigandInfo() const;
    TMap<FString, FLigandInfo*> LigandMap;
    TMap<FString, FResidueInfo*> ResidueMap;

    // OPTIMIZATION: Cache ligands organized by chain for faster lookups
    TMap<FString, TArray<FLigandInfo*>> LigandsByChain;
    bool bLigandChainCacheDirty = true;  // OPTIMIZATION #9: Lazy rebuild flag

    // OPTIMIZATION #19: Material instance pool for color reuse
    TMap<FLinearColor, UMaterialInstanceDynamic*> MaterialPool;

    

/*     // Debug: highlight severe overlaps found by MMGBSA
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Debug")
    void HighlightOverlapAtoms(const TArray<FMMOverlapInfo>& Overlaps); */

    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|Debug")
    void ClearOverlapMarkers();

    // ===== OPTIMIZATION #17: LOD SYSTEM =====

    // Enable/disable LOD system
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|LOD")
    bool bEnableLODSystem = true;

    // Distance thresholds for LOD levels (in Unreal units)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|LOD")
    float LOD0Distance = 1000.0f;  // Full detail

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|LOD")
    float LOD1Distance = 2500.0f;  // Medium detail

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|LOD")
    float LOD2Distance = 5000.0f;  // Low detail

    // LOD3 is anything beyond LOD2Distance (very low detail)

    // Get current LOD level based on camera distance
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|LOD")
    int32 GetCurrentLODLevel() const { return CurrentLODLevel; }

    // Force a specific LOD level (set to -1 for automatic)
    UFUNCTION(BlueprintCallable, Category = "PDB Viewer|LOD")
    void SetForcedLODLevel(int32 LODLevel);

protected:
    UFUNCTION()
    void OnLigandsLoadedHandler();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer")
    bool bAutoGenerateHydrogens = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PDB Viewer|Interactions")
    bool bAutoCalculateInteractions = false;

    UPROPERTY() TArray<UStaticMeshComponent*> OverlapMarkers;

    virtual void BeginPlay() override;
    
    UPROPERTY() UStaticMesh* SphereMeshAsset;
    UPROPERTY() UStaticMesh* CylinderMeshAsset;
    UPROPERTY() UMaterial* SphereMaterialAsset;
    UPROPERTY() TArray<UStaticMeshComponent*> AllAtomMeshes;
    UPROPERTY() TArray<UStaticMeshComponent*> AllBondMeshes;
    
    FString CurrentPDBContent, CurrentStructureID;

    TSet<FString> ChainIDs; // Track all chains in the structure
    bool bHydrogensVisible = true;
    
    // Cache for tree nodes - prevents recreating objects on every GetChildren call
    UPROPERTY()
    TMap<FString, UPDBTreeNode*> TreeNodeCache;
    
    // ===== NEW: INTERACTION DATA =====
    UPROPERTY()
    TArray<FMolecularInteraction> DetectedInteractions;
    
    UPROPERTY()
    TArray<UStaticMeshComponent*> InteractionMeshes;
    
    TMap<EInteractionType, bool> InteractionVisibility;
    
    void FetchAndDisplayStructure(const FString& PDB_ID);
    void FetchFileAsync(const FString& URL, TFunction<void(bool, const FString&)> Callback);
    void ParsePDB(const FString& FileContent);
    void ParseMMCIF(const FString& FileContent);
    void ParseSDF(const FString& FileContent);
    void CreateResiduesFromAtomData(const TMap<FString, TMap<FString, FVector>>& ResidueAtoms, const TMap<FString, FResidueMetadata>& Metadata);
    void DrawProteinBondsAndConnectivity(const TMap<FString, FVector>& AtomPositions, FResidueInfo* ResInfo);
    void GenerateHydrogensForResidue(FResidueInfo* ResInfo);
    void FetchLigandBondsForHETATM(const FString& Key, const FString& Name, const TMap<FString, FVector>& Pos, FLigandInfo* LigInfo);
    void ParseLigandCIFForLigand(const FString& FileContent, const TMap<FString, FVector>& AtomPositions, FLigandInfo* LigInfo);
    
    // New functions for mmCIF bond parsing
    void FetchStructureBondsFromCIF(const FString& StructureID);
    void ParseStructureBondsFromCIF(const FString& Content);
    void ApplyBondsToResidues(const TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>>& ComponentBonds);
    
    FString NormalizeAtomID(const FString& In) const;
    int32 ParseBondOrder(const FString& OrderStr) const;

    // Optimization: Helper to parse ligand key format "NAME_SEQ_CHAIN"
    static FString GetChainFromLigandKey(const FString& Key);
    static void ParseLigandKey(const FString& Key, FString& OutName, FString& OutSeq, FString& OutChain);

    // Optimization: Rebuild ligand chain cache
    void RebuildLigandChainCache();

    // OPTIMIZATION #15: Cached string trimming helper
    static const FString& GetTrimmedString(const FString& Input);
    static void ClearTrimCache();

    // OPTIMIZATION #19: Material instance pooling helper
    UMaterialInstanceDynamic* GetOrCreateMaterial(const FLinearColor& Color);

    // ===== CODE REDUCTION HELPERS =====

    // Helper: Destroy all mesh components in array
    static void DestroyMeshComponents(TArray<UStaticMeshComponent*>& Meshes);

    // Helper: Safe pointer dereference
    template<typename T>
    static inline T* SafeDereference(T** PtrPtr) { return (PtrPtr && *PtrPtr) ? *PtrPtr : nullptr; }

    // Helper: Check if mesh is valid
    template<typename T>
    static inline bool IsValidMesh(T* Mesh) { return Mesh && IsValid(Mesh); }

    // Helper: Find atom index by name
    static int32 FindAtomIndexByName(const TArray<FString>& Names, const FString& Name);

    // ===== LAMBDA-BASED CODE REDUCTION HELPERS =====

    // Helper: Update mesh visibility (replaces dual atom/bond loops)
    static void SetMeshArrayVisibility(TArray<UStaticMeshComponent*>& Meshes, bool bVisible, bool bPropagateToChildren = false);

    // Helper: Update hydrogen visibility for any structure type (template)
    template<typename TInfo>
    void UpdateStructureHydrogenVisibility(TInfo* Info, bool bVisible);

    // Helper: Apply action to all valid lights in ligand map
    void ForEachValidLigandLight(TFunction<void(UPointLightComponent*)> Action);

    // Helper: Get filtered tree nodes for chain
    TArray<UPDBTreeNode*> GetFilteredNodesForChain(const FString& ChainID, TFunction<bool(const FString&)> KeyFilter, const FString& CategoryPrefix);

    // Helper: Get ligand key sorting comparator (format: NAME_SEQ_CHAIN)
    static TFunction<bool(const FString&, const FString&)> GetLigandKeyComparator(bool bSortByName = false);

    // Helper: Update all hydrogen visibility (both ligands and residues)
    void UpdateAllHydrogenVisibility(bool bVisible);

    // Helper: Generic info map cleanup
    template<typename TMap, typename TPreDelete>
    void ClearInfoMap(TMap& InfoMap, TPreDelete PreDeleteFunc);

    // Helper: Count bond orders by type (returns array: [Single, Double, Triple, Other])
    static TArray<int32> CountBondOrdersByType(const TArray<int32>& BondOrders);

    // Helper: Extract keys from array of TPairs
    template<typename K, typename V>
    static TArray<K> ExtractKeysFromPairs(const TArray<TPair<K, V>>& Pairs);

    void DrawSphere(float X, float Y, float Z, const FLinearColor& Color, USceneComponent* Parent, TArray<UStaticMeshComponent*>& OutArray);
    void DrawSphere(const FVector& Position, const FLinearColor& Color, USceneComponent* Parent, TArray<UStaticMeshComponent*>& OutArray);
    void DrawBond(const FVector& Start, const FVector& End, int32 Order, const FString& Element1, const FString& Element2, USceneComponent* Parent, TArray<UStaticMeshComponent*>& OutArray);
    FLinearColor GetElementColor(const FString& Element) const;
    void ClearResidueMap();
    void ClearLigandMap();
    bool ShowFileDialog(bool bSave, FString& OutFilePath);
    
    // Hydrogen generation helpers
    int32 AddHydrogensToLigand(FLigandInfo* LigInfo);

    // OPTIMIZATION: Consolidated hydrogen visibility helpers
    void UpdateLigandHydrogenVisibility(FLigandInfo* LigInfo, bool bVisible);
    void UpdateResidueHydrogenVisibility(FResidueInfo* ResInfo, bool bVisible);

    // OPTIMIZATION #16: Batched visibility update helper
    void SetMeshVisibilityBatched(TArray<UStaticMeshComponent*>& Meshes, bool bVisible);
    
    // ===== NEW: LIGAND LIGHTING HELPERS =====
    
    // Create lights for all atoms in a ligand
    void CreateLigandAtomLights(FLigandInfo* LigInfo);
    
    // Update light visibility and properties for a ligand
    void UpdateLigandAtomLights(FLigandInfo* LigInfo);
    
    // Clear all lights for a ligand
    void ClearLigandAtomLights(FLigandInfo* LigInfo);
    
    // Get light color based on element
    FLinearColor GetLightColorForElement(const FString& Element) const;
    
    // ===== INTERACTION DETECTION HELPERS =====
    
    // Detect hydrogen bonds
    void DetectHydrogenBonds(bool bProteinProtein, bool bProteinLigand);
    
    // Detect salt bridges
    void DetectSaltBridges(bool bProteinProtein, bool bProteinLigand);
    
    // Detect pi-stacking interactions
    void DetectPiStacking(bool bProteinProtein, bool bProteinLigand);
    
    // Detect hydrophobic interactions
    void DetectHydrophobicInteractions(bool bProteinProtein, bool bProteinLigand);


    void AnalyzeHBondNetworks();
    
    // Helper: Check if atom can be H-bond donor
    bool IsHBondDonor(const FString& Element, const FString& AtomName) const;
    
    // Helper: Check if atom can be H-bond acceptor
    bool IsHBondAcceptor(const FString& Element, const FString& AtomName) const;
    
    // Helper: Check if residue is charged positive
    bool IsPositivelyCharged(const FString& ResidueName) const;
    
    // Helper: Check if residue is charged negative
    bool IsNegativelyCharged(const FString& ResidueName) const;
    
    // Helper: Check if residue is aromatic
    bool IsAromatic(const FString& ResidueName) const;
    
    // Helper: Check if residue is hydrophobic
    bool IsHydrophobic(const FString& ResidueName) const;
    
    // Helper: Get atom's formal charge
    int32 GetAtomCharge(const FString& Element, const FString& AtomName, const FString& ResidueName) const;
    
    // Helper: Calculate angle between three points (degrees)
    float CalculateAngle(const FVector& A, const FVector& B, const FVector& C) const;
    
    // Helper: Get residue center of mass (OPTIMIZED: Non-const for caching)
    FVector GetResidueCenterOfMass(FResidueInfo* ResInfo);
    FVector GetLigandCenterOfMass(FLigandInfo* LigInfo);
    
    // Helper: Get aromatic ring center for residue (OPTIMIZED: Non-const for caching)
    bool GetAromaticRingCenter(FResidueInfo* ResInfo, FVector& OutCenter, FVector& OutNormal);
    bool GetAromaticRingCenter(FLigandInfo* LigInfo, FVector& OutCenter, FVector& OutNormal);
    
    // Visualize an interaction
    void DrawInteraction(const FMolecularInteraction& Interaction);

    // Get color for interaction type
    FLinearColor GetInteractionColor(EInteractionType Type) const;

    // ===== OPTIMIZATION #17: LOD SYSTEM HELPERS =====

    // Current LOD level (0 = highest detail, 3 = lowest)
    int32 CurrentLODLevel = 0;

    // Forced LOD level (-1 = automatic, 0-3 = forced level)
    int32 ForcedLODLevel = -1;

    // Cached structure center for LOD distance calculations
    FVector StructureCenter;
    bool bStructureCenterCached = false;

    // LOD check interval (seconds) - don't check every frame
    float LODCheckInterval = 0.5f;
    float TimeSinceLastLODCheck = 0.0f;

    // Calculate structure center of mass
    FVector CalculateStructureCenter();

    // Update LOD based on camera distance
    void UpdateLODLevel();

    // Apply LOD level to all meshes
    void ApplyLODLevel(int32 NewLODLevel);

    // Check if atom should be visible at given LOD level
    bool ShouldAtomBeVisibleAtLOD(const FString& AtomName, int32 LODLevel) const;

    // Check if bond should be visible at given LOD level
    bool ShouldBondBeVisibleAtLOD(const FString& Atom1Name, const FString& Atom2Name, int32 LODLevel) const;
};
