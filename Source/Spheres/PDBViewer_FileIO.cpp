// PDBViewer_FileIO.cpp - File save/load dialogs and operations

#include "PDBViewer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Misc/MessageDialog.h"
#if WITH_EDITOR
#include "Interfaces/IMainFrameModule.h"
#endif

void APDBViewer::SaveStructureToFile(const FString &Path)
{
    if (CurrentPDBContent.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("No data to save"));
        return;
    }

    if (FFileHelper::SaveStringToFile(CurrentPDBContent, *Path))
    {
        UE_LOG(LogTemp, Log, TEXT("Saved: %s"), *Path);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed: %s"), *Path);
    }
}

void APDBViewer::LoadStructureFromFile(const FString &Path)
{
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *Path))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load: %s"), *Path);
        return;
    }

    FString Ext = FPaths::GetExtension(Path).ToLower();

    // Only clear structure ID and set content for PDB/CIF
    if (Ext == TEXT("pdb") || Ext == TEXT("cif"))
    {
        // Don't call ClearCurrentStructure here - let individual parsers handle it
        CurrentStructureID = FPaths::GetBaseFilename(Path);
    }

    CurrentPDBContent = Content;

    if (Ext == TEXT("pdb"))
        ParsePDB(CurrentPDBContent);
    else if (Ext == TEXT("cif"))
        ParseMMCIF(CurrentPDBContent);
    else if (Ext == TEXT("sdf") || Ext == TEXT("mol"))
    {
        // Show chain selection for SDF files
        FString SelectedChain;
        if (ShowChainSelectionDialog(SelectedChain))
        {
            ParseSDF(CurrentPDBContent, SelectedChain);
        }
    }
    else
        UE_LOG(LogTemp, Error, TEXT("Unsupported format: %s"), *Ext);
}

bool APDBViewer::ShowFileDialog(bool bSave, FString &Out)
{
    auto *DP = FDesktopPlatformModule::Get();
    if (!DP)
        return false;

    void *Handle = nullptr;
#if WITH_EDITOR
    if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
        if (auto P = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame").GetParentWindow())
            if (P.IsValid() && P->GetNativeWindow().IsValid())
                Handle = P->GetNativeWindow()->GetOSWindowHandle();
#endif

    TArray<FString> Files;
    FString FileTypes = TEXT("All Supported (*.pdb;*.cif;*.sdf;*.mol)|*.pdb;*.cif;*.sdf;*.mol|")
        TEXT("PDB Files (*.pdb)|*.pdb|")
            TEXT("mmCIF Files (*.cif)|*.cif|")
                TEXT("SDF Files (*.sdf)|*.sdf|")
                    TEXT("MOL Files (*.mol)|*.mol");

    FString DefaultFile = bSave ? TEXT("structure.pdb") : TEXT("");
    FString DialogTitle = bSave ? TEXT("Save Structure") : TEXT("Load Structure");

    bool bOK = bSave ? DP->SaveFileDialog(Handle, DialogTitle, FPaths::ProjectSavedDir(), DefaultFile,
                                          FileTypes, EFileDialogFlags::None, Files)
                     : DP->OpenFileDialog(Handle, DialogTitle, FPaths::ProjectSavedDir(), TEXT(""),
                                          FileTypes, EFileDialogFlags::None, Files);

    if (bOK && Files.Num() > 0)
    {
        Out = MoveTemp(Files[0]);
        return true;
    }
    return false;
}

void APDBViewer::OpenSaveDialog()
{
    FString P;
    if (ShowFileDialog(true, P))
        SaveStructureToFile(P);
}

void APDBViewer::OpenLoadDialog()
{
    FString P;
    if (ShowFileDialog(false, P))
        LoadStructureFromFile(P);
}

bool APDBViewer::ShowSDFFileDialog(FString& OutFilePath)
{
    auto* DP = FDesktopPlatformModule::Get();
    if (!DP)
        return false;

    void* Handle = nullptr;
#if WITH_EDITOR
    if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
        if (auto P = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame").GetParentWindow())
            if (P.IsValid() && P->GetNativeWindow().IsValid())
                Handle = P->GetNativeWindow()->GetOSWindowHandle();
#endif

    TArray<FString> Files;
    FString FileTypes = TEXT("SDF/MOL Files (*.sdf;*.mol)|*.sdf;*.mol");

    bool bOK = DP->OpenFileDialog(Handle, TEXT("Load SDF File"), FPaths::ProjectSavedDir(), TEXT(""),
                                   FileTypes, EFileDialogFlags::None, Files);

    if (bOK && Files.Num() > 0)
    {
        OutFilePath = MoveTemp(Files[0]);
        return true;
    }
    return false;
}

void APDBViewer::LoadPDBFromString(const FString& PDBContent)
{
    CurrentPDBContent = PDBContent;
    ParsePDB(PDBContent);
}

void APDBViewer::LoadPDB(const FString& PDB_ID)
{
    FetchAndDisplayStructure(PDB_ID);
}

void APDBViewer::LoadSDFFromString(const FString& SDFContent)
{
    CurrentPDBContent = SDFContent;
    ParseSDF(SDFContent);
}

bool APDBViewer::ShowChainSelectionDialog(FString& OutChainID)
{
    // Get available chains
    TArray<FString> AvailableChains = ChainIDs.Array();

    if (AvailableChains.Num() == 0)
    {
        // No chains available, use default
        OutChainID = TEXT("SDF");
        return true;
    }

    // Sort chains for consistent display
    AvailableChains.Sort();

    // Build message string with available chains
    FString Message = TEXT("Select chain to load SDF into:\n\n");
    for (int32 i = 0; i < AvailableChains.Num(); ++i)
    {
        Message += FString::Printf(TEXT("[%d] Chain %s\n"), i + 1, *AvailableChains[i]);
    }
    Message += FString::Printf(TEXT("[%d] Create new chain 'SDF'\n\n"), AvailableChains.Num() + 1);
    Message += TEXT("Enter number (1-") + FString::FromInt(AvailableChains.Num() + 1) + TEXT("):");

    // For now, use a simple input dialog (Blueprint/UI should handle this better)
    // This is a placeholder - in production, this should be handled by the UI
    // For testing, default to first chain
    UE_LOG(LogTemp, Warning, TEXT("Chain selection needed. Available chains: %s"), *FString::Join(AvailableChains, TEXT(", ")));

#if WITH_EDITOR
    // In editor, show a simple prompt
    // Note: This is Windows-specific and should ideally be replaced with Slate UI
    void* Handle = nullptr;
    if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
    {
        if (auto P = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame").GetParentWindow())
        {
            if (P.IsValid() && P->GetNativeWindow().IsValid())
            {
                Handle = P->GetNativeWindow()->GetOSWindowHandle();
            }
        }
    }

    // Build options for dialog
    FString Options = TEXT("Available Chains:\n");
    for (const FString& Chain : AvailableChains)
    {
        Options += TEXT("  ") + Chain + TEXT("\n");
    }
    Options += TEXT("\nDefault: ") + AvailableChains[0];

    FText Title = FText::FromString(TEXT("Select Chain"));
    FText MessageText = FText::FromString(Options);

    // Use a simple message box for now
    EAppReturnType::Type Result = FMessageDialog::Open(
        EAppMsgType::OkCancel,
        FText::FromString(Options + TEXT("\n\nLoad into chain '") + AvailableChains[0] + TEXT("'?"))
    );

    if (Result == EAppReturnType::Ok)
    {
        OutChainID = AvailableChains[0];
        return true;
    }
    return false;
#else
    // In packaged game, default to first chain
    OutChainID = AvailableChains[0];
    return true;
#endif
}
