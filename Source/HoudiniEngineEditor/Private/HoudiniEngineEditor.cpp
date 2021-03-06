/*
* Copyright (c) <2017> Side Effects Software Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*/

#include "HoudiniApi.h"
#include "HoudiniEngineEditor.h"
#include "HoudiniEngineEditorPrivatePCH.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniAssetThumbnailRenderer.h"
#include "HoudiniAsset.h"
#include "HoudiniEngine.h"
#include "HoudiniAssetActor.h"
#include "HoudiniAssetComponent.h"
#include "HoudiniHandleComponentVisualizer.h"
#include "HoudiniHandleComponent.h"
#include "HoudiniSplineComponentVisualizer.h"
#include "HoudiniSplineComponent.h"
#include "HoudiniAssetComponentDetails.h"
#include "HoudiniRuntimeSettingsDetails.h"
#include "HoudiniAssetTypeActions.h"
#include "HoudiniAssetBroker.h"
#include "HoudiniAssetActorFactory.h"
#include "HoudiniShelfEdMode.h"
#include "HoudiniEngineBakeUtils.h"

#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/ObjectLibrary.h"
#include "EditorDirectories.h"
#include "Styling/SlateStyleRegistry.h"
#include "SHoudiniToolPalette.h"
#include "IPlacementModeModule.h"

#include "AssetRegistryModule.h"
#include "Engine/Selection.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#define LOCTEXT_NAMESPACE HOUDINI_LOCTEXT_NAMESPACE 

const FName
FHoudiniEngineEditor::HoudiniEngineEditorAppIdentifier = FName( TEXT( "HoudiniEngineEditorApp" ) );

IMPLEMENT_MODULE( FHoudiniEngineEditor, HoudiniEngineEditor );
DEFINE_LOG_CATEGORY( LogHoudiniEngineEditor );

FHoudiniEngineEditor *
FHoudiniEngineEditor::HoudiniEngineEditorInstance = nullptr;

FHoudiniEngineEditor &
FHoudiniEngineEditor::Get()
{
    return *HoudiniEngineEditorInstance;
}

bool
FHoudiniEngineEditor::IsInitialized()
{
    return FHoudiniEngineEditor::HoudiniEngineEditorInstance != nullptr;
}

FHoudiniEngineEditor::FHoudiniEngineEditor()
    : LastHoudiniAssetComponentUndoObject( nullptr )
{}

void
FHoudiniEngineEditor::StartupModule()
{
    HOUDINI_LOG_MESSAGE( TEXT( "Starting the Houdini Engine Editor module." ) );
    
    // Register asset type actions.
    RegisterAssetTypeActions();

    // Register asset brokers.
    RegisterAssetBrokers();

    // Register component visualizers.
    RegisterComponentVisualizers();

    // Register detail presenters.
    RegisterDetails();

    // Register actor factories.
    RegisterActorFactories();

    // Create style set.
    RegisterStyleSet();

    // Register thumbnails.
    RegisterThumbnails();

    // Extends the file menu.
    ExtendMenu();

    // Adds the custom console commands
    RegisterConsoleCommands();

    // Register global undo / redo callbacks.
    RegisterForUndo();

#ifdef HOUDINI_MODE
    // Register editor modes
    RegisterModes();
#endif 

    RegisterPlacementModeExtensions();

    // Store the instance.
    FHoudiniEngineEditor::HoudiniEngineEditorInstance = this;
}

void
FHoudiniEngineEditor::ShutdownModule()
{
    HOUDINI_LOG_MESSAGE( TEXT( "Shutting down the Houdini Engine Editor module." ) );

    // Unregister asset type actions.
    UnregisterAssetTypeActions();

    // Unregister detail presenters.
    UnregisterDetails();

    // Unregister thumbnails.
    UnregisterThumbnails();

    // Unregister our component visualizers.
    UnregisterComponentVisualizers();

    // Unregister global undo / redo callbacks.
    UnregisterForUndo();

#ifdef HOUDINI_MODE
    UnregisterModes();
#endif

    UnregisterPlacementModeExtensions();
}

void
FHoudiniEngineEditor::RegisterComponentVisualizers()
{
    if ( GUnrealEd )
    {
        if ( !SplineComponentVisualizer.IsValid() )
        {
            SplineComponentVisualizer = MakeShareable( new FHoudiniSplineComponentVisualizer );

            GUnrealEd->RegisterComponentVisualizer(
                UHoudiniSplineComponent::StaticClass()->GetFName(),
                SplineComponentVisualizer
            );

            SplineComponentVisualizer->OnRegister();
        }

        if ( !HandleComponentVisualizer.IsValid() )
        {
            HandleComponentVisualizer = MakeShareable( new FHoudiniHandleComponentVisualizer );

            GUnrealEd->RegisterComponentVisualizer(
                UHoudiniHandleComponent::StaticClass()->GetFName(),
                HandleComponentVisualizer
            );

            HandleComponentVisualizer->OnRegister();
        }
    }
}

void
FHoudiniEngineEditor::UnregisterComponentVisualizers()
{
    if ( GUnrealEd )
    {
        if ( SplineComponentVisualizer.IsValid() )
            GUnrealEd->UnregisterComponentVisualizer( UHoudiniSplineComponent::StaticClass()->GetFName() );

        if ( HandleComponentVisualizer.IsValid() )
            GUnrealEd->UnregisterComponentVisualizer( UHoudiniHandleComponent::StaticClass()->GetFName() );
    }
}

void
FHoudiniEngineEditor::RegisterDetails()
{
    FPropertyEditorModule & PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( "PropertyEditor" );

    // Register details presenter for our component type and runtime settings.
    PropertyModule.RegisterCustomClassLayout(
        TEXT( "HoudiniAssetComponent" ),
        FOnGetDetailCustomizationInstance::CreateStatic( &FHoudiniAssetComponentDetails::MakeInstance ) );
    PropertyModule.RegisterCustomClassLayout(
        TEXT( "HoudiniRuntimeSettings" ),
        FOnGetDetailCustomizationInstance::CreateStatic( &FHoudiniRuntimeSettingsDetails::MakeInstance ) );
}

void
FHoudiniEngineEditor::UnregisterDetails()
{
    if ( FModuleManager::Get().IsModuleLoaded( "PropertyEditor" ) )
    {
        FPropertyEditorModule & PropertyModule =
            FModuleManager::LoadModuleChecked<FPropertyEditorModule>( "PropertyEditor" );

        PropertyModule.UnregisterCustomClassLayout( TEXT( "HoudiniAssetComponent" ) );
        PropertyModule.UnregisterCustomClassLayout( TEXT( "HoudiniRuntimeSettings" ) );
    }
}

void
FHoudiniEngineEditor::RegisterAssetTypeActions()
{
    // Create and register asset type actions for Houdini asset.
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked< FAssetToolsModule >( "AssetTools" ).Get();
    RegisterAssetTypeAction( AssetTools, MakeShareable( new FHoudiniAssetTypeActions() ) );
}

void
FHoudiniEngineEditor::UnregisterAssetTypeActions()
{
    // Unregister asset type actions we have previously registered.
    if ( FModuleManager::Get().IsModuleLoaded( "AssetTools" ) )
    {
        IAssetTools & AssetTools = FModuleManager::GetModuleChecked< FAssetToolsModule >( "AssetTools" ).Get();

        for ( int32 Index = 0; Index < AssetTypeActions.Num(); ++Index )
            AssetTools.UnregisterAssetTypeActions( AssetTypeActions[ Index ].ToSharedRef() );

        AssetTypeActions.Empty();
    }
}

void
FHoudiniEngineEditor::RegisterAssetTypeAction( IAssetTools & AssetTools, TSharedRef< IAssetTypeActions > Action )
{
    AssetTools.RegisterAssetTypeActions( Action );
    AssetTypeActions.Add( Action );
}

void
FHoudiniEngineEditor::RegisterAssetBrokers()
{
    // Create and register broker for Houdini asset.
    HoudiniAssetBroker = MakeShareable( new FHoudiniAssetBroker() );
    FComponentAssetBrokerage::RegisterBroker( HoudiniAssetBroker, UHoudiniAssetComponent::StaticClass(), true, true );
}

void
FHoudiniEngineEditor::UnregisterAssetBrokers()
{
    if ( UObjectInitialized() )
    {
        // Unregister broker.
        FComponentAssetBrokerage::UnregisterBroker( HoudiniAssetBroker );
    }
}

void
FHoudiniEngineEditor::RegisterActorFactories()
{
    if ( GEditor )
    {
        UHoudiniAssetActorFactory * HoudiniAssetActorFactory =
            NewObject< UHoudiniAssetActorFactory >( GetTransientPackage(), UHoudiniAssetActorFactory::StaticClass() );

        GEditor->ActorFactories.Add( HoudiniAssetActorFactory );
    }
}

void
FHoudiniEngineEditor::ExtendMenu()
{
    if ( !IsRunningCommandlet() )
    {
        // We need to add/bind the UI Commands to their functions first
        BindMenuCommands();

        // Extend main menu, we will add Houdini section.
        MainMenuExtender = MakeShareable( new FExtender );
        MainMenuExtender->AddMenuExtension(
            "FileLoadAndSave", EExtensionHook::After, HEngineCommands,
            FMenuExtensionDelegate::CreateRaw( this, &FHoudiniEngineEditor::AddHoudiniMenuExtension ) );
        FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked< FLevelEditorModule >( "LevelEditor" );
        LevelEditorModule.GetMenuExtensibilityManager()->AddExtender( MainMenuExtender );
    }
}

void
FHoudiniEngineEditor::AddHoudiniMenuExtension( FMenuBuilder & MenuBuilder )
{
    MenuBuilder.BeginSection( "Houdini", LOCTEXT( "HoudiniLabel", "Houdini Engine" ) );

    MenuBuilder.AddMenuEntry( FHoudiniEngineCommands::Get().OpenInHoudini );
    MenuBuilder.AddMenuEntry( FHoudiniEngineCommands::Get().SaveHIPFile );
    MenuBuilder.AddMenuEntry( FHoudiniEngineCommands::Get().ReportBug );
    MenuBuilder.AddMenuEntry( FHoudiniEngineCommands::Get().CleanUpTempFolder );
    MenuBuilder.AddMenuEntry( FHoudiniEngineCommands::Get().BakeAllAssets );
    MenuBuilder.AddMenuEntry( FHoudiniEngineCommands::Get().PauseAssetCooking );

    MenuBuilder.EndSection();
}

void
FHoudiniEngineEditor::BindMenuCommands()
{
    HEngineCommands = MakeShareable( new FUICommandList );

    FHoudiniEngineCommands::Register();
    const FHoudiniEngineCommands& Commands = FHoudiniEngineCommands::Get();

    HEngineCommands->MapAction(
        Commands.OpenInHoudini,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::OpenInHoudini ),
        FCanExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::CanOpenInHoudini ) );

    HEngineCommands->MapAction(
        Commands.SaveHIPFile,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::SaveHIPFile ),
        FCanExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::CanSaveHIPFile ) );

    HEngineCommands->MapAction(
        Commands.ReportBug,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::ReportBug ),
        FCanExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::CanReportBug ) );

    HEngineCommands->MapAction(
        Commands.CleanUpTempFolder,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::CleanUpTempFolder ),
        FCanExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::CanCleanUpTempFolder ) );

    HEngineCommands->MapAction(
        Commands.BakeAllAssets,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::BakeAllAssets ),
        FCanExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::CanBakeAllAssets ) );

    HEngineCommands->MapAction(
        Commands.PauseAssetCooking,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::PauseAssetCooking ),
        FCanExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::CanPauseAssetCooking ),
        FIsActionChecked::CreateRaw( this, &FHoudiniEngineEditor::IsAssetCookingPaused ) );

    // Non menu command (used for shortcuts only)
    HEngineCommands->MapAction(
        Commands.CookSelec,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::RecookSelection ),
        FCanExecuteAction() );

    HEngineCommands->MapAction(
        Commands.RebuildSelec,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::RebuildSelection ),
        FCanExecuteAction() );

    HEngineCommands->MapAction(
        Commands.BakeSelec,
        FExecuteAction::CreateRaw( this, &FHoudiniEngineEditor::BakeSelection ),
        FCanExecuteAction() );

    // Append the command to the editor module
    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked< FLevelEditorModule >("LevelEditor");
    LevelEditorModule.GetGlobalLevelEditorActions()->Append(HEngineCommands.ToSharedRef());
}

void
FHoudiniEngineEditor::RegisterConsoleCommands()
{
    // Register corresponding console commands
    static FAutoConsoleCommand CCmdOpen = FAutoConsoleCommand(
        TEXT("Houdini.Open"),
        TEXT("Open the scene in Houdini."),
        FConsoleCommandDelegate::CreateRaw( this, &FHoudiniEngineEditor::OpenInHoudini ) );

    static FAutoConsoleCommand CCmdSave = FAutoConsoleCommand(
        TEXT("Houdini.Save"),
        TEXT("Save the current Houdini scene to a hip file."),
        FConsoleCommandDelegate::CreateRaw(this, &FHoudiniEngineEditor::SaveHIPFile ) );

    static FAutoConsoleCommand CCmdBake = FAutoConsoleCommand(
        TEXT("Houdini.BakeAll"),
        TEXT("Bakes and replaces with blueprints all Houdini Asset Actors in the current level."),
        FConsoleCommandDelegate::CreateRaw(this, &FHoudiniEngineEditor::BakeAllAssets ) );

    static FAutoConsoleCommand CCmdClean = FAutoConsoleCommand(
        TEXT("Houdini.Clean"),
        TEXT("Cleans up unused/unreferenced Houdini Engine temporary files."),
        FConsoleCommandDelegate::CreateRaw(this, &FHoudiniEngineEditor::CleanUpTempFolder ) );

    static FAutoConsoleCommand CCmdPause = FAutoConsoleCommand(
        TEXT( "Houdini.Pause"),
        TEXT( "Pauses Houdini Engine Asset cooking." ),
        FConsoleCommandDelegate::CreateRaw( this, &FHoudiniEngineEditor::PauseAssetCooking ) );

    // Additional console only commands
    static FAutoConsoleCommand CCmdCookAll = FAutoConsoleCommand(
        TEXT("Houdini.CookAll"),
        TEXT("Re-cooks all Houdini Engine Asset Actors in the current level."),
        FConsoleCommandDelegate::CreateRaw( this, &FHoudiniEngineEditor::RecookAllAssets ) );

    static FAutoConsoleCommand CCmdRebuildAll = FAutoConsoleCommand(
        TEXT("Houdini.RebuildAll"),
        TEXT("Rebuilds all Houdini Engine Asset Actors in the current level."),
        FConsoleCommandDelegate::CreateRaw( this, &FHoudiniEngineEditor::RebuildAllAssets ) );

    static FAutoConsoleCommand CCmdCookSelec = FAutoConsoleCommand(
        TEXT("Houdini.Cook"),
        TEXT("Re-cooks selected Houdini Asset Actors in the current level."),
        FConsoleCommandDelegate::CreateRaw( this, &FHoudiniEngineEditor::RecookSelection ) );

    static FAutoConsoleCommand CCmdRebuildSelec = FAutoConsoleCommand(
        TEXT("Houdini.Rebuild"),
        TEXT("Rebuilds selected Houdini Asset Actors in the current level."),
        FConsoleCommandDelegate::CreateRaw( this, &FHoudiniEngineEditor::RebuildSelection ) );

    static FAutoConsoleCommand CCmdBakeSelec = FAutoConsoleCommand(
        TEXT("Houdini.Bake"),
        TEXT("Bakes and replaces with blueprints selected Houdini Asset Actors in the current level."),
        FConsoleCommandDelegate::CreateRaw( this, &FHoudiniEngineEditor::BakeSelection ) );
}

bool
FHoudiniEngineEditor::CanSaveHIPFile() const
{
    return FHoudiniEngine::IsInitialized();
}

void
FHoudiniEngineEditor::SaveHIPFile()
{
    IDesktopPlatform * DesktopPlatform = FDesktopPlatformModule::Get();
    if ( DesktopPlatform && FHoudiniEngineUtils::IsInitialized() )
    {
        TArray< FString > SaveFilenames;
        bool bSaved = false;
        void * ParentWindowWindowHandle = NULL;

        IMainFrameModule & MainFrameModule = FModuleManager::LoadModuleChecked< IMainFrameModule >( TEXT( "MainFrame" ) );
        const TSharedPtr< SWindow > & MainFrameParentWindow = MainFrameModule.GetParentWindow();
        if ( MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid() )
            ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();

        bSaved = DesktopPlatform->SaveFileDialog(
            ParentWindowWindowHandle,
            NSLOCTEXT( "SaveHIPFile", "SaveHIPFile", "Saves a .hip file of the current Houdini scene." ).ToString(),
            *( FEditorDirectories::Get().GetLastDirectory( ELastDirectory::GENERIC_EXPORT ) ),
            TEXT( "" ),
            TEXT( "Houdini HIP file|*.hip" ),
            EFileDialogFlags::None,
            SaveFilenames );

        if ( bSaved && SaveFilenames.Num() )
        {
            // Add a slate notification
            FString Notification = TEXT("Saving internal Houdini scene...");
            FHoudiniEngineUtils::CreateSlateNotification(Notification);

            // ... and a log message
            HOUDINI_LOG_MESSAGE(TEXT("Saved Houdini scene to %s"), *SaveFilenames[ 0 ] );

            // Get first path.
            std::wstring HIPPath( *SaveFilenames[ 0 ] );
            std::string HIPPathConverted( HIPPath.begin(), HIPPath.end() );

            // Save HIP file through Engine.
            FHoudiniApi::SaveHIPFile( FHoudiniEngine::Get().GetSession(), HIPPathConverted.c_str(), false );
        }
    }
}

bool
FHoudiniEngineEditor::CanOpenInHoudini() const
{
    return FHoudiniEngine::IsInitialized();
}

void
FHoudiniEngineEditor::OpenInHoudini()
{
    if ( !FHoudiniEngine::IsInitialized() )
        return;

    // First, saves the current scene as a hip file
    // Creates a proper temporary file name
    FString UserTempPath = FPaths::CreateTempFilename(
        FPlatformProcess::UserTempDir(), 
        TEXT( "HoudiniEngine" ), TEXT( ".hip" ) );

    // Save HIP file through Engine.
    std::string TempPathConverted( TCHAR_TO_UTF8( *UserTempPath ) );
    FHoudiniApi::SaveHIPFile(
        FHoudiniEngine::Get().GetSession(), 
        TempPathConverted.c_str(), false);

    if ( !FPaths::FileExists( UserTempPath ) )
        return;
    
    // Add a slate notification
    FString Notification = TEXT( "Opening scene in Houdini..." );
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    HOUDINI_LOG_MESSAGE( TEXT("Opened scene in Houdini.") );

    // Then open the hip file in Houdini
    FString LibHAPILocation = FHoudiniEngine::Get().GetLibHAPILocation();
    FString houdiniLocation = LibHAPILocation + "//houdini";
    FPlatformProcess::CreateProc( 
        houdiniLocation.GetCharArray().GetData(), 
        UserTempPath.GetCharArray().GetData(), 
        true, false, false, 
        nullptr, 0,
        FPlatformProcess::UserTempDir(),
        nullptr, nullptr );

    // Unfortunately, LaunchFileInDefaultExternalApplication doesn't seem to be working properly
    //FPlatformProcess::LaunchFileInDefaultExternalApplication( UserTempPath.GetCharArray().GetData(), nullptr, ELaunchVerb::Open );
}

void
FHoudiniEngineEditor::ReportBug()
{
    FPlatformProcess::LaunchURL( HAPI_UNREAL_BUG_REPORT_URL, nullptr, nullptr );
}

bool
FHoudiniEngineEditor::CanReportBug() const
{
    return FHoudiniEngine::IsInitialized();
}

const FName
FHoudiniEngineEditor::GetStyleSetName()
{
    return FName("HoudiniEngineStyle");
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define TTF_FONT( RelativePath, ... ) FSlateFontInfo( StyleSet->RootToContentDir( RelativePath, TEXT(".ttf") ), __VA_ARGS__ )
#define TTF_CORE_FONT( RelativePath, ... ) FSlateFontInfo( StyleSet->RootToCoreContentDir( RelativePath, TEXT(".ttf") ), __VA_ARGS__ )
#define OTF_FONT( RelativePath, ... ) FSlateFontInfo( StyleSet->RootToContentDir( RelativePath, TEXT(".otf") ), __VA_ARGS__ )
#define OTF_CORE_FONT( RelativePath, ... ) FSlateFontInfo( StyleSet->RootToCoreContentDir( RelativePath, TEXT(".otf") ), __VA_ARGS__ )

void
FHoudiniEngineEditor::RegisterStyleSet()
{
    // Create Slate style set.
    if ( !StyleSet.IsValid() )
    {
        StyleSet = MakeShareable( new FSlateStyleSet( FHoudiniEngineEditor::GetStyleSetName() ) );
        StyleSet->SetContentRoot( FPaths::EngineContentDir() / TEXT( "Editor/Slate" ) );
        StyleSet->SetCoreContentRoot( FPaths::EngineContentDir() / TEXT( "Slate" ) );

        // Note, these sizes are in Slate Units.
        // Slate Units do NOT have to map to pixels.
        const FVector2D Icon5x16( 5.0f, 16.0f );
        const FVector2D Icon8x4( 8.0f, 4.0f );
        const FVector2D Icon8x8( 8.0f, 8.0f );
        const FVector2D Icon10x10( 10.0f, 10.0f );
        const FVector2D Icon12x12( 12.0f, 12.0f );
        const FVector2D Icon12x16( 12.0f, 16.0f );
        const FVector2D Icon14x14( 14.0f, 14.0f );
        const FVector2D Icon16x16( 16.0f, 16.0f );
        const FVector2D Icon20x20( 20.0f, 20.0f );
        const FVector2D Icon22x22( 22.0f, 22.0f );
        const FVector2D Icon24x24( 24.0f, 24.0f );
        const FVector2D Icon25x25( 25.0f, 25.0f );
        const FVector2D Icon32x32( 32.0f, 32.0f );
        const FVector2D Icon40x40( 40.0f, 40.0f );
        const FVector2D Icon64x64( 64.0f, 64.0f );
        const FVector2D Icon36x24( 36.0f, 24.0f );
        const FVector2D Icon128x128( 128.0f, 128.0f );

        static FString IconsDir = FPaths::EnginePluginsDir() / TEXT( "Runtime/HoudiniEngine/Content/Icons/" );
        StyleSet->Set(
            "HoudiniEngine.HoudiniEngineLogo",
            new FSlateImageBrush( IconsDir + TEXT( "icon_houdini_logo_16.png" ), Icon16x16 ) );
        StyleSet->Set(
            "ClassIcon.HoudiniAssetActor",
            new FSlateImageBrush( IconsDir + TEXT( "icon_houdini_logo_16.png" ), Icon16x16 ) );
        StyleSet->Set(
            "HoudiniEngine.HoudiniEngineLogo40",
            new FSlateImageBrush( IconsDir + TEXT( "icon_houdini_logo_40.png" ), Icon40x40 ) );

        FSlateImageBrush* HoudiniLogo16 = new FSlateImageBrush( IconsDir + TEXT( "icon_houdini_logo_16.png" ), Icon16x16 );
        StyleSet->Set( "HoudiniEngine.SaveHIPFile", HoudiniLogo16 );
        StyleSet->Set( "HoudiniEngine.ReportBug", HoudiniLogo16 );
        StyleSet->Set( "HoudiniEngine.OpenInHoudini", HoudiniLogo16 );
        StyleSet->Set( "HoudiniEngine.CleanUpTempFolder", HoudiniLogo16 );
        StyleSet->Set( "HoudiniEngine.BakeAllAssets", HoudiniLogo16 );
        StyleSet->Set( "HoudiniEngine.PauseAssetCooking", HoudiniLogo16 );

        // We need some colors from Editor Style & this is the only way to do this at the moment
        const FSlateColor DefaultForeground = FEditorStyle::GetSlateColor( "DefaultForeground" );
        const FSlateColor InvertedForeground = FEditorStyle::GetSlateColor( "InvertedForeground" );
        const FSlateColor SelectorColor = FEditorStyle::GetSlateColor( "SelectorColor" );
        const FSlateColor SelectionColor = FEditorStyle::GetSlateColor( "SelectionColor" );
        const FSlateColor SelectionColor_Inactive = FEditorStyle::GetSlateColor( "SelectionColor_Inactive" );

        // Normal Text
        FTextBlockStyle NormalText = FTextBlockStyle()
            .SetFont( TTF_CORE_FONT( "Fonts/Roboto-Regular", 9 ) )
            .SetColorAndOpacity( FSlateColor::UseForeground() )
            .SetShadowOffset( FVector2D::ZeroVector )
            .SetShadowColorAndOpacity( FLinearColor::Black )
            .SetHighlightColor( FLinearColor( 0.02f, 0.3f, 0.0f ) )
            .SetHighlightShape( BOX_BRUSH( "Common/TextBlockHighlightShape", FMargin( 3.f / 8.f ) ) );

        StyleSet->Set( "HoudiniEngine.TableRow", FTableRowStyle()
                       .SetEvenRowBackgroundBrush( FSlateNoResource() )
                       .SetEvenRowBackgroundHoveredBrush( IMAGE_BRUSH( "Common/Selection", Icon8x8, FLinearColor( 1.0f, 1.0f, 1.0f, 0.1f ) ) )
                       .SetOddRowBackgroundBrush( FSlateNoResource() )
                       .SetOddRowBackgroundHoveredBrush( IMAGE_BRUSH( "Common/Selection", Icon8x8, FLinearColor( 1.0f, 1.0f, 1.0f, 0.1f ) ) )
                       .SetSelectorFocusedBrush( BORDER_BRUSH( "Common/Selector", FMargin( 4.f / 16.f ), SelectorColor ) )
                       .SetActiveBrush( IMAGE_BRUSH( "Common/Selection", Icon8x8, SelectionColor ) )
                       .SetActiveHoveredBrush( IMAGE_BRUSH( "Common/Selection", Icon8x8, SelectionColor ) )
                       .SetInactiveBrush( IMAGE_BRUSH( "Common/Selection", Icon8x8, SelectionColor_Inactive ) )
                       .SetInactiveHoveredBrush( IMAGE_BRUSH( "Common/Selection", Icon8x8, SelectionColor_Inactive ) )
                       .SetTextColor( DefaultForeground )
                       .SetSelectedTextColor( InvertedForeground )
        );

        StyleSet->Set( "HoudiniEngine.ThumbnailShadow", new BOX_BRUSH( "ContentBrowser/ThumbnailShadow", FMargin( 4.0f / 64.0f ) ) );
        StyleSet->Set( "HoudiniEngine.ThumbnailBackground", new IMAGE_BRUSH( "Common/ClassBackground_64x", FVector2D( 64.f, 64.f ), FLinearColor( 0.75f, 0.75f, 0.75f, 1.0f ) ) );
        StyleSet->Set( "HoudiniEngine.ThumbnailText", NormalText );

        // Register Slate style.
        FSlateStyleRegistry::RegisterSlateStyle( *StyleSet.Get() );
    }
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef TTF_CORE_FONT
#undef OTF_FONT
#undef OTF_CORE_FONT

void
FHoudiniEngineEditor::UnregisterStyleSet()
{
    // Unregister Slate style set.
    if ( StyleSet.IsValid() )
    {
        // Unregister Slate style.
        FSlateStyleRegistry::UnRegisterSlateStyle( *StyleSet.Get() );

        ensure( StyleSet.IsUnique() );
        StyleSet.Reset();
    }
}

void
FHoudiniEngineEditor::RegisterForUndo()
{
    if ( GUnrealEd )
        GUnrealEd->RegisterForUndo( this );
}

void
FHoudiniEngineEditor::UnregisterForUndo()
{
    if ( GUnrealEd )
        GUnrealEd->UnregisterForUndo( this );
}

void 
FHoudiniEngineEditor::RegisterModes()
{
    FEditorModeRegistry::Get().RegisterMode<FHoudiniShelfEdMode>( 
        FHoudiniShelfEdMode::EM_HoudiniShelfEdModeId, 
        LOCTEXT( "HoudiniMode", "Houdini Tools" ), 
        FSlateIcon( StyleSet->GetStyleSetName(), "HoudiniEngine.HoudiniEngineLogo40" ),
        true );
}

void 
FHoudiniEngineEditor::UnregisterModes()
{
    FEditorModeRegistry::Get().UnregisterMode( FHoudiniShelfEdMode::EM_HoudiniShelfEdModeId );
}

/** Registers placement mode extensions. */
void 
FHoudiniEngineEditor::RegisterPlacementModeExtensions()
{
    // Load custom houdini tools 
    const UHoudiniRuntimeSettings * HoudiniRuntimeSettings = GetDefault< UHoudiniRuntimeSettings >();
    check( HoudiniRuntimeSettings );

    if ( HoudiniRuntimeSettings->bHidePlacementModeHoudiniTools )
        return;

    //
    // Set up Built-in Houdini Tools
    //
    auto ToolArray = HoudiniRuntimeSettings->CustomHoudiniTools;
    AddDefaultHoudiniToolToArray( ToolArray );

    for ( const FHoudiniToolDescription& HoudiniTool : ToolArray )
    {
        FText ToolName = FText::FromString( HoudiniTool.Name );
        FText ToolTip = FText::FromString( HoudiniTool.ToolTip );

        FString IconPath = FPaths::ConvertRelativePathToFull( HoudiniTool.IconPath.FilePath );
        const FSlateBrush* CustomIconBrush = nullptr;
        if ( FPaths::FileExists( IconPath ) )
        {
            FName BrushName = *IconPath;
            CustomIconBrush = new FSlateDynamicImageBrush( BrushName, FVector2D( 40.f, 40.f ) );
        }
        else
        {
            CustomIconBrush = StyleSet->GetBrush( TEXT( "HoudiniEngine.HoudiniEngineLogo40" ) );
        }

        HoudiniTools.Add( MakeShareable( new FHoudiniTool( HoudiniTool.HoudiniAsset, ToolName, HoudiniTool.Type, ToolTip, CustomIconBrush, HoudiniTool.HelpURL ) ) );
    }

    FPlacementCategoryInfo Info(
        LOCTEXT( "HoudiniCategoryName", "Houdini Engine" ),
        "HoudiniEngine",
        TEXT( "PMHoudiniEngine" ),
        25
    );
    Info.CustomGenerator = []() -> TSharedRef<SWidget> { return SNew( SHoudiniToolPalette ); };

    IPlacementModeModule::Get().RegisterPlacementCategory( Info );
}

void
FHoudiniEngineEditor::AddDefaultHoudiniToolToArray( TArray< FHoudiniToolDescription >& ToolArray )
{
    // Default paths
    FString ToolsDir = FPaths::EnginePluginsDir() / TEXT("Runtime/HoudiniEngine/Content/Tools/");
    FString DefaultIconPath = FPaths::EnginePluginsDir() / TEXT( "Runtime/HoudiniEngine/Content/Icons/icon_houdini_logo_40.png" );    

    int32 nInsertPos = 0;
    // 1. Rock Generator
    ToolArray.Insert( FHoudiniToolDescription{
        TEXT( "Rock Generator" ),
        EHoudiniToolType::HTOOLTYPE_GENERATOR,
        TEXT( "Generates procedural rock meshes" ),
        FFilePath{ ToolsDir / TEXT("rock_generator.png") },
        TSoftObjectPtr<UHoudiniAsset>( FStringAssetReference( TEXT( "HoudiniAsset'/HoudiniEngine/Tools/rock_generator.rock_generator'" ) ) ),
        TEXT("http://www.sidefx.com/docs/unreal/")
    }, nInsertPos++ );

    // 2. Boolean
    ToolArray.Insert( FHoudiniToolDescription{
        TEXT( "Boolean" ),
        EHoudiniToolType::HTOOLTYPE_OPERATOR_MULTI,
        TEXT( "Apply boolean operations to two input objects" ),
        FFilePath{ ToolsDir / TEXT("he_sop_boolean.png") },
        TSoftObjectPtr<UHoudiniAsset>( FStringAssetReference( TEXT( "HoudiniAsset'/HoudiniEngine/Tools/he_sop_boolean.he_sop_boolean'" ) ) ),
        TEXT("http://www.sidefx.com/docs/unreal/")
    }, nInsertPos++ );

    // 3. Polyreducer
    ToolArray.Insert( FHoudiniToolDescription{
        TEXT( "Polyreducer" ),
        EHoudiniToolType::HTOOLTYPE_OPERATOR_BATCH,
        TEXT( "Reduces the number of polygons of the input objects" ),
        FFilePath{ ToolsDir / TEXT("he_sop_polyreduce.png") },
        TSoftObjectPtr<UHoudiniAsset>( FStringAssetReference( TEXT( "HoudiniAsset'/HoudiniEngine/Tools/he_sop_polyreduce.he_sop_polyreduce'" ) ) ),
        TEXT("http://www.sidefx.com/docs/unreal/")
    }, nInsertPos++ );

    // 4. Curve Instancer
    ToolArray.Insert( FHoudiniToolDescription{
        TEXT( "Curve Instancer" ),
        EHoudiniToolType::HTOOLTYPE_OPERATOR_SINGLE,
        TEXT( "Scatters and instances the input objects along a curve or in a zone defined by a closed curve." ),
        FFilePath{ ToolsDir / TEXT("he_sop_curve_instancer.png") },
        TSoftObjectPtr<UHoudiniAsset>( FStringAssetReference( TEXT( "HoudiniAsset'/HoudiniEngine/Tools/he_sop_curve_instancer.he_sop_curve_instancer'" ) ) ),
        TEXT("http://www.sidefx.com/docs/unreal/")
    }, nInsertPos++ );
}

void 
FHoudiniEngineEditor::UnregisterPlacementModeExtensions()
{
    if ( IPlacementModeModule::IsAvailable() )
    {
        IPlacementModeModule::Get().UnregisterPlacementCategory( "HoudiniEngine" );
    }
}

TSharedPtr<ISlateStyle>
FHoudiniEngineEditor::GetSlateStyle() const
{
    return StyleSet;
}

void
FHoudiniEngineEditor::RegisterThumbnails()
{
    UThumbnailManager::Get().RegisterCustomRenderer( UHoudiniAsset::StaticClass(), UHoudiniAssetThumbnailRenderer::StaticClass() );
}

void
FHoudiniEngineEditor::UnregisterThumbnails()
{
    if ( UObjectInitialized() )
        UThumbnailManager::Get().UnregisterCustomRenderer( UHoudiniAsset::StaticClass() );
}

bool
FHoudiniEngineEditor::MatchesContext( const FString & InContext, UObject * PrimaryObject ) const
{
    if ( InContext == TEXT( HOUDINI_MODULE_EDITOR ) || InContext == TEXT( HOUDINI_MODULE_RUNTIME ) )
    {
        LastHoudiniAssetComponentUndoObject = Cast< UHoudiniAssetComponent >( PrimaryObject );
        return true;
    }

    LastHoudiniAssetComponentUndoObject = nullptr;
    return false;
}

void
FHoudiniEngineEditor::PostUndo( bool bSuccess )
{
    if ( LastHoudiniAssetComponentUndoObject && bSuccess )
    {
        LastHoudiniAssetComponentUndoObject->UpdateEditorProperties( false );
        LastHoudiniAssetComponentUndoObject = nullptr;
    }
}

void
FHoudiniEngineEditor::PostRedo( bool bSuccess )
{
    if ( LastHoudiniAssetComponentUndoObject && bSuccess )
    {
        LastHoudiniAssetComponentUndoObject->UpdateEditorProperties( false );
        LastHoudiniAssetComponentUndoObject = nullptr;
    }
}

void
FHoudiniEngineEditor::CleanUpTempFolder()
{
    // Add a slate notification
    FString Notification = TEXT("Cleaning up Houdini Engine temporary folder");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // Get Runtime settings to get the Temp Cook Folder
    const UHoudiniRuntimeSettings * HoudiniRuntimeSettings = GetDefault< UHoudiniRuntimeSettings >();
    if (!HoudiniRuntimeSettings)
        return;
    
    FString TempCookFolder = HoudiniRuntimeSettings->TemporaryCookFolder.ToString();

    // The Asset registry will help us finding if the content of the asset is referenced
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    int32 DeletedCount = 0;
    bool bDidDeleteAsset = true;
    while ( bDidDeleteAsset )
    {
        // To correctly clean the temp folder, we need to iterate multiple times, because some of the temp assets
        // might be referenced by other temp assets.. (ie Textures are referenced by Materials)
        // We'll stop looking for assets to delete when no deletion occured.
        bDidDeleteAsset = false;

        // The Object library will list all UObjects found in the TempFolder
        auto ObjectLibrary = UObjectLibrary::CreateLibrary( UObject::StaticClass(), false, true );
        ObjectLibrary->LoadAssetDataFromPath( TempCookFolder );

        // Getting all the found asset in the TEMPO folder
        TArray<FAssetData> AssetDataList;
        ObjectLibrary->GetAssetDataList( AssetDataList );

        // All the assets we're going to delete
        TArray<FAssetData> AssetDataToDelete;

        for ( FAssetData Data : AssetDataList )
        {
            UPackage* CurrentPackage = Data.GetPackage();
            if ( !CurrentPackage )
                continue;

            TArray<FAssetData> AssetsInPackage;
            bool bAssetDataSafeToDelete = true;
            AssetRegistryModule.Get().GetAssetsByPackageName( CurrentPackage->GetFName(), AssetsInPackage );

            for ( const auto& AssetInfo : AssetsInPackage )
            {
                if ( !AssetInfo.GetAsset() )
                    continue;

                // Check and see whether we are referenced by any objects that won't be garbage collected (*including* the undo buffer)
                FReferencerInformationList ReferencesIncludingUndo;
                UObject* AssetInPackage = AssetInfo.GetAsset();
                bool bReferencedInMemoryOrUndoStack = IsReferenced( AssetInPackage, GARBAGE_COLLECTION_KEEPFLAGS, EInternalObjectFlags::GarbageCollectionKeepFlags, true, &ReferencesIncludingUndo );
                if ( !bReferencedInMemoryOrUndoStack )
                    continue;

                // We do have external references, check if the external references are in our ObjectToDelete list
                // If they are, we can delete the asset cause its references are going to be deleted too.
                for ( auto ExtRef : ReferencesIncludingUndo.ExternalReferences )
                {
                    UObject* Outer = ExtRef.Referencer->GetOuter();

                    bool bOuterFound = false;
                    for ( auto DataToDelete : AssetDataToDelete )
                    {
                        if ( DataToDelete.GetPackage() == Outer )
                        {
                            bOuterFound = true;
                            break;
                        }
                        else if ( DataToDelete.GetAsset() == Outer )
                        {
                            bOuterFound = true;
                            break;
                        }
                    }

                    // We have at least one reference that's not going to be deleted, we have to keep the asset
                    if ( !bOuterFound )
                    {
                        bAssetDataSafeToDelete = false;
                        break;
                    }
                }
            }
            
            if ( bAssetDataSafeToDelete )
                AssetDataToDelete.Add( Data );
        }

        // Nothing to delete
        if ( AssetDataToDelete.Num() <= 0 )
            break;

        int32 CurrentDeleted = ObjectTools::DeleteAssets( AssetDataToDelete, false );
        if ( CurrentDeleted <= 0 )
        {
            // Normal deletion failed...  Try to force delete the objects?
            TArray<UObject*> ObjectsToDelete;
            for (int i = 0; i < AssetDataToDelete.Num(); i++)
            {
                const FAssetData& AssetData = AssetDataToDelete[i];
                UObject *ObjectToDelete = AssetData.GetAsset();
                // Assets can be loaded even when their underlying type/class no longer exists...
                if (ObjectToDelete != nullptr)
                {
                    ObjectsToDelete.Add(ObjectToDelete);
                }
            }

            CurrentDeleted = ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
        }

        if ( CurrentDeleted > 0 )
        {
            DeletedCount += CurrentDeleted;
            bDidDeleteAsset = true;
        }
    }

    // Add a slate notification
    Notification = TEXT("Deleted ") + FString::FromInt( DeletedCount ) + TEXT(" temporary files.");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    HOUDINI_LOG_MESSAGE( TEXT("Deleted %d temporary files."), DeletedCount );
}

bool
FHoudiniEngineEditor::CanCleanUpTempFolder() const
{
    return FHoudiniEngine::IsInitialized();
}

void
FHoudiniEngineEditor::BakeAllAssets()
{
    // Add a slate notification
    FString Notification = TEXT("Baking all assets in the current level...");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // Bakes and replaces with blueprints all Houdini Assets in the current level
    int32 BakedCount = 0;
    for (TObjectIterator<UHoudiniAssetComponent> Itr; Itr; ++Itr)
    {
        UHoudiniAssetComponent * HoudiniAssetComponent = *Itr;
        if ( !HoudiniAssetComponent )
        {
            HOUDINI_LOG_ERROR( TEXT( "Failed to export a Houdini Asset in the scene!" ) );
            continue;
        }

        if ( !HoudiniAssetComponent->IsComponentValid() )
        {
            FString AssetName = HoudiniAssetComponent->GetOuter() ? HoudiniAssetComponent->GetOuter()->GetName() : HoudiniAssetComponent->GetName();
            if ( AssetName != "Default__HoudiniAssetActor" )
                HOUDINI_LOG_ERROR( TEXT( "Failed to export Houdini Asset: %s in the scene!" ), *AssetName );
            continue;
        }

        // If component is not cooking or instancing, we can bake blueprint.
        if ( !HoudiniAssetComponent->IsInstantiatingOrCooking() )
        {
            if ( FHoudiniEngineBakeUtils::ReplaceHoudiniActorWithBlueprint( HoudiniAssetComponent ) )
                BakedCount++;
        }
    }

    // Add a slate notification
    Notification = TEXT("Baked ") + FString::FromInt( BakedCount ) + TEXT(" Houdini assets.");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    HOUDINI_LOG_MESSAGE( TEXT("Baked all %d Houdini assets in the current level."), BakedCount );
}

bool
FHoudiniEngineEditor::CanBakeAllAssets() const
{
    if ( !FHoudiniEngine::IsInitialized() )
        return false;
    
    return true;
}

void
FHoudiniEngineEditor::PauseAssetCooking()
{
    // Revert the global flag
    bool CurrentEnableCookingGlobal = !FHoudiniEngine::Get().GetEnableCookingGlobal();
    FHoudiniEngine::Get().SetEnableCookingGlobal( CurrentEnableCookingGlobal );

    // Add a slate notification
    FString Notification = TEXT("Houdini Engine cooking paused");
    if ( CurrentEnableCookingGlobal )
        Notification = TEXT("Houdini Engine cooking resumed");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    if ( CurrentEnableCookingGlobal )
        HOUDINI_LOG_MESSAGE( TEXT("Houdini Engine cooking resumed.") );
    else
        HOUDINI_LOG_MESSAGE( TEXT("Houdini Engine cooking paused.") );

    if ( !CurrentEnableCookingGlobal )
        return;

    // If we are unpausing, tick each asset component to "update" them
    for (TObjectIterator<UHoudiniAssetComponent> Itr; Itr; ++Itr)
    {
        UHoudiniAssetComponent * HoudiniAssetComponent = *Itr;
        if (!HoudiniAssetComponent || !HoudiniAssetComponent->IsValidLowLevel())
        {
            HOUDINI_LOG_ERROR( TEXT("Failed to cook a Houdini Asset in the scene!") );
            continue;
        }

        HoudiniAssetComponent->StartHoudiniTicking();
    }
}

bool
FHoudiniEngineEditor::CanPauseAssetCooking()
{
    if ( !FHoudiniEngine::IsInitialized() )
        return false;

    return true;
}

bool
FHoudiniEngineEditor::IsAssetCookingPaused()
{
    return !FHoudiniEngine::Get().GetEnableCookingGlobal();
}

void
FHoudiniEngineEditor::RecookSelection()
{
    // Get current world selection
    TArray<UObject*> WorldSelection;
    int32 SelectedHoudiniAssets = FHoudiniEngineEditor::GetWorldSelection( WorldSelection, true );
    if ( SelectedHoudiniAssets <= 0 )
    {
        HOUDINI_LOG_MESSAGE( TEXT( "No Houdini Assets selected in the world outliner" ) );
        return;
    }

    // Add a slate notification
    FString Notification = TEXT("Cooking selected Houdini Assets...");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // Iterates over the selection and cook the assets if they're in a valid state
    int32 CookedCount = 0;
    for ( int32 Idx = 0; Idx < SelectedHoudiniAssets; Idx++ )
    {
        AHoudiniAssetActor * HoudiniAssetActor = Cast<AHoudiniAssetActor>( WorldSelection[ Idx ] );
        if ( !HoudiniAssetActor )
            continue;

        UHoudiniAssetComponent * HoudiniAssetComponent = HoudiniAssetActor->GetHoudiniAssetComponent();
        if ( !HoudiniAssetComponent || !HoudiniAssetComponent->IsComponentValid() )
            continue;

        HoudiniAssetComponent->StartTaskAssetCookingManual();
        CookedCount++;
    }

    // Add a slate notification
    Notification = TEXT("Re-cooked ") + FString::FromInt( CookedCount ) + TEXT(" Houdini assets.");
    FHoudiniEngineUtils::CreateSlateNotification(Notification);

    // ... and a log message
    HOUDINI_LOG_MESSAGE( TEXT("Re-cooked %d selected Houdini assets."), CookedCount );
}

void
FHoudiniEngineEditor::RecookAllAssets()
{
    // Add a slate notification
    FString Notification = TEXT("Cooking all assets in the current level...");
    FHoudiniEngineUtils::CreateSlateNotification(Notification);

    // Bakes and replaces with blueprints all Houdini Assets in the current level
    int32 CookedCount = 0;
    for (TObjectIterator<UHoudiniAssetComponent> Itr; Itr; ++Itr)
    {
        UHoudiniAssetComponent * HoudiniAssetComponent = *Itr;
        if (!HoudiniAssetComponent || !HoudiniAssetComponent->IsComponentValid())
            continue;

        HoudiniAssetComponent->StartTaskAssetCookingManual();
        CookedCount++;
    }

    // Add a slate notification
    Notification = TEXT("Re-cooked ") + FString::FromInt( CookedCount ) + TEXT(" Houdini assets.");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    HOUDINI_LOG_MESSAGE(TEXT("Re-cooked %d Houdini assets in the current level."), CookedCount );
}

void
FHoudiniEngineEditor::RebuildAllAssets()
{
    // Add a slate notification
    FString Notification = TEXT("Re-building all assets in the current level...");
    FHoudiniEngineUtils::CreateSlateNotification(Notification);

    // Bakes and replaces with blueprints all Houdini Assets in the current level
    int32 RebuiltCount = 0;
    for (TObjectIterator<UHoudiniAssetComponent> Itr; Itr; ++Itr)
    {
        UHoudiniAssetComponent * HoudiniAssetComponent = *Itr;
        if ( !HoudiniAssetComponent || !HoudiniAssetComponent->IsComponentValid() )
            continue;

        HoudiniAssetComponent->StartTaskAssetRebuildManual();
        RebuiltCount++;
    }

    // Add a slate notification
    Notification = TEXT("Rebuilt ") + FString::FromInt( RebuiltCount ) + TEXT(" Houdini assets.");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    HOUDINI_LOG_MESSAGE(TEXT("Rebuilt %d Houdini assets in the current level."), RebuiltCount );
}

void
FHoudiniEngineEditor::RebuildSelection()
{
    // Get current world selection
    TArray<UObject*> WorldSelection;
    int32 SelectedHoudiniAssets = FHoudiniEngineEditor::GetWorldSelection( WorldSelection, true );
    if ( SelectedHoudiniAssets <= 0 )
    {
        HOUDINI_LOG_MESSAGE(TEXT("No Houdini Assets selected in the world outliner"));
        return;
    }

    // Add a slate notification
    FString Notification = TEXT("Rebuilding selected Houdini Assets...");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // Iterates over the selection and rebuilds the assets if they're in a valid state
    int32 RebuiltCount = 0;
    for ( int32 Idx = 0; Idx < SelectedHoudiniAssets; Idx++ )
    {
        AHoudiniAssetActor * HoudiniAssetActor = Cast<AHoudiniAssetActor>( WorldSelection[ Idx ] );
        if ( !HoudiniAssetActor )
            continue;

        UHoudiniAssetComponent * HoudiniAssetComponent = HoudiniAssetActor->GetHoudiniAssetComponent();
        if ( !HoudiniAssetComponent || !HoudiniAssetComponent->IsComponentValid() )
            continue;

        HoudiniAssetComponent->StartTaskAssetRebuildManual();
        RebuiltCount++;
    }

    // Add a slate notification
    Notification = TEXT("Rebuilt ") + FString::FromInt( RebuiltCount ) + TEXT(" Houdini assets.");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    HOUDINI_LOG_MESSAGE( TEXT("Rebuilt %d selected Houdini assets."), RebuiltCount );
}

void
FHoudiniEngineEditor::BakeSelection()
{
    // Get current world selection
    TArray<UObject*> WorldSelection;
    int32 SelectedHoudiniAssets = FHoudiniEngineEditor::GetWorldSelection( WorldSelection, true );
    if ( SelectedHoudiniAssets <= 0 )
    {
        HOUDINI_LOG_MESSAGE( TEXT("No Houdini Assets selected in the world outliner") );
        return;
    }

    // Add a slate notification
    FString Notification = TEXT("Baking selected Houdini Asset Actors in the current level...");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // Iterates over the selection and rebuilds the assets if they're in a valid state
    int32 BakedCount = 0;
    for ( int32 Idx = 0; Idx < SelectedHoudiniAssets; Idx++ )
    {
        AHoudiniAssetActor * HoudiniAssetActor = Cast<AHoudiniAssetActor>( WorldSelection[ Idx ] );
        if ( !HoudiniAssetActor )
            continue;

        UHoudiniAssetComponent* HoudiniAssetComponent = HoudiniAssetActor->GetHoudiniAssetComponent();
        if ( !HoudiniAssetComponent )
        {
            HOUDINI_LOG_ERROR( TEXT("Failed to export a Houdini Asset in the scene!") );
            continue;
        }

        if ( !HoudiniAssetComponent->IsComponentValid() )
        {
            FString AssetName = HoudiniAssetComponent->GetOuter() ? HoudiniAssetComponent->GetOuter()->GetName() : HoudiniAssetComponent->GetName();
            HOUDINI_LOG_ERROR(TEXT("Failed to export Houdini Asset: %s in the scene!"), *AssetName);
            continue;
        }

        // If component is not cooking or instancing, we can bake blueprint.
        if ( !HoudiniAssetComponent->IsInstantiatingOrCooking() )
        {
            if ( FHoudiniEngineBakeUtils::ReplaceHoudiniActorWithBlueprint( HoudiniAssetComponent ) )
                BakedCount++;
        }
    }

    // Add a slate notification
    Notification = TEXT("Baked ") + FString::FromInt(BakedCount) + TEXT(" Houdini assets.");
    FHoudiniEngineUtils::CreateSlateNotification( Notification );

    // ... and a log message
    HOUDINI_LOG_MESSAGE(TEXT("Baked all %d Houdini assets in the current level."), BakedCount);
}

int32
FHoudiniEngineEditor::GetContentBrowserSelection( TArray< UObject* >& ContentBrowserSelection )
{
    ContentBrowserSelection.Empty();

    // Get the current Content browser selection
    FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked< FContentBrowserModule >( "ContentBrowser" );
    TArray<FAssetData> SelectedAssets;
    ContentBrowserModule.Get().GetSelectedAssets( SelectedAssets );

    for( int32 n = 0; n < SelectedAssets.Num(); n++ )
    {
        // Get the current object
        UObject * Object = SelectedAssets[ n ].GetAsset();
        if ( !Object )
            continue;

        // Only static meshes are supported
        if ( Object->GetClass() != UStaticMesh::StaticClass() )
            continue;

        ContentBrowserSelection.Add( Object );
    }

    return ContentBrowserSelection.Num();
}

int32 
FHoudiniEngineEditor::GetWorldSelection( TArray< UObject* >& WorldSelection, bool bHoudiniAssetActorsOnly )
{
    WorldSelection.Empty();

    // Get the current editor selection
    if ( GEditor )
    {
        USelection* SelectedActors = GEditor->GetSelectedActors();
        for ( FSelectionIterator It( *SelectedActors ); It; ++It )
        {
            AActor * Actor = Cast< AActor >( *It );
            if ( !Actor )
                continue;

            // Ignore the SkySphere?
            FString ClassName = Actor->GetClass() ? Actor->GetClass()->GetName() : FString();
            if ( ClassName == TEXT( "BP_Sky_Sphere_C" ) )
                continue;

            // We're normally only selecting actors with StaticMeshComponents and SplineComponents
            // Heightfields? Filter here or later? also allow HoudiniAssets?
            WorldSelection.Add( Actor );
        }
    }

    // If we only want Houdini Actors...
    if ( bHoudiniAssetActorsOnly )
    {
        // ... remove all but them
        for ( int32 Idx = WorldSelection.Num() - 1; Idx >= 0; Idx-- )
        {
            AHoudiniAssetActor * HoudiniAssetActor = Cast<AHoudiniAssetActor>( WorldSelection[ Idx ] );
            if ( !HoudiniAssetActor)
                WorldSelection.RemoveAt( Idx );
        }
    }

    return WorldSelection.Num();
}

void 
FHoudiniEngineCommands::RegisterCommands()
{  
    UI_COMMAND( OpenInHoudini, "Open scene in Houdini", "Opens the current Houdini scene in Houdini.", EUserInterfaceActionType::Button, FInputChord( EKeys::O, EModifierKey::Control | EModifierKey::Alt) );

    UI_COMMAND( SaveHIPFile, "Save Houdini scene (HIP)", "Saves a .hip file of the current Houdini scene.", EUserInterfaceActionType::Button, FInputChord() );
    UI_COMMAND( ReportBug, "Report a plugin bug", "Report a bug for Houdini Engine plugin.", EUserInterfaceActionType::Button, FInputChord() );
   
    UI_COMMAND( CleanUpTempFolder, "Clean Houdini Engine Temp Folder", "Deletes the unused temporary files in the Temporary Cook Folder.", EUserInterfaceActionType::Button, FInputChord() );
    UI_COMMAND( BakeAllAssets, "Bake And Replace All Houdini Assets", "Bakes and replaces with blueprints all Houdini Assets in the scene.", EUserInterfaceActionType::Button, FInputChord() );
    UI_COMMAND( PauseAssetCooking, "Pause Houdini Engine Cooking", "When activated, prevents Houdini Engine from cooking assets until unpaused.", EUserInterfaceActionType::Check, FInputChord( EKeys::P, EModifierKey::Control | EModifierKey::Alt ) );

    UI_COMMAND( CookSelec, "Recook Selection", "Recooks selected Houdini Asset Actors in the current level.", EUserInterfaceActionType::Button, FInputChord( EKeys::C, EModifierKey::Control | EModifierKey::Alt ) );
    UI_COMMAND( RebuildSelec, "Rebuild Selection", "Rebuilds selected Houdini Asset Actors in the current level.", EUserInterfaceActionType::Button, FInputChord( EKeys::R, EModifierKey::Control | EModifierKey::Alt ) );
    UI_COMMAND( BakeSelec, "Bake Selection", "Bakes and replaces with blueprints selected Houdini Asset Actors in the current level.", EUserInterfaceActionType::Button, FInputChord( EKeys::B, EModifierKey::Control | EModifierKey::Alt ) );
}

#undef LOCTEXT_NAMESPACE