// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/GRTWorldTypes.h"
#include "GRTOrchestrator.generated.h"

UENUM(BlueprintType)
enum class EShowcasePhase : uint8
{
	Warmup,
	AuthoredWorldDemo,
	SculptingDemo,
	LayerDemo,
	UndoRedoDemo,
	PopulationDemo,
	ProceduralWorldDemo,
	GameplayHooksDemo,
	DesignerWorkflowDemo,
	PersistenceDemo,
	ReplicationDemo,
	LODDemo,
	Interactive
};

USTRUCT()
struct FShowcaseCameraWaypoint
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float TransitDuration = 1.0f;
	float HoldDuration = 1.0f;
};

USTRUCT()
struct FShowcasePhaseConfig
{
	GENERATED_BODY()

	float Duration = 5.0f;
	FString Title;
	FString Description;
};

/**
 * AGRTOrchestrator - drives the product demo flow and can bootstrap
 * a fallback runtime world when the map was not fully prepared in advance.
 */
UCLASS()
class GRT_API AGRTOrchestrator : public AActor
{
	GENERATED_BODY()

public:
	AGRTOrchestrator();
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Showcase")
	EShowcasePhase CurrentPhase;

	// When false (default), the showcase no longer drives the player pawn around the level
	// (no camera waypoint flythrough, no LOD-phase teleport, no interactive-phase reset).
	// The player keeps full manual control of their pawn throughout the demo.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	bool bEnableCameraFlythrough = false;

	// When true (default), RestartShowcase() enters directly at EShowcasePhase::Interactive instead of
	// Warmup, skipping the ~95s scripted phase sequence so sculpting/tool input is live immediately.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	bool bStartInInteractivePhase = true;

	UPROPERTY(Transient)
	TObjectPtr<class UUserWidget> ActiveUI;

	// Resets/(re)enters the showcase, respecting bStartInInteractivePhase (used on BeginPlay).
	UFUNCTION(BlueprintCallable, Category = "Showcase")
	void RestartShowcase();

	// Always (re)runs the full scripted phase sequence from Warmup, regardless of
	// bStartInInteractivePhase — this is what the panel's "Run Full Showcase" button calls.
	UFUNCTION(BlueprintCallable, Category = "Showcase")
	void RunScriptedShowcase();

	UFUNCTION(BlueprintCallable, Category = "Showcase")
	void StartPersistenceTest();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	float PhaseTimer;
	float ActionTimer;
	FVector OriginalPlayerPos;

	// Input State
	bool bIsClicking;
	bool bFlattenHeightLocked;
	bool bStrokeBegun = false;
	float LockedFlattenHeight;

	// Phase Config
	TMap<EShowcasePhase, FShowcasePhaseConfig> PhaseConfigs;
	TArray<EShowcasePhase> ShowcaseSequence;
	void InitPhaseConfigs();
	void InitShowcaseSequence();
	int32 GetPhaseIndex(EShowcasePhase Phase) const;

	// Camera Waypoint System
	TArray<FShowcaseCameraWaypoint> CameraTrack;
	int32 CameraWaypointIndex;
	float CameraWaypointTimer;
	bool bCameraInTransit;
	bool bCameraEnabled;
	FVector CameraTransitStartLocation;
	FRotator CameraTransitStartRotation;
	void SetCameraTrack(const TArray<FShowcaseCameraWaypoint>& Track);
	void UpdateCamera(float DeltaTime);

	// Overlay Helpers
	void SetOverlay(const FString& Title, const FString& Description, const FString& Detail = TEXT(""));
	void ClearOverlay();

	// World Bootstrap
	UPROPERTY(Transient)
	TObjectPtr<class UGRTWorldPreset> ShowcasePreset;

	bool bShowcaseBaselineCaptured = false;

	TWeakObjectPtr<class AGRTManager> BoundManager;
	void EnsureShowcaseWorld();
	class AGRTManager* GetManager() const;
	void RestartShowcaseInternal(bool bForceInteractive);
	class AGRTManager* EnsureShowcaseManager();
	class UGRTWorldPreset* BuildFallbackPreset();
	class AGRTChunk* EnsurePrimaryAuthoredChunk(class AGRTManager* Manager);
	void SeedFallbackTerrain(class AGRTManager* Manager);
	void EnsureFallbackFoliagePresentation(class AGRTManager* Manager, class AGRTChunk* Chunk);
	void EnsureShowcaseTerrainMaterial(class AGRTManager* Manager);
	void EnsureBasePopulation(class AGRTManager* Manager);
	FGuid FindPopulationIdByTypeId(const class AGRTManager* Manager, FName TypeId) const;
	void CaptureShowcaseBaseline();
	bool RestoreShowcaseBaseline();
	void BindManagerEvents(class AGRTManager* Manager);
	void UnbindManagerEvents();
	void ClearShowcaseTimers();

	// Phase Transition
	EShowcasePhase GetNextPhase(EShowcasePhase Current) const;
	void AdvancePhase();
	void OnPhaseEnter(EShowcasePhase Phase);

	// Phase Update Methods
	void UpdateAuthoredWorldDemo(float DeltaTime);
	void UpdateSculptingDemo(float DeltaTime);
	void UpdateLayerDemo(float DeltaTime);
	void UpdateUndoRedoDemo(float DeltaTime);
	void UpdatePopulationDemo(float DeltaTime);
	void UpdateProceduralWorldDemo(float DeltaTime);
	void UpdateGameplayHooksDemo(float DeltaTime);
	void UpdateDesignerWorkflowDemo(float DeltaTime);
	void UpdatePersistenceDemo(float DeltaTime);
	void UpdateReplicationDemo(float DeltaTime);
	void UpdateLODDemo(float DeltaTime);

	// Demo sub-state
	int32 UndoRedoDemoStep;
	float UndoRedoSubTimer;
	bool bPopulationRuntimePlacementTriggered;
	bool bPopulationHarvestTriggered;
	bool bPopulationDestroyTriggered;
	bool bPopulationRestoreTriggered;
	bool bGameplayPulseTriggered;
	FTimerHandle PersistenceMutationTimer;
	FTimerHandle PersistenceRestoreTimer;

	// Delegate telemetry
	int32 TerrainEventCount;
	int32 FoliageEventCount;
	int32 PopulationEventCount;
	FIntPoint LastTerrainEventCoord;
	int32 LastFoliageEventInstances;
	FName LastPopulationReason;
	FName LastPopulationTypeId;

	UFUNCTION()
	void HandleTerrainChanged(const FGRTTerrainChangeEvent& Event);

	UFUNCTION()
	void HandleFoliageChanged(const FGRTFoliageChangeEvent& Event);

	UFUNCTION()
	void HandlePopulationChanged(const FGRTPopulationChangeEvent& Event);

	/**
	 * Bound to the manager's OnWorldModeChosen. BeginPlay defers RestartShowcase() to this instead
	 * of calling it immediately when the world is still genuinely awaiting the player's Sandbox/
	 * Import choice -- see BeginPlay's own comment for why forcing the choice open here was wrong.
	 */
	UFUNCTION()
	void HandleWorldModeChosen();
};
