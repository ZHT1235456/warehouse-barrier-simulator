#include "WarehouseMode.h"
#include "BarrierDashboard.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
TArray<double> FBarrierNetwork::Run(TArray<double> X) const
{
    for (int k = 0; k < Layers.Num(); k++)
    {
        const auto &L = Layers[k];
        TArray<double> Y;
        Y.SetNumZeroed(L.Out);
        for (int i = 0; i < L.Out; i++)
        {
            Y[i] = L.B[i];
            for (int j = 0; j < L.In; j++)
                Y[i] += L.W[i * L.In + j] * X[j];
            if (k < Layers.Num() - 1)
                Y[i] = FMath::Tanh(Y[i]);
        }
        X = MoveTemp(Y);
    }
    return X;
}
AWarehouseMode::AWarehouseMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = nullptr;
}
AActor *AWarehouseMode::Box(FVector Pos, FVector Scale, FLinearColor Color, const TCHAR *Mesh)
{
    auto *A = GetWorld()->SpawnActor<AActor>();
    auto *C = NewObject<UStaticMeshComponent>(A);
    A->SetRootComponent(C);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->RegisterComponent();
    A->SetActorLocation(Pos);
    A->SetActorScale3D(Scale);
    auto *Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Warehouse.M_Warehouse"));
    if (!Mat)
        Mat =
            LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Mat)
    {
        auto *D = UMaterialInstanceDynamic::Create(Mat, A);
        D->SetVectorParameterValue(TEXT("Color"), Color);
        D->SetScalarParameterValue(TEXT("Roughness"), .72);
        C->SetMaterial(0, D);
    }
    return A;
}
bool AWarehouseMode::LoadModel()
{
    FString Text;
    auto Path = FPaths::ProjectContentDir() / TEXT("Models/model.json");
    if (!FFileHelper::LoadFileToString(Text, *Path))
    {
        Status = TEXT("Model missing: build and train first");
        return false;
    }
    TSharedPtr<FJsonObject> J;
    auto R = TJsonReaderFactory<>::Create(Text);
    if (!FJsonSerializer::Deserialize(R, J) || !J.IsValid())
    {
        Status = TEXT("Invalid model JSON");
        return false;
    }
    J->TryGetBoolField(TEXT("dynamic"), DynamicScene);
    const int ED = DynamicScene ? 30 : 18;
    if (J->GetBoolField(TEXT("paper")) || J->GetIntegerField(TEXT("environment_dim")) != ED ||
        J->GetIntegerField(TEXT("schema")) != 1)
    {
        Status = TEXT("Requires warehouse schema 1 model");
        return false;
    }
    auto Parse = [&](const TCHAR *Name, FBarrierNetwork &N, int Input, int Output) {
        N.Layers.Empty();
        const TArray<TSharedPtr<FJsonValue>> *Array = nullptr;
        if (!J->TryGetArrayField(Name, Array) || Array->Num() != 3)
            return false;
        int Previous = Input;
        for (auto Value : *Array)
        {
            auto O = Value->AsObject();
            if (!O.IsValid())
                return false;
            FBarrierLayer L;
            L.In = O->GetIntegerField(TEXT("in"));
            L.Out = O->GetIntegerField(TEXT("out"));
            if (L.In != Previous || L.Out <= 0 || L.Out > 1024)
                return false;
            for (auto W : O->GetArrayField(TEXT("weight")))
                L.W.Add(W->AsNumber());
            for (auto B : O->GetArrayField(TEXT("bias")))
                L.B.Add(B->AsNumber());
            if (L.W.Num() != L.In * L.Out || L.B.Num() != L.Out)
                return false;
            Previous = L.Out;
            N.Layers.Add(MoveTemp(L));
        }
        return Previous == Output;
    };
    if (!Parse(TEXT("barrier"), H, ED + 4, 1) || !Parse(TEXT("switch"), S, ED + 4, 2) ||
        !Parse(TEXT("threshold"), Tau, ED, 1))
    {
        Status = TEXT("Invalid network dimensions");
        return false;
    }
    FString FixtureText;
    if (FFileHelper::LoadFileToString(FixtureText, *(FPaths::ProjectContentDir() / TEXT("Models/parity.json"))))
    {
        TArray<TSharedPtr<FJsonValue>> Cases;
        auto Reader = TJsonReaderFactory<>::Create(FixtureText);
        if (!FJsonSerializer::Deserialize(Reader, Cases))
        {
            Status = TEXT("Invalid parity fixture");
            return false;
        }
        double MaxError = 0;
        for (auto Case : Cases)
        {
            auto O = Case->AsObject();
            TArray<double> XX, EE;
            for (auto Val : O->GetArrayField(TEXT("input")))
                XX.Add(Val->AsNumber());
            for (auto Val : O->GetArrayField(TEXT("env")))
                EE.Add(Val->AsNumber());
            if (XX.Num() != ED + 4 || EE.Num() != ED)
            {
                Status = TEXT("Invalid parity input dimensions");
                return false;
            }
            auto Check = [&](const TCHAR *Field, TArray<double> Result) {
                auto Expected = O->GetArrayField(Field);
                if (Expected.Num() != Result.Num())
                    return false;
                for (int i = 0; i < Result.Num(); i++)
                    MaxError = FMath::Max(MaxError, FMath::Abs(Result[i] - Expected[i]->AsNumber()));
                return true;
            };
            if (!Check(TEXT("h"), H.Run(XX)) || !Check(TEXT("s"), S.Run(XX)) || !Check(TEXT("tau"), Tau.Run(EE)))
            {
                Status = TEXT("Invalid parity output dimensions");
                return false;
            }
        }
        if (MaxError > 1e-4)
        {
            Status = TEXT("Inference parity failed");
            UE_LOG(LogTemp, Error, TEXT("Inference parity error %.9f"), MaxError);
            return false;
        }
        UE_LOG(LogTemp, Display, TEXT("LibTorch/UE inference parity passed: max_abs_error=%.9f"), MaxError);
    }
    Status = J->GetBoolField(TEXT("smoke")) ? TEXT("SMOKE MODEL: pipeline check only")
                                            : (DynamicScene ? TEXT("动态仓库模型已载入") : TEXT("静态仓库模型已载入"));
    UE_LOG(LogTemp, Display, TEXT("Barrier model loaded: %s"), *Path);
    return true;
}
void AWarehouseMode::BeginPlay()
{
    Super::BeginPlay();
    Env = {-4, -2, .75, 0, -2, .75, 4, -2, .75, -4, 2, .75, 0, 2, .75, 4, 2, .75};
    Loaded = LoadModel();
    Verify = FParse::Param(FCommandLine::Get(), TEXT("BarrierVerify"));
    Navigation = !FParse::Param(FCommandLine::Get(), TEXT("PaperRecovery"));
    if (DynamicScene)
    {
        Env.SetNumZeroed(30);
        Env[12] = 0;
        Env[13] = 0;
        Env[14] = .45;
        Env[15] = 6;
        Env[16] = 0;
        Env[17] = .45;
        Env[26] = CartSpeed;
        Env[29] = -CartSpeed;
    }
    InitialEnv = Env;
    double SpeedOverride = -1;
    FParse::Value(FCommandLine::Get(), TEXT("BarrierSpeed="), SpeedOverride);
    if (SpeedOverride >= 0)
        SetCartSpeed(FMath::Clamp(SpeedOverride, 0., .6));
    FParse::Value(FCommandLine::Get(), TEXT("BarrierMethod="), Method);
    Method = FMath::Clamp(Method, 0, 2);
    BuildWarehouse();
    BuildInventory();
    Reset();
    OpsVerify = FParse::Param(FCommandLine::Get(), TEXT("OpsVerify"));
    if (OpsVerify)
    {
        Verify = true;
        StartWork(false);
        SortAfter = true;
    }
    if (!OpsVerify && Navigation)
        Paused = true;
    if (GEngine && GEngine->GameViewport)
    {
        SAssignNew(Dashboard, SBarrierDashboard).Mode(this);
        GEngine->GameViewport->AddViewportWidgetContent(Dashboard.ToSharedRef(), 10);
        if (auto *PC = GetWorld()->GetFirstPlayerController())
        {
            PC->bShowMouseCursor = true;
            PC->SetInputMode(FInputModeGameAndUI());
        }
        int Page = 0;
        FParse::Value(FCommandLine::Get(), TEXT("BarrierPage="), Page);
        Dashboard->SetPage(FMath::Clamp(Page, 0, 3));
    }
    Capture = FParse::Param(FCommandLine::Get(), TEXT("BarrierCapture"));
    FollowCamera = FParse::Param(FCommandLine::Get(), TEXT("BarrierFollow"));
    FParse::Value(FCommandLine::Get(), TEXT("CameraCheckFPS="), CameraCheckFPS);
    if (CameraCheckFPS > 0)
    {
        FollowCamera = true;
        StartWork(false);
    }
    if (Capture)
        Paused = true;
    WorkCapture = FParse::Param(FCommandLine::Get(), TEXT("WorkCapture"));
    if (WorkCapture)
    {
        StartWork(false);
    }
    SortedCapture = FParse::Param(FCommandLine::Get(), TEXT("SortedCapture"));
    if (SortedCapture)
    {
        StartWork(false);
        SortAfter = true;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("CartSpeedVerify")))
    {
        int Failures = 0, Changes = 0;
        for (int Scenario = 0; Scenario < 2; ++Scenario)
        {
            SetCartSpeed(.3);
            if (Scenario == 0) MoveObstacles(2);
            else
            {
                Env[12] = 6.99; Env[16] = -4.99;
                Env[26] = .3; Env[29] = -.3;
                MoveObstacles(.2);
            }
            const double DirectionX = FMath::Sign(Env[26]), DirectionY = FMath::Sign(Env[29]);
            for (double Speed : {.45, 0., .15, .6, 0., .3})
            {
                const auto Before = Env;
                const FVector A = Carts[0]->GetActorLocation(), B = Carts[1]->GetActorLocation();
                SetCartSpeed(Speed);
                ++Changes;
                for (int Position = 0; Position < 18; ++Position) Failures += Env[Position] != Before[Position];
                Failures += !FMath::IsNearlyEqual(Env[26], DirectionX * Speed);
                Failures += !FMath::IsNearlyEqual(Env[29], DirectionY * Speed);
                Failures += Carts[0]->GetActorLocation() != A || Carts[1]->GetActorLocation() != B;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("CART_SPEED_CHECK changes=%d failures=%d position_reset=0 stop_resume_direction=preserved"), Changes, Failures);
        FPlatformMisc::RequestExit(false);
    }
}
void AWarehouseMode::BuildWarehouse()
{
    const FLinearColor Steel(.025, .065, .10), Orange(.9, .22, .025), Concrete(.25, .29, .32), Wood(.42, .24, .10),
        Cardboard(.52, .34, .17), Yellow(.95, .65, .035);
    if (auto *Backdrop = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Backdrop.M_Backdrop")))
    {
        auto *Dome =
            Box(FVector::ZeroVector, FVector(500), FLinearColor::White, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        auto *Component = Cast<UStaticMeshComponent>(Dome->GetRootComponent());
        Component->SetMaterial(0, Backdrop);
        Component->SetCastShadow(false);
    }
    Box(FVector(0, 0, -12), FVector(16.4, 12.4, .24), Concrete);
    // Floor expansion joints and painted traffic lanes.
    for (int x = -700; x < 800; x += 100)
        Box(FVector(x, 0, .3), FVector(.007, 12, .006), FLinearColor(.14, .15, .16));
    for (int y = -500; y < 600; y += 100)
        Box(FVector(0, y, .3), FVector(16, .007, .006), FLinearColor(.14, .15, .16));
    for (double y : {-450., 0., 450.})
        for (int x = -730; x < 740; x += 110)
            Box(FVector(x, y, 1), FVector(.62, .045, .015), Yellow);
    for (double y : {-580., 580.})
        Box(FVector(0, y, 1), FVector(15.6, .05, .02), Yellow);
    for (double x : {-780., 780.})
        Box(FVector(x, 0, 1), FVector(.05, 11.6, .02), Yellow);
    // Six storage bays, all visible footprints inside their radius .75 m envelope.
    for (int i = 0; i < (DynamicScene ? 4 : 6); i++)
    {
        const double X = Env[3 * i] * 100, Y = Env[3 * i + 1] * 100;
        Box(FVector(X, Y, .6), FVector(1.38, 1.38, .009), FLinearColor(.07, .19, .22));
        for (double Side : {-67., 67.})
        {
            Box(FVector(X + Side, Y, 1.2), FVector(.025, 1.36, .008), FLinearColor(.33, .58, .59));
            Box(FVector(X, Y + Side, 1.2), FVector(1.36, .025, .008), FLinearColor(.33, .58, .59));
        }
        for (double dx : {-49., 49.})
            for (double dy : {-49., 49.})
                Box(FVector(X + dx, Y + dy, 145), FVector(.045, .045, 2.9), Steel);
        for (double z : {15., 105., 205., 290.})
        {
            for (double dy : {-49., 49.})
                Box(FVector(X, Y + dy, z), FVector(1.03, .05, .075), Orange);
            Box(FVector(X, Y, z - 4), FVector(1.02, 1.02, .025), Steel);
        }
    }
    // Rear and side walls: front wall low enough for the view into the warehouse.
    if (DynamicScene)
    {
        for (int i = 4; i < 6; i++)
        {
            double X = Env[3 * i] * 100, Y = Env[3 * i + 1] * 100;
            auto *Cart = Box(FVector(X, Y, 22), FVector(.55, .7, .16),
                             i == 4 ? FLinearColor(.8f, .18f, .04f) : FLinearColor(.12f, .36f, .48f));
            Carts.Add(Cart);
            auto CartPart = [&](FVector Local, FVector Size, FLinearColor Color,
                                const TCHAR *Mesh = TEXT("/Engine/BasicShapes/Cube.Cube")) {
                auto *A = Box(FVector(X, Y, 22) + Local, Size, Color, Mesh);
                A->AttachToActor(Cart, FAttachmentTransformRules::KeepWorldTransform);
                return A;
            };
            CartPart(FVector(0, 0, 28), FVector(.46, .59, .40), Cardboard);
            CartPart(FVector(0, 0, 49), FVector(.045, .59, .015), Wood);
            for (double x : {-20., 20.})
                for (double y : {-28., 28.})
                {
                    auto *W = CartPart(FVector(x, y, -11), FVector(.12, .12, .08), Steel,
                                       TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
                    W->SetActorRotation(FRotator(90, 0, 0));
                }
            CartPart(FVector(-21, 0, 39), FVector(.035, .60, .60), Steel);
        }
    }
    Box(FVector(0, 608, 180), FVector(16.4, .16, 3.6), FLinearColor(.52, .55, .57));
    for (double x : {-808., 808.})
        Box(FVector(x, 0, 90), FVector(.16, 12.2, 1.8), FLinearColor(.45, .48, .5));
    Box(FVector(0, -608, 24), FVector(16.4, .16, .48), Steel);
    for (int x = -750; x <= 750; x += 300)
    {
        Box(FVector(x, 594, 180), FVector(.14, .12, 3.6), Steel);
        Box(FVector(x, 590, 65), FVector(.3, .12, .9), Yellow);
    }
    // Loading door and high windows.
    Box(FVector(590, 597, 145), FVector(2.4, .025, 2.7), Steel);
    for (int z = 25; z < 275; z += 18)
        Box(FVector(590, 593, z), FVector(2.36, .025, .025), FLinearColor(.28, .32, .34));
    for (int x = -590; x < 300; x += 240)
    {
        Box(FVector(x, 594, 285), FVector(1.8, .03, .65), Steel);
        Box(FVector(x, 590, 285), FVector(1.65, .015, .5), FLinearColor(.23, .42, .52));
    }
    // Suspended lighting fixtures, with warm industrial illumination.
    for (int x = -500; x <= 500; x += 500)
        for (int y = -340; y <= 340; y += 680)
        {
            Box(FVector(x, y, 375), FVector(1.3, .24, .07), Steel);
            auto *L = GetWorld()->SpawnActor<APointLight>(FVector(x, y, 345), FRotator::ZeroRotator);
            auto *LC = Cast<UPointLightComponent>(L->GetLightComponent());
            LC->SetIntensity(11000);
            LC->SetSourceRadius(35);
            LC->SetSoftSourceRadius(60);
            LC->SetAttenuationRadius(1000);
            LC->SetLightColor(FLinearColor(1, .88, .73));
            LC->SetCastShadows(true);
        }
    auto *Sun = GetWorld()->SpawnActor<ADirectionalLight>();
    Sun->SetActorRotation(FRotator(-50, -35, 0));
    Sun->GetLightComponent()->SetIntensity(2.0);
    auto *Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetIntensity(.7);
    Sky->GetLightComponent()->SetLightColor(FLinearColor(.68, .78, 1));
    // Compact autonomous pallet truck. Child pieces follow the rigid planar state.
    Robot = Box(FVector::ZeroVector, FVector(.34, .4, .17), Yellow);
    auto Part = [&](FVector Local, FVector Size, FLinearColor Color,
                    const TCHAR *Mesh = TEXT("/Engine/BasicShapes/Cube.Cube")) {
        auto *A = Box(Local * .8, Size * .8, Color, Mesh);
        A->AttachToActor(Robot, FAttachmentTransformRules::KeepWorldTransform);
        return A;
    };
    Part(FVector(-6, 0, 14), FVector(.21, .32, .20), Steel);
    Part(FVector(-40, 0, 165), FVector(.045, .27, 3.1), Steel);
    LiftParts.Add(Part(FVector(-40, 0, 64), FVector(.045, .30, .07), Steel));
    for (double y : {-11., 11.})
    {
        LiftParts.Add(Part(FVector(25, y, 64), FVector(.90, .035, .028), Steel));
        Part(FVector(-40, y, 165), FVector(.035, .025, 3.1), Orange);
    }
    for (double x : {-10., 9.})
        for (double y : {-18., 18.})
        {
            auto *Wheel = Part(FVector(x, y, -6), FVector(.105, .105, .055), FLinearColor(.025, .026, .03),
                               TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
            Wheel->SetActorRotation(FRotator(90, 0, 0));
        }
    Part(FVector(-7, 0, 28), FVector(.13, .22, .12), Yellow);
    Part(FVector(-7, 0, 37), FVector(.10, .10, .035), Steel, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Part(FVector(-12, 0, 46), FVector(.03, .03, .06), Orange, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto *Label = GetWorld()->SpawnActor<AActor>();
    auto *Text = NewObject<UTextRenderComponent>(Label);
    Label->SetRootComponent(Text);
    Text->SetText(FText::FromString(TEXT("ZONE A  /  AUTONOMOUS LOGISTICS")));
    Text->SetWorldSize(32);
    Text->SetTextRenderColor(FColor(40, 50, 60));
    Text->RegisterComponent();
    Label->SetActorLocation(FVector(-320, 582, 345));
    Label->SetActorRotation(FRotator(0, -90, 0));
    for (int Rack = 0; Rack < 4; Rack++)
    {
        auto *Sign = GetWorld()->SpawnActor<AActor>();
        auto *Caption = NewObject<UTextRenderComponent>(Sign);
        Sign->SetRootComponent(Caption);
        Caption->SetText(FText::FromString(FString::Chr(TCHAR('A' + Rack))));
        Caption->SetHorizontalAlignment(EHTA_Center);
        Caption->SetVerticalAlignment(EVRTA_TextCenter);
        Caption->SetWorldSize(44);
        Caption->SetTextRenderColor(FColor::White);
        Caption->RegisterComponent();
        Sign->SetActorLocation(FVector(Env[Rack * 3] * 100, Env[Rack * 3 + 1] * 100 - 58, 355));
        Sign->SetActorRotation((FVector(1300, -1550, 1250) - Sign->GetActorLocation()).Rotation());
        const FVector Normal = Sign->GetActorForwardVector();
        auto *Rim = Box(Sign->GetActorLocation() - Normal * 3.5, FVector(.025, .64, .68), Orange);
        auto *Plate = Box(Sign->GetActorLocation() - Normal * 2, FVector(.025, .58, .62), Steel);
        for (auto *SignPart : {Rim, Plate})
        {
            SignPart->SetActorRotation(Sign->GetActorRotation());
            SignPart->AttachToActor(Sign, FAttachmentTransformRules::KeepWorldTransform);
            if (auto *Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RackSign.M_RackSign")))
            {
                auto *Instance = UMaterialInstanceDynamic::Create(Material, SignPart);
                Instance->SetVectorParameterValue(TEXT("Color"), SignPart == Plate ? FLinearColor(.012f, .028f, .049f) : Orange);
                Cast<UStaticMeshComponent>(SignPart->GetRootComponent())->SetMaterial(0, Instance);
            }
        }
        RackSigns.Add(Sign);
    }
    ViewCamera = GetWorld()->SpawnActor<ACameraActor>();
    auto &Post = ViewCamera->GetCameraComponent()->PostProcessSettings;
    Post.bOverride_VignetteIntensity = true;
    Post.VignetteIntensity = .18f;
    Post.bOverride_AmbientOcclusionIntensity = true;
    Post.AmbientOcclusionIntensity = .65f;
    ViewCamera->GetCameraComponent()->SetFieldOfView(58);
    ViewCamera->SetActorLocation(FVector(1300, -1550, 1250));
    ViewCamera->SetActorRotation((FVector(0, 0, 70) - ViewCamera->GetActorLocation()).Rotation());
    if (auto *PC = GetWorld()->GetFirstPlayerController())
        PC->SetViewTarget(ViewCamera);
}
double AWarehouseMode::Margin(FVector2D Pos, const TArray<double> *Future) const
{
    const auto &E = Future ? *Future : Env;
    double Radius = AutoWork ? .38 : .3;
    for (auto Item : Cargo)
        if (Item.Slot == -2)
            Radius = .85;
    double C = FMath::Min(8 - Radius - FMath::Abs(Pos.X), 6 - Radius - FMath::Abs(Pos.Y));
    for (int i = 0; i < 6; i++)
        C = FMath::Min(C, (Pos - FVector2D(E[3 * i], E[3 * i + 1])).Size() - E[3 * i + 2] - Radius);
    for (auto Item : Cargo)
        if (Item.Slot == -1)
            C = FMath::Min(C, (Pos - FVector2D(-6.2, -4.5)).Size() - .56 - Radius);
    return C;
}
TArray<double> AWarehouseMode::AdvanceEnvironment(const TArray<double> &E, double Dt) const
{
    auto Next = E;
    if (!DynamicScene)
        return Next;
    for (int i = 4; i < 6; i++)
        for (int j = 0; j < 2; j++)
        {
            int Position = 3 * i + j, Velocity = 18 + 2 * i + j;
            double Bound = j == 0 ? 7 : 5;
            Next[Position] += Dt * E[Velocity];
            if (FMath::Abs(Next[Position]) > Bound)
            {
                Next[Position] = Next[Position] > Bound ? 2 * Bound - Next[Position] : -2 * Bound - Next[Position];
                Next[Velocity] = -E[Velocity];
            }
        }
    return Next;
}
void AWarehouseMode::MoveObstacles(double Dt)
{
    if (!DynamicScene)
        return;
    Env = AdvanceEnvironment(Env, Dt);
    for (int k = 0; k < Carts.Num(); k++)
    {
        int i = k + 4;
        Carts[k]->SetActorLocation(FVector(Env[3 * i] * 100, Env[3 * i + 1] * 100, 22));
    }
}
void AWarehouseMode::SetCartSpeed(double Speed)
{
    if (!DynamicScene)
        return;
    CartSpeed = FMath::Clamp(Speed, 0., .6);
    if (!FMath::IsNearlyZero(Env[26]))
        CartDirections.X = FMath::Sign(Env[26]);
    if (!FMath::IsNearlyZero(Env[29]))
        CartDirections.Y = FMath::Sign(Env[29]);
    Env[26] = CartDirections.X * CartSpeed;
    Env[29] = CartDirections.Y * CartSpeed;
    InitialEnv[26] = CartSpeed;
    InitialEnv[29] = -CartSpeed;
}
FVector2D AWarehouseMode::Brake(FVector2D Vel) const
{
    return FVector2D(FMath::Clamp(-2 * Vel.X, -1., 1.), FMath::Clamp(-2 * Vel.Y, -1., 1.));
}
void AWarehouseMode::Reset()
{
    Episode++;
    ResetInventory();
    Env = InitialEnv;
    CartDirections = FVector2D(1, -1);
    Waypoint = 0;
    P = Navigation ? FVector2D(-6.8, -3.1) : FVector2D(-6.1, -2.3);
    PreviousRenderP = P;
    SimulationRotation = PreviousRenderRotation = FRotator::ZeroRotator;
    V = Navigation ? FVector2D::ZeroVector : FVector2D(1.5, .15);
    Goal = FVector2D(7.1, 4.5);
    Elapsed = Effort = Accumulator = 0;
    Minimum = Margin(P);
    MoveObstacles(0);
    if (Robot)
    {
        Robot->SetActorLocation(FVector(P.X * 100, P.Y * 100, 18));
        Robot->SetActorRotation(FRotator(0, 0, 0));
    }
}
void AWarehouseMode::Step()
{
    const double Dt = .05;
    PreviousRenderP = P;
    PreviousRenderRotation = SimulationRotation;
    Robot->SetActorLocation(FVector(P.X * 100, P.Y * 100, 18));
    Robot->SetActorRotation(SimulationRotation);
    auto Old = P;
    auto U = Brake(V);
    Gate = false;
    TArray<double> X = {P.X, P.Y, V.X, V.Y};
    X.Append(Env);
    if (Loaded)
    {
        HValue = H.Run(X)[0];
        TValue = Tau.Run(Env)[0];
        Gate = Method == 2 || (Method == 1 && HValue < TValue);
        if (Gate)
        {
            auto Z = S.Run(X);
            U = FVector2D(Z[0] >= 0 ? 1 : -1, Z[1] >= 0 ? 1 : -1);
        }
    }
    if (AutoWork)
    {
        UpdateWork(Dt);
        SimulationRotation = Robot->GetActorRotation();
        if (Paused)
            return;
        U = (WorkPhase == 1 || WorkPhase == 3) ? NavigationAction(Goal, Dt) : Brake(V);
        if (RouteIndex < Route.Num())
            DrawDebugLine(GetWorld(), FVector(P.X * 100, P.Y * 100, 5), FVector(Goal.X * 100, Goal.Y * 100, 5),
                          FColor(45, 140, 145), false, .1, 0, 2);
    }
    else if (Navigation)
    {
        if (Waypoint == 0)
            Goal = FVector2D(7.1, -4.5);
        else
            Goal = FVector2D(7.1, 4.5);
        if (Waypoint == 0 && (P - Goal).Size() < .45)
        {
            Waypoint = 1;
            Goal = FVector2D(7.1, 4.5);
        }
        auto Desired = (Goal - P) * .8 - V * 1.5;
        Desired.X = FMath::Clamp(Desired.X, -1., 1.);
        Desired.Y = FMath::Clamp(Desired.Y, -1., 1.);
        auto PP = P;
        auto VV = V;
        auto Future = Env;
        bool Safe = true;
        for (int k = 0; k < 60; k++)
        {
            auto A = k == 0 ? Desired : Brake(VV);
            PP += VV * Dt + A * (.5 * Dt * Dt);
            VV += A * Dt;
            Future = AdvanceEnvironment(Future, Dt);
            if (Margin(PP, &Future) < .15)
            {
                Safe = false;
                break;
            }
        }
        if (Safe)
        {
            U = Desired;
            Gate = false;
        }
    }
    P += V * Dt + U * (.5 * Dt * Dt);
    if (AutoWork)
        TravelDistance += (P - Old).Size();
    V += U * Dt;
    Elapsed += Dt;
    Effort += U.SizeSquared() * Dt;
    MoveObstacles(Dt);
    int StackLevel = 0;
    for (auto &Item : Cargo)
        if (Item.Slot == -1)
        {
            if (AutoWork && WorkPhase == 2 && Jobs.IsValidIndex(JobIndex) && Jobs[JobIndex].Cargo == Item.Id)
                continue;
            FVector Target(-620, -450, 25 + 75 * StackLevel++);
            Item.Actor->SetActorLocation(FMath::VInterpTo(Item.Actor->GetActorLocation(), Target, Dt, 8));
        }
    Minimum = FMath::Min(Minimum, Margin(P));
    Robot->SetActorLocation(FVector(P.X * 100, P.Y * 100, 18));
    if (V.Size() > .05 && WorkPhase != 2 && WorkPhase != 4)
        Robot->SetActorRotation(FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)), 0));
    UpdateForks();
    SimulationRotation = Robot->GetActorRotation();
    DrawDebugLine(GetWorld(), FVector(Old.X * 100, Old.Y * 100, 3), FVector(P.X * 100, P.Y * 100, 3),
                  Gate ? FColor::Red : FColor::Cyan, false, 20, 0, 2);
    if (Minimum < 0 || (AutoWork ? WorkTime > 900 : Elapsed > (Navigation ? 30 : 3)) ||
        (!AutoWork && Navigation && Waypoint == 1 && (P - Goal).Size() < .4))
    {
        UE_LOG(LogTemp, Display,
               TEXT("Episode=%d method=%d navigation=%d min_margin=%.4f effort=%.4f px=%.3f py=%.3f time=%.2f"),
               Episode, Method, Navigation, Minimum, Effort, P.X, P.Y, Elapsed);
        Paused = true;
        if (AutoWork)
        {
            WorkStatus = TEXT("作业暂停 · 重新调度");
            UE_LOG(LogTemp, Error, TEXT("OPS_STOP phase=%d job=%d margin=%.5f px=%.3f py=%.3f time=%.2f"), WorkPhase,
                   JobIndex, Minimum, P.X, P.Y, WorkTime);
        }
    }
}
bool AWarehouseMode::IsFullscreen() const
{
    return GEngine && GEngine->GetGameUserSettings() &&
           GEngine->GetGameUserSettings()->GetFullscreenMode() != EWindowMode::Windowed;
}
void AWarehouseMode::ToggleFullscreen()
{
    if (GEngine && GEngine->GetGameUserSettings())
    {
        auto *Settings = GEngine->GetGameUserSettings();
        Settings->SetFullscreenMode(IsFullscreen() ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen);
        Settings->ApplyResolutionSettings(false);
    }
}
void AWarehouseMode::ExitWarehouse()
{
    SaveInventory();
    FPlatformMisc::RequestExit(false);
}
void AWarehouseMode::OrbitCamera(double X, double Y)
{
    CameraYaw -= X * .3;
    CameraPitch = FMath::Clamp(CameraPitch + Y * .25, 12., 78.);
}
void AWarehouseMode::ZoomCamera(double Wheel)
{
    CameraDistance = FMath::Clamp(CameraDistance * FMath::Pow(.88, Wheel), 550., 4000.);
}
void AWarehouseMode::UpdateForks()
{
    if (!Robot || LiftParts.Num() != 3)
        return;
    if (WorkPhase != 2 && WorkPhase != 4)
    {
        bool Carrying = false;
        for (const auto &Item : Cargo)
            Carrying |= Item.Slot == -2;
        CargoHeight = Carrying ? 78 : 40;
        ForkReach = Carrying ? 25 : -7;
    }
    const FVector Forward = Robot->GetActorForwardVector(), Right = Robot->GetActorRightVector();
    FVector Base = Robot->GetActorLocation();
    LiftParts[0]->SetActorLocation(Base - Forward * 32 + FVector(0, 0, CargoHeight - Base.Z - 2.4));
    for (int I = 1; I < 3; ++I)
    {
        double Length = ForkReach + 32 + 30;
        FVector Position = Base + Forward * (-32 + Length * .5) + Right * (I == 1 ? -8.8 : 8.8);
        Position.Z = CargoHeight - 2.4;
        LiftParts[I]->SetActorLocation(Position);
        LiftParts[I]->SetActorScale3D(FVector(Length / 100., .028, .0224));
    }
}
void AWarehouseMode::Tick(float Delta)
{
    Super::Tick(Delta);
    if (CameraCheckFPS > 0)
        Delta = 1.f / CameraCheckFPS;
    auto *PC = GetWorld()->GetFirstPlayerController();
    if (PC)
    {
        if (PC->WasInputKeyJustPressed(EKeys::F11))
            ToggleFullscreen();
        if (PC->WasInputKeyJustPressed(EKeys::Escape))
        {
            ExitWarehouse();
            return;
        }
        if (PC->WasInputKeyJustPressed(EKeys::R))
        {
            Reset();
            Paused = false;
        }
        if (PC->WasInputKeyJustPressed(EKeys::SpaceBar))
            Paused = !Paused;
        if (PC->WasInputKeyJustPressed(EKeys::N))
        {
            Navigation = !Navigation;
            Reset();
            Paused = false;
        }
        if (PC->WasInputKeyJustPressed(EKeys::One))
        {
            Method = 0;
            Reset();
            Paused = false;
        }
        if (PC->WasInputKeyJustPressed(EKeys::Two))
        {
            Method = 1;
            Reset();
            Paused = false;
        }
        if (PC->WasInputKeyJustPressed(EKeys::Three))
        {
            Method = 2;
            Reset();
            Paused = false;
        }
    }
    if (PC && PC->WasInputKeyJustPressed(EKeys::C))
        FollowCamera = !FollowCamera;
    if (Capture)
    {
        CaptureTime += Delta;
        if (CaptureTime > 8 && !Captured && PC && (!SortedCapture || !AutoWork))
        {
            PC->ConsoleCommand(TEXT("Shot SHOWUI"));
            Captured = true;
        }
        if (Captured)
            AfterCaptureTime += Delta;
        if (AfterCaptureTime > 3)
            FPlatformMisc::RequestExit(false);
    }
    if (WorkCapture && WorkTime >= 6)
    {
        Paused = true;
        WorkCapture = false;
    }
    if (!Paused)
    {
        Accumulator +=
            (OpsVerify || WorkCapture || (SortedCapture && AutoWork)) ? 1.0 : (Verify ? .05 : FMath::Min(Delta, .25f));
        while (Accumulator >= .05)
        {
            Accumulator -= .05;
            Step();
            if (Paused)
                break;
        }
    }
    // Interpolate fixed-step simulation for display; navigation remains at 20 Hz.
    const bool FastSimulation = OpsVerify || WorkCapture || SortedCapture || Verify;
    const double Alpha = Paused || FastSimulation ? 1. : FMath::Clamp(Accumulator / .05, 0., 1.);
    const FVector2D DisplayP = FMath::Lerp(PreviousRenderP, P, Alpha);
    Robot->SetActorLocation(FVector(DisplayP.X * 100, DisplayP.Y * 100, 18));
    Robot->SetActorRotation(FQuat::Slerp(PreviousRenderRotation.Quaternion(), SimulationRotation.Quaternion(), Alpha));
    if (ViewCamera)
    {
        const FVector Target = FollowCamera ? FVector(DisplayP.X * 100, DisplayP.Y * 100, 25) : FVector(0, 0, 70);
        const double Radius = FollowCamera ? FMath::Clamp(CameraDistance * .28, 250., 1200.) : CameraDistance;
        const double Pitch = FMath::DegreesToRadians(CameraPitch), Yaw = FMath::DegreesToRadians(CameraYaw);
        const FVector Offset =
            FVector(FMath::Cos(Pitch) * FMath::Cos(Yaw), FMath::Cos(Pitch) * FMath::Sin(Yaw), FMath::Sin(Pitch)) *
            Radius;
        if (!CameraInitialized)
        {
            CameraAnchor = Target;
            CameraOffset = Offset;
            CameraInitialized = true;
        }
        const double FollowAlpha = 1. - FMath::Exp(-8. * FMath::Max(0.f, Delta));
        const double OrbitAlpha = 1. - FMath::Exp(-10. * FMath::Max(0.f, Delta));
        CameraAnchor = FMath::Lerp(CameraAnchor, Target, FollowAlpha);
        CameraOffset = FMath::Lerp(CameraOffset, Offset, OrbitAlpha);
        ViewCamera->SetActorLocation(CameraAnchor + CameraOffset);
        ViewCamera->SetActorRotation((-CameraOffset).Rotation());
        for (auto *Sign : RackSigns)
            Sign->SetActorRotation((ViewCamera->GetActorLocation() - Sign->GetActorLocation()).Rotation());
        if (CameraCheckFPS > 0)
        {
            const FVector DisplayPosition = Robot->GetActorLocation(), CameraPosition = ViewCamera->GetActorLocation();
            const FQuat CameraRotation = ViewCamera->GetActorQuat();
            if (CameraCheckFrames > 0)
            {
                DisplayMaxStep = FMath::Max(DisplayMaxStep, FVector::Distance(DisplayPosition, LastDisplayPosition));
                CameraMaxStep = FMath::Max(CameraMaxStep, FVector::Distance(CameraPosition, LastCameraPosition));
                CameraMaxAngle = FMath::Max(
                    CameraMaxAngle, FMath::RadiansToDegrees(CameraRotation.AngularDistance(LastCameraRotation)));
                if (V.Size() > .1)
                {
                    ++CameraMovingFrames;
                    if (FVector::Distance(DisplayPosition, LastDisplayPosition) < .0001)
                        ++CameraRepeatedFrames;
                }
            }
            LastDisplayPosition = DisplayPosition;
            LastCameraPosition = CameraPosition;
            LastCameraRotation = CameraRotation;
            if (++CameraCheckFrames >= CameraCheckFPS * 12)
            {
                UE_LOG(LogTemp, Display,
                       TEXT("CAMERA_CHECK fps=%d moving_frames=%d repeated=%d max_display_cm=%.4f max_camera_cm=%.4f "
                            "max_angle_deg=%.6f"),
                       CameraCheckFPS, CameraMovingFrames, CameraRepeatedFrames, DisplayMaxStep, CameraMaxStep,
                       CameraMaxAngle);
                FPlatformMisc::RequestExit(false);
            }
        }
    }
    if (Verify && Paused)
        FPlatformMisc::RequestExit(false);
    if (GEngine && !Dashboard.IsValid())
    {
        GEngine->AddOnScreenDebugMessage(
            1, 0, FColor::White,
            FString::Printf(
                TEXT("WAREHOUSE | %s | %s\n1 Brake  2 Barrier switch  3 Full boundary | N Navigation | R Reset | Space "
                     "Pause\nMode %d  t %.2f  min margin %.3f m  effort %.3f  h %.3f  tau %.3f | %s"),
                *Status, Navigation ? TEXT("Task navigation extension") : TEXT("Paper recovery protocol"), Method,
                Elapsed, Minimum, Effort, HValue, TValue, Gate ? TEXT("RECOVERY") : TEXT("BACKUP / NOMINAL")));
    }
}

