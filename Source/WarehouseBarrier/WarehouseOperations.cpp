#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "WarehouseMode.h"

FVector AWarehouseMode::SlotPosition(int Slot) const
{
    if (Slot < 0)
        return FVector(-620, -450, 25);
    int Rack = Slot / 3, Level = Slot % 3;
    return FVector(Env[Rack * 3] * 100, Env[Rack * 3 + 1] * 100, 31 + (Level == 2 ? 190 : 90 * Level));
}
FVector2D AWarehouseMode::ShelfDock(int Slot) const
{
    int Rack = Slot / 3;
    return FVector2D(Env[Rack * 3], Env[Rack * 3 + 1] + (Env[Rack * 3 + 1] < 0 ? -1.85 : 1.85));
}
FVector2D AWarehouseMode::CargoDock(int Id) const
{
    return Cargo[Id].Slot >= 0 ? ShelfDock(Cargo[Id].Slot) : FVector2D(-6.2, -2.8);
}
void AWarehouseMode::BuildInventory()
{
    for (int Id = 0; Id < 12; Id++)
    {
        FWarehouseCargo Item;
        Item.Id = Id;
        Item.SKU = Id < 8 ? (Id % 2 == 0 ? (Id / 2 + 1) % 4 : Id / 2) : Id - 8;
        Item.Slot = Id < 8 ? (Id / 2) * 3 + Id % 2 : -1;
        FVector Base = Item.Slot >= 0 ? SlotPosition(Item.Slot) : FVector(-620, -450, 25 + 75 * (Id - 8));
        Item.Actor = Box(Base, FVector(.70, .84, .025), FLinearColor(.38f, .24f, .12f));
        auto Part = [&](FVector Local, FVector Scale, FLinearColor Color) {
            auto *A = Box(Base + Local, Scale, Color);
            A->AttachToActor(Item.Actor, FAttachmentTransformRules::KeepWorldTransform);
        };
        const FLinearColor Colors[] = {FLinearColor(.13f, .45f, .48f), FLinearColor(.62f, .36f, .10f),
                                       FLinearColor(.26f, .39f, .64f), FLinearColor(.57f, .26f, .33f)};
        Part(FVector(0, 0, 29), FVector(.63, .73, .50), FLinearColor(.59f, .41f, .23f));
        Part(FVector(0, -37, 30), FVector(.34, .012, .20), Colors[Item.SKU]);
        Part(FVector(0, 0, 55), FVector(.04, .73, .025), FLinearColor(.74f, .61f, .40f));
        for (double X : {-30., 0., 30.})
            Part(FVector(X, 0, -8), FVector(.09, .84, .13), FLinearColor(.38f, .24f, .12f));
        Part(FVector(0, 0, -15), FVector(.70, .84, .025), FLinearColor(.38f, .24f, .12f));
        Cargo.Add(Item);
    }
    Box(FVector(-620, -450, 1), FVector(1.3, 1.3, .025), FLinearColor(.18f, .43f, .42f));
}
void AWarehouseMode::ResetInventory()
{
    Jobs.Empty();
    Route.Empty();
    AutoWork = false;
    SortAfter = false;
    JobIndex = RouteIndex = WorkPhase = CompletedJobs = 0;
    WorkTime = PhaseTime = TravelDistance = 0;
    WorkStatus = TEXT("等待调度");
    LastEvent = TEXT("入库区有4件待上架货物");
    for (auto &C : Cargo)
    {
        C.Slot = C.Id < 8 ? (C.Id / 2) * 3 + C.Id % 2 : -1;
        C.Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        C.Actor->SetActorLocation(C.Slot >= 0 ? SlotPosition(C.Slot) : FVector(-620, -450, 25 + 75 * (C.Id - 8)));
        C.Actor->SetActorRotation(FRotator::ZeroRotator);
    }
}
void AWarehouseMode::StartWork(bool Organize)
{
    if (AutoWork)
    {
        if (Organize)
        {
            SortAfter = true;
            LastEvent = TEXT("货架整理已加入后续任务");
        }
        return;
    }
    Navigation = true;
    Paused = false;
    AutoWork = true;
    Jobs.Empty();
    JobIndex = 0;
    PhaseTime = 0;
    if (Organize)
        BuildSortJobs();
    else
    {
        TArray<bool> Reserved;
        Reserved.Init(false, 12);
        for (auto C : Cargo)
            if (C.Slot >= 0)
                Reserved[C.Slot] = true;
        for (int CargoIndex = Cargo.Num() - 1; CargoIndex >= 0; --CargoIndex)
            if (auto C = Cargo[CargoIndex]; C.Slot == -1)
            {
                for (int Slot = C.SKU * 3; Slot < C.SKU * 3 + 3; Slot++)
                    if (!Reserved[Slot])
                    {
                        FWarehouseJob J;
                        J.Cargo = C.Id;
                        J.Destination = Slot;
                        Jobs.Add(J);
                        Reserved[Slot] = true;
                        break;
                    }
            }
    }
    DispatchJob();
}
void AWarehouseMode::BuildSortJobs()
{
    Jobs.Empty();
    JobIndex = 0;
    auto Sim = Cargo;
    Sim.Sort([](const FWarehouseCargo &A, const FWarehouseCargo &B) { return A.Id > B.Id; });
    for (int Iter = 0; Iter < 64; Iter++)
    {
        TArray<int> Occupied;
        Occupied.Init(-1, 12);
        for (auto C : Sim)
            if (C.Slot >= 0)
                Occupied[C.Slot] = C.Id;
        bool Changed = false, Wrong = false;
        for (auto &C : Sim)
            if (C.Slot < 0 || C.Slot / 3 != C.SKU)
            {
                Wrong = true;
                for (int Slot = C.SKU * 3; Slot < C.SKU * 3 + 3; Slot++)
                    if (Occupied[Slot] < 0)
                    {
                        FWarehouseJob J;
                        J.Cargo = C.Id;
                        J.Destination = Slot;
                        J.Sort = true;
                        Jobs.Add(J);
                        C.Slot = Slot;
                        Changed = true;
                        break;
                    }
                if (Changed)
                    break;
            }
        if (!Wrong)
            break;
        if (!Changed)
        {
            for (auto &C : Sim)
                if (C.Slot >= 0 && C.Slot / 3 != C.SKU)
                {
                    FWarehouseJob J;
                    J.Cargo = C.Id;
                    J.Destination = -1;
                    J.Sort = true;
                    Jobs.Add(J);
                    C.Slot = -1;
                    Changed = true;
                    break;
                }
        }
        if (!Changed)
            break;
    }
}
void AWarehouseMode::DispatchJob()
{
    if (JobIndex >= Jobs.Num())
    {
        if (SortAfter)
        {
            SortAfter = false;
            BuildSortJobs();
            if (Jobs.Num())
            {
                DispatchJob();
                return;
            }
        }
        AutoWork = false;
        WorkPhase = 0;
        V = FVector2D::ZeroVector;
        Paused = true;
        WorkStatus = TEXT("全部作业完成");
        LastEvent = FString::Printf(TEXT("已完成 %d 次搬运，库存已更新"), CompletedJobs);
        SaveInventory();
        UE_LOG(LogTemp, Display, TEXT("OPS_COMPLETE jobs=%d cargo=%d min_margin=%.4f time=%.2f"), CompletedJobs,
               Cargo.Num(), Minimum, WorkTime);
        return;
    }
    auto J = Jobs[JobIndex];
    Goal = CargoDock(J.Cargo);
    WorkPhase = 1;
    PhaseTime = 0;
    WorkStatus = FString::Printf(TEXT("前往取货 · %c类货品"), TCHAR('A' + Cargo[J.Cargo].SKU));
    if (!PlanRoute(Goal))
    {
        WorkStatus = TEXT("重新规划取货路线");
        Route = {Goal};
        RouteIndex = 0;
    }
}
double AWarehouseMode::PlanningMargin(FVector2D Pos) const
{
    double R = .38;
    for (auto C : Cargo)
        if (C.Slot == -2)
            R = .85;
    double Result = FMath::Min(8 - FMath::Abs(Pos.X) - R, 6 - FMath::Abs(Pos.Y) - R);
    for (int i = 0; i < 6; i++)
        Result = FMath::Min(Result, (Pos - FVector2D(Env[i * 3], Env[i * 3 + 1])).Size() - Env[i * 3 + 2] - R);
    for (auto C : Cargo)
        if (C.Slot == -1)
            Result = FMath::Min(Result, (Pos - FVector2D(-6.2, -4.5)).Size() - .56 - R);
    return Result;
}
bool AWarehouseMode::PlanRoute(FVector2D Target)
{
    const double Cell = .35;
    const int NX = 43, NY = 31, Count = NX * NY;
    auto Position = [&](int I) { return FVector2D(-7.35 + (I % NX) * Cell, -5.25 + (I / NX) * Cell); };
    auto Nearest = [&](FVector2D Point) {
        int Best = -1;
        double Distance = 1e9;
        for (int i = 0; i < Count; i++)
        {
            auto Q = Position(i);
            if (PlanningMargin(Q) < .07)
                continue;
            double D = (Q - Point).SizeSquared();
            if (D < Distance)
            {
                Distance = D;
                Best = i;
            }
        }
        return Best;
    };
    int Start = Nearest(P), End = Nearest(Target);
    if (Start < 0 || End < 0)
        return false;
    TArray<double> Score;
    Score.Init(1e12, Count);
    Score[Start] = 0;
    TArray<int> Previous;
    Previous.Init(-1, Count);
    TArray<bool> Closed;
    Closed.Init(false, Count);
    for (int Iter = 0; Iter < Count; Iter++)
    {
        int Current = -1;
        double Best = 1e12;
        for (int i = 0; i < Count; i++)
            if (!Closed[i] && Score[i] < 1e11)
            {
                double F = Score[i] + (Position(i) - Position(End)).Size();
                if (F < Best)
                {
                    Best = F;
                    Current = i;
                }
            }
        if (Current < 0)
            break;
        if (Current == End)
            break;
        Closed[Current] = true;
        int X = Current % NX, Y = Current / NX;
        for (int DX = -1; DX <= 1; DX++)
            for (int DY = -1; DY <= 1; DY++)
            {
                if (!DX && !DY)
                    continue;
                int XX = X + DX, YY = Y + DY;
                if (XX < 0 || XX >= NX || YY < 0 || YY >= NY)
                    continue;
                int Next = YY * NX + XX;
                if (Closed[Next] || PlanningMargin(Position(Next)) < .07 ||
                    PlanningMargin((Position(Current) + Position(Next)) * .5) < .07)
                    continue;
                double New = Score[Current] + (Position(Next) - Position(Current)).Size();
                if (New < Score[Next])
                {
                    Score[Next] = New;
                    Previous[Next] = Current;
                }
            }
    }
    if (End != Start && Previous[End] < 0)
        return false;
    TArray<FVector2D> Reverse;
    int Node = End;
    while (Node >= 0 && Node != Start)
    {
        Reverse.Add(Position(Node));
        Node = Previous[Node];
    }
    TArray<FVector2D> Raw;
    for (int i = Reverse.Num() - 1; i >= 0; i--)
        Raw.Add(Reverse[i]);
    Raw.Add(Target);
    Route.Empty();
    FVector2D From = P;
    for (int i = 0; i < Raw.Num();)
    {
        int Furthest = i;
        for (int j = i; j < Raw.Num(); j++)
        {
            double Distance = (Raw[j] - From).Size();
            bool Clear = true;
            int Samples = FMath::Max(1, FMath::CeilToInt(Distance / .10));
            for (int k = 1; k <= Samples; k++)
                if (PlanningMargin(From + (Raw[j] - From) * (double(k) / Samples)) < .09)
                {
                    Clear = false;
                    break;
                }
            if (Clear)
                Furthest = j;
            else
                break;
        }
        Route.Add(Raw[Furthest]);
        From = Raw[Furthest];
        i = Furthest + 1;
    }
    RouteIndex = 0;
    return Route.Num() > 0;
}
FVector2D AWarehouseMode::NavigationAction(FVector2D Target, double Dt)
{
    bool Carrying = false;
    for (auto C : Cargo)
        Carrying |= C.Slot == -2;
    FVector2D Direction = Target - P;
    double Distance = Direction.Size();
    double Speed = FMath::Min(Carrying ? .65 : 1.0, FMath::Sqrt(FMath::Max(0., 1.1 * Distance)));
    auto Desired = (Direction.GetSafeNormal() * Speed - V) * 2.5;
    Desired.X = FMath::Clamp(Desired.X, -1., 1.);
    Desired.Y = FMath::Clamp(Desired.Y, -1., 1.);
    auto Predict = [&](FVector2D First) {
        auto PP = P, VV = V;
        auto Future = Env;
        double Min = Margin(PP, &Future);
        for (int k = 0; k < 60; k++)
        {
            auto A = k == 0 ? First : Brake(VV);
            PP += VV * Dt + A * (.5 * Dt * Dt);
            VV += A * Dt;
            Future = AdvanceEnvironment(Future, Dt);
            Min = FMath::Min(Min, Margin(PP, &Future));
        }
        return Min;
    };
    if (Predict(Desired) > .045)
    {
        Gate = false;
        return Desired;
    }
    auto Stop = Brake(V);
    double Best = Predict(Stop);
    auto Action = Stop;
    TArray<FVector2D> Candidates = {FVector2D(-1, -1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(1, 1)};
    for (auto A : Candidates)
    {
        double M = Predict(A);
        if (M > Best + .002)
        {
            Best = M;
            Action = A;
        }
    }
    Gate = true;
    return Action;
}
void AWarehouseMode::UpdateWork(double Dt)
{
    WorkTime += Dt;
    RouteAge += Dt;
    if (JobIndex >= Jobs.Num())
        return;
    auto J = Jobs[JobIndex];
    auto &C = Cargo[J.Cargo];
    if ((WorkPhase == 1 || WorkPhase == 3) && RouteAge > 2)
    {
        PlanRoute(WorkPhase == 1 ? CargoDock(J.Cargo)
                                 : (J.Destination >= 0 ? ShelfDock(J.Destination) : FVector2D(-6.2, -2.8)));
        RouteAge = 0;
    }
    if (WorkPhase == 1 || WorkPhase == 3)
    {
        if (RouteIndex < Route.Num() - 1 && (P - Route[RouteIndex]).Size() < .24)
            RouteIndex++;
        Goal = Route.IsValidIndex(RouteIndex) ? Route[RouteIndex] : Goal;
        if (RouteIndex >= Route.Num() - 1 && (P - Goal).Size() < .075 && V.Size() < .085)
        {
            V = FVector2D::ZeroVector;
            PhaseTime = 0;
            TransferStart = C.Actor->GetActorLocation();
            if (WorkPhase == 1)
            {
                WorkPhase = 2;
                WorkStatus = TEXT("装载货物");
                TransferEnd = Robot->GetActorLocation() + FVector(0, 0, 60);
                FVector Facing = TransferStart - Robot->GetActorLocation();
                Facing.Z = 0;
                Robot->SetActorRotation(Facing.Rotation());
                TransferEnd += Robot->GetActorForwardVector() * 25;
            }
            else
            {
                WorkPhase = 4;
                WorkStatus = TEXT("放置货物 · 更新货位");
                C.Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
                TransferEnd = J.Destination >= 0 ? SlotPosition(J.Destination) : FVector(-620, -450, 25);
                FVector Facing = TransferEnd - Robot->GetActorLocation();
                Facing.Z = 0;
                Robot->SetActorRotation(Facing.Rotation());
                TransferStart = Robot->GetActorLocation() + Robot->GetActorForwardVector() * 25 + FVector(0, 0, 60);
                C.Actor->SetActorLocation(TransferStart);
                C.Actor->SetActorRotation(FRotator::ZeroRotator);
            }
        }
    }
    else if (WorkPhase == 2 || WorkPhase == 4)
    {
        PhaseTime += Dt;
        // Raise clear of the shelf, retract horizontally, then lower outside it.
        auto Smooth = [](double T) {
            T = FMath::Clamp(T, 0., 1.);
            return T * T * (3 - 2 * T);
        };
        const double T = PhaseTime / 4.8;
        FVector Position;
        const FVector AtRobot = Robot->GetActorLocation() + Robot->GetActorForwardVector() * 25;
        if (WorkPhase == 2)
        {
            FVector Raised = TransferStart + FVector(0, 0, 12);
            FVector Retracted(AtRobot.X, AtRobot.Y, Raised.Z);
            if (T < .2)
                Position = TransferStart; // forks enter the pallet channels first
            else if (T < .35)
                Position = FMath::Lerp(TransferStart, Raised, Smooth((T - .2) / .15));
            else if (T < .7)
                Position = FMath::Lerp(Raised, Retracted, Smooth((T - .35) / .35));
            else
                Position = FMath::Lerp(Retracted, TransferEnd, Smooth((T - .7) / .3));
            double Reach = FVector::DotProduct(Position - Robot->GetActorLocation(), Robot->GetActorForwardVector());
            ForkReach = T < .2 ? FMath::Lerp(-7., Reach, Smooth(T / .2)) : Reach;
            CargoHeight = Position.Z;
        }
        else
        {
            FVector Raised(AtRobot.X, AtRobot.Y, TransferEnd.Z + 12);
            FVector Extended = TransferEnd + FVector(0, 0, 12);
            if (T < .3)
                Position = FMath::Lerp(TransferStart, Raised, Smooth(T / .3));
            else if (T < .65)
                Position = FMath::Lerp(Raised, Extended, Smooth((T - .3) / .35));
            else if (T < .8)
                Position = FMath::Lerp(Extended, TransferEnd, Smooth((T - .65) / .15));
            else
                Position = TransferEnd;
            double Reach = FVector::DotProduct(Position - Robot->GetActorLocation(), Robot->GetActorForwardVector());
            ForkReach = T < .8 ? Reach : FMath::Lerp(Reach, 25., Smooth((T - .8) / .2));
            CargoHeight = Position.Z - (T > .8 ? 5 : 0);
        }
        C.Actor->SetActorLocation(Position);
        UpdateForks();
        if (PhaseTime >= 4.8)
        {
            if (WorkPhase == 2)
            {
                C.Slot = -2;
                C.Actor->AttachToActor(Robot, FAttachmentTransformRules::KeepWorldTransform);
                WorkPhase = 3;
                Goal = J.Destination >= 0 ? ShelfDock(J.Destination) : FVector2D(-6.2, -2.8);
                WorkStatus = J.Sort ? TEXT("整理货架 · 运输中") : TEXT("自动上架 · 运输中");
                PlanRoute(Goal);
            }
            else
            {
                C.Slot = J.Destination;
                C.Actor->SetActorLocation(TransferEnd);
                C.Actor->SetActorRotation(FRotator::ZeroRotator);
                CompletedJobs++;
                LastEvent = J.Destination >= 0 ? FString::Printf(TEXT("%c类货物已放入 %c-%02d"), TCHAR('A' + C.SKU),
                                                                 TCHAR('A' + J.Destination / 3), J.Destination % 3 + 1)
                                               : TEXT("货物已移至周转区");
                UE_LOG(LogTemp, Display, TEXT("OPS_TRANSFER cargo=%d sku=%d destination=%d completed=%d"), C.Id, C.SKU,
                       C.Slot, CompletedJobs);
                SaveInventory();
                JobIndex++;
                DispatchJob();
            }
        }
    }
}
FString AWarehouseMode::InventoryText() const
{
    FString Result;
    for (int Rack = 0; Rack < 4; Rack++)
    {
        int Count = 0, Wrong = 0;
        for (auto C : Cargo)
            if (C.Slot >= 0 && C.Slot / 3 == Rack)
            {
                Count++;
                Wrong += C.SKU != Rack;
            }
        Result += FString::Printf(TEXT("%c 区   %d / 3 货位     %s\n"), TCHAR('A' + Rack), Count,
                                  Wrong ? *FString::Printf(TEXT("%d 件待整理"), Wrong) : TEXT("分类正确"));
    }
    return Result;
}
void AWarehouseMode::SaveInventory()
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("completed_jobs"), CompletedJobs);
    Root->SetNumberField(TEXT("time"), WorkTime);
    TArray<TSharedPtr<FJsonValue>> Items;
    for (auto C : Cargo)
    {
        auto Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("id"), C.Id);
        Object->SetStringField(TEXT("sku"), FString::Chr(TCHAR('A' + C.SKU)));
        Object->SetNumberField(TEXT("slot"), C.Slot);
        Items.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("inventory"), Items);
    FString Text;
    auto Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Root, Writer);
    FFileHelper::SaveStringToFile(Text,
                                  *(FPaths::ProjectSavedDir() / (OpsVerify       ? TEXT("verification_inventory.json")
                                                                 : SortedCapture ? TEXT("preview_inventory.json")
                                                                                 : TEXT("warehouse_inventory.json"))));
}

