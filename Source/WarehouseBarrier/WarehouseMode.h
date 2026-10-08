#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WarehouseMode.generated.h"
struct FBarrierLayer
{
    int In = 0, Out = 0;
    TArray<double> W, B;
};
struct FBarrierNetwork
{
    TArray<FBarrierLayer> Layers;
    TArray<double> Run(TArray<double> X) const;
};
struct FWarehouseCargo
{
    int Id = 0, SKU = 0, Slot = -1;
    AActor *Actor = nullptr;
};
struct FWarehouseJob
{
    int Cargo = 0, Destination = -1;
    bool Sort = false;
};
UCLASS()
class WAREHOUSEBARRIER_API AWarehouseMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    AWarehouseMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

  private:
    friend class SBarrierDashboard;
    TSharedPtr<class SBarrierDashboard> Dashboard;
    FBarrierNetwork H, S, Tau;
    TArray<double> Env;
    FVector2D P, V, Goal;
    UPROPERTY() AActor *Robot = nullptr;
    UPROPERTY() class ACameraActor *ViewCamera = nullptr;
    bool FollowCamera = false;
    double CameraYaw = -50, CameraPitch = 30, CameraDistance = 2370;
    FVector2D PreviousRenderP;
    FRotator SimulationRotation, PreviousRenderRotation;
    FVector CameraAnchor, CameraOffset;
    bool CameraInitialized = false;
    int CameraCheckFPS = 0, CameraCheckFrames = 0, CameraMovingFrames = 0, CameraRepeatedFrames = 0;
    FVector LastDisplayPosition, LastCameraPosition;
    FQuat LastCameraRotation = FQuat::Identity;
    double CameraMaxStep = 0, DisplayMaxStep = 0, CameraMaxAngle = 0;
    double CargoHeight = 78, ForkReach = 25;
    void OrbitCamera(double X, double Y);
    void ZoomCamera(double Wheel);
    void ToggleFullscreen();
    bool IsFullscreen() const;
    void ExitWarehouse();
    void UpdateForks();
    bool Capture = false, Captured = false;
    bool Verify = false;
    bool DynamicScene = false;
    double CartSpeed = .25;
    FVector2D CartDirections = FVector2D(1, -1);
    TArray<double> InitialEnv;
    UPROPERTY() TArray<AActor *> Carts;
    UPROPERTY() TArray<AActor *> RackSigns;
    UPROPERTY() TArray<AActor *> LiftParts;
    bool WorkCapture = false;
    bool SortedCapture = false;
    double AfterCaptureTime = 0;
    double CaptureTime = 0;
    bool Loaded = false, Paused = false, Gate = false, Navigation = false;
    int Waypoint = 0;
    int Method = 1, Episode = 0;
    double Accumulator = 0, Elapsed = 0, Minimum = 0, Effort = 0, HValue = 0, TValue = 0;
    FString Status;
    AActor *Box(FVector Position, FVector Scale, FLinearColor Color,
                const TCHAR *Mesh = TEXT("/Engine/BasicShapes/Cube.Cube"));
    bool LoadModel();
    void Reset();
    double Margin(FVector2D Pos, const TArray<double> *Future = nullptr) const;
    TArray<double> AdvanceEnvironment(const TArray<double> &E, double Dt) const;
    FVector2D Brake(FVector2D Vel) const;
    void Step();
    void BuildWarehouse();
    void MoveObstacles(double Dt);
    void SetCartSpeed(double Speed);
    TArray<FWarehouseCargo> Cargo;
    TArray<FWarehouseJob> Jobs;
    TArray<FVector2D> Route;
    int JobIndex = 0, RouteIndex = 0, WorkPhase = 0, CompletedJobs = 0;
    bool AutoWork = false, SortAfter = false, OpsVerify = false;
    double WorkTime = 0, PhaseTime = 0, TravelDistance = 0, RouteAge = 0;
    FString WorkStatus = TEXT("等待调度"), LastEvent = TEXT("入库区有4件待上架货物");
    FVector TransferStart, TransferEnd;
    void BuildInventory();
    void ResetInventory();
    void StartWork(bool Organize);
    void BuildSortJobs();
    void DispatchJob();
    void UpdateWork(double Dt);
    void SaveInventory();
    FVector SlotPosition(int Slot) const;
    FVector2D CargoDock(int CargoId) const;
    FVector2D ShelfDock(int Slot) const;
    double PlanningMargin(FVector2D Pos) const;
    bool PlanRoute(FVector2D Target);
    FVector2D NavigationAction(FVector2D Target, double Dt);
    FString InventoryText() const;
};
