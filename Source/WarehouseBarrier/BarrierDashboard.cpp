#include "BarrierDashboard.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "WarehouseMode.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
namespace
{
const FSlateRoundedBoxBrush CardBrush(FLinearColor::White, 16.f);
const FProgressBarStyle ProgressStyle =
    FProgressBarStyle()
        .SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(.88f, .93f, .94f), 4.f))
        .SetFillImage(FSlateRoundedBoxBrush(FLinearColor(.12f, .5f, .52f), 4.f));
const FButtonStyle ButtonStyle = FButtonStyle()
                                     .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 7.f))
                                     .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.85f, .93f, .94f), 7.f))
                                     .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.7f, .85f, .86f), 7.f));
const FLinearColor Ink(.97f, .98f, .99f, 1.f), Paper(.018f, .038f, .060f), Amber(.86f, .29f, .055f),
    Teal(.015f, .24f, .29f), Navy(.012f, .028f, .049f);
TSharedRef<STextBlock> Label(const FString &S, int Size = 15, FLinearColor Color = Paper)
{
    return SNew(STextBlock)
        .Text(FText::FromString(S))
        .Font(FCoreStyle::GetDefaultFontStyle(Size >= 21 ? "Bold" : "Regular", Size))
        .ColorAndOpacity(Color);
}
} // namespace
FReply SBarrierDashboard::OnMouseButtonDown(const FGeometry &, const FPointerEvent &Event)
{
    if (Page == 0 && (Event.GetEffectingButton() == EKeys::RightMouseButton ||
                      Event.GetEffectingButton() == EKeys::MiddleMouseButton))
        return FReply::Handled().CaptureMouse(SharedThis(this));
    return FReply::Unhandled();
}
FReply SBarrierDashboard::OnMouseButtonUp(const FGeometry &, const FPointerEvent &Event)
{
    if (HasMouseCapture())
        return FReply::Handled().ReleaseMouseCapture();
    return FReply::Unhandled();
}
FReply SBarrierDashboard::OnMouseMove(const FGeometry &, const FPointerEvent &Event)
{
    if (HasMouseCapture())
    {
        Mode->OrbitCamera(Event.GetCursorDelta().X, Event.GetCursorDelta().Y);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
FReply SBarrierDashboard::OnMouseWheel(const FGeometry &, const FPointerEvent &Event)
{
    if (Page == 0)
    {
        Mode->ZoomCamera(Event.GetWheelDelta());
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
int32 SBarrierChart::OnPaint(const FPaintArgs &, const FGeometry &G, const FSlateRect &, FSlateWindowElementList &Out,
                             int32 Layer, const FWidgetStyle &, bool) const
{
    const float Left = 70, Top = 35, Width = G.GetLocalSize().X - 105, Height = G.GetLocalSize().Y - 85;
    auto Font = FCoreStyle::GetDefaultFontStyle("Regular", 12);
    auto Text = [&](FString S, FVector2D P) {
        FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2D(180, 20), FSlateLayoutTransform(P)), S,
                                    Font, ESlateDrawEffect::None, Paper);
    };
    if (Points.Num() < 2)
    {
        Text(TEXT("No training data. Run scripts/train.ps1."), FVector2D(Left, Top));
        return Layer + 2;
    }
    double Low = Points[0].Y, High = Low;
    for (auto P : Points)
    {
        Low = FMath::Min(Low, P.Y);
        High = FMath::Max(High, P.Y);
    }
    if (High - Low < 1e-9)
        High = Low + 1;
    for (int i = 0; i <= 4; i++)
    {
        float Y = Top + Height * i / 4;
        TArray<FVector2D> Line = {FVector2D(Left, Y), FVector2D(Left + Width, Y)};
        FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Line, ESlateDrawEffect::None,
                                     FLinearColor(.79f, .84f, .87f), true, 1);
        Text(FString::Printf(TEXT("%.3f"), High - (High - Low) * i / 4), FVector2D(5, Y - 8));
    }
    TArray<FVector2D> Raw, Smooth;
    double Average = Points[0].Y;
    double MaxX = FMath::Max(1., Points.Last().X);
    for (auto P : Points)
    {
        Average = .9 * Average + .1 * P.Y;
        auto X = Left + Width * P.X / MaxX;
        Raw.Add(FVector2D(X, Top + Height * (High - P.Y) / (High - Low)));
        Smooth.Add(FVector2D(X, Top + Height * (High - Average) / (High - Low)));
    }
    FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Raw, ESlateDrawEffect::None,
                                 FLinearColor(.26f, .48f, .52f, .25f), true, 1);
    FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), Smooth, ESlateDrawEffect::None, Amber, true, 2.5);
    Text(TEXT("Loss"), FVector2D(5, 0));
    Text(TEXT("Optimizer iteration"), FVector2D(Left + Width * .4, Top + Height + 24));
    Text(TEXT("0"), FVector2D(Left, Top + Height + 6));
    Text(FString::Printf(TEXT("%.0f"), MaxX), FVector2D(Left + Width - 30, Top + Height + 6));
    return Layer + 2;
}

void SBarrierDashboard::Construct(const FArguments &Args)
{
    Mode = Args._Mode;
    auto Header = SNew(SHorizontalBox);
    Header->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0, 0, 18, 0)[SNew(SBorder)
                                  .BorderImage(&CardBrush)
                                  .BorderBackgroundColor(Amber)
                                  .Padding(FMargin(16, 10))[Label(TEXT("W"), 28, FLinearColor::White)]];
    Header->AddSlot().FillWidth(1).VAlign(
        VAlign_Center)[SNew(SVerticalBox) +
                       SVerticalBox::Slot().AutoHeight()[Label(TEXT("仓储作业中心"), 25, FLinearColor::White)] +
                       SVerticalBox::Slot().AutoHeight().Padding(0, 4)[Label(
                           TEXT("WAREHOUSE CONTROL    /    仓储自动化实验室"), 11, FLinearColor(.42f, .58f, .65f))]];
    const TCHAR *Names[] = {TEXT("作业现场"), TEXT("货架库存"), TEXT("训练分析"), TEXT("策略评估")};
    for (int i = 0; i < 4; i++)
        Header->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(4, 0)[SNew(SButton)
                               .ButtonStyle(&ButtonStyle)
                               .IsFocusable(false)
                               .ButtonColorAndOpacity_Lambda(
                                   [this, i]() { return Page == i ? FLinearColor(.055f, .16f, .21f) : Navy; })
                               .ContentPadding(FMargin(22, 14))
                               .OnClicked_Lambda([this, i]() {
                                   SetPage(i);
                                   return FReply::Handled();
                               })[Label(Names[i], 16, FLinearColor(.88f, .94f, .96f))]];
    Header->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(18, 0, 4, 0)[SNew(SButton)
                                  .ButtonStyle(&ButtonStyle)
                                  .IsFocusable(false)
                                  .ButtonColorAndOpacity(Teal)
                                  .ContentPadding(FMargin(16, 14))
                                  .OnClicked_Lambda([this]() {
                                      Mode->ToggleFullscreen();
                                      return FReply::Handled();
                                  })[SNew(STextBlock)
                                         .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                                         .ColorAndOpacity(FLinearColor::White)
                                         .Text_Lambda([this]() {
                                             return FText::FromString(Mode->IsFullscreen() ? TEXT("窗口 · F11")
                                                                                           : TEXT("全屏 · F11"));
                                         })]];
    Header->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(4, 0)[SNew(SButton)
                           .ButtonStyle(&ButtonStyle)
                           .IsFocusable(false)
                           .ButtonColorAndOpacity(FLinearColor(.20f, .045f, .035f))
                           .ContentPadding(FMargin(16, 14))
                           .OnClicked_Lambda([this]() {
                               Mode->ExitWarehouse();
                               return FReply::Handled();
                           })[Label(TEXT("退出 · Esc"), 14, FLinearColor::White)]];
    ChildSlot[SNew(SVerticalBox) +
              SVerticalBox::Slot().AutoHeight()[SNew(SBorder)
                                                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                                    .BorderBackgroundColor(Navy)
                                                    .Padding(FMargin(24, 16))[Header]] +
              SVerticalBox::Slot().FillHeight(1)[SAssignNew(Body, SVerticalBox)]];
    BuildBody();
}
void SBarrierDashboard::SetPage(int NewPage)
{
    Page = NewPage;
    BuildBody();
}
FReply SBarrierDashboard::Action(int Id)
{
    if (Id == 20)
        Mode->StartWork(false);
    if (Id == 21)
        Mode->StartWork(true);
    if (Id == 3)
    {
        Mode->Reset();
        Mode->Paused = true;
    }
    if (Id == 4)
        Mode->Paused = !Mode->Paused;
    if (Id == 5)
        Mode->FollowCamera = !Mode->FollowCamera;
    if (Id == 30)
        Mode->ZoomCamera(1);
    if (Id == 31)
        Mode->ZoomCamera(-1);
    if (Id == 32)
    {
        Mode->CameraYaw = -50;
        Mode->CameraPitch = 30;
        Mode->CameraDistance = 2370;
        Mode->FollowCamera = false;
    }
    return FReply::Handled();
}
FString SBarrierDashboard::LiveText() const
{
    return FString::Printf(TEXT("%s\n\n速度 %.2f m/s  ·  裕度 %.2f m\n%s"), *Mode->WorkStatus, Mode->V.Size(),
                           Mode->Minimum,
                           Mode->Paused     ? TEXT("已暂停")
                           : Mode->AutoWork ? TEXT("自动作业中")
                                            : TEXT("可领取新任务"));
}
TArray<FVector2D> SBarrierDashboard::ReadCurve() const
{
    TArray<FVector2D> Points;
    TArray<FString> Lines;
    FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectContentDir() / TEXT("Models/loss.csv")));
    const TCHAR *Stages[] = {TEXT("A_barrier"), TEXT("B_switch"), TEXT("C_threshold"), TEXT("D_joint")};
    for (auto Line : Lines)
    {
        TArray<FString> Parts;
        Line.ParseIntoArray(Parts, TEXT(","));
        if (Parts.Num() == 3 && Parts[0] == Stages[Stage])
            Points.Add(FVector2D(FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2])));
    }
    return Points;
}

void SBarrierDashboard::BuildBody()
{
    Body->ClearChildren();
    auto Button = [&](const TCHAR *Name, int Id, bool Primary = false) {
        return SNew(SButton)
            .ButtonStyle(&ButtonStyle)
            .IsFocusable(false)
            .ButtonColorAndOpacity(Primary ? Teal : FLinearColor(.84f, .89f, .92f))
            .ContentPadding(FMargin(12, 12))
            .OnClicked_Lambda(
                [this, Id]() { return Action(Id); })[Label(Name, 15, Primary ? FLinearColor::White : Paper)];
    };
    auto Card = [&](TSharedRef<SWidget> Widget) {
        return SNew(SBorder).BorderImage(&CardBrush).BorderBackgroundColor(Ink).Padding(22)[Widget];
    };
    auto Inventory = [&]() {
        auto Panel = SNew(SVerticalBox);
        Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 18)[Label(TEXT("货架概览"), 21)];
        for (int Rack = 0; Rack < 4; Rack++)
        {
            Panel->AddSlot().AutoHeight().Padding(
                0, 8, 0,
                9)[Label(FString::Printf(TEXT("%c 区 · %c类货品"), TCHAR('A' + Rack), TCHAR('A' + Rack)), 15, Teal)];
            auto Row = SNew(SHorizontalBox);
            for (int Level = 0; Level < 3; Level++)
            {
                int Slot = Rack * 3 + Level;
                Row->AddSlot().FillWidth(1).Padding(
                    3, 0)[SNew(SBorder)
                              .BorderImage(&CardBrush)
                              .BorderBackgroundColor_Lambda([this, Slot]() {
                                  for (auto C : Mode->Cargo)
                                      if (C.Slot == Slot)
                                          return C.SKU == Slot / 3 ? FLinearColor(.77f, .89f, .88f)
                                                                   : FLinearColor(.95f, .85f, .65f);
                                  return FLinearColor(.90f, .93f, .95f);
                              })
                              .Padding(FMargin(8, 14))[SNew(STextBlock)
                                                           .Justification(ETextJustify::Center)
                                                           .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
                                                           .ColorAndOpacity(Paper)
                                                           .Text_Lambda([this, Slot]() {
                                                               for (auto C : Mode->Cargo)
                                                                   if (C.Slot == Slot)
                                                                       return FText::FromString(
                                                                           FString::Chr(TCHAR('A' + C.SKU)));
                                                               return FText::FromString(TEXT("—"));
                                                           })]];
            }
            Panel->AddSlot().AutoHeight()[Row];
        }
        Panel->AddSlot().AutoHeight().Padding(
            0,
            18)[Label(TEXT("左至右：L1 / L2 / L3\n青色 分类正确    金色 待整理"), 12, FLinearColor(.4f, .48f, .52f))];
        return Panel;
    };
    if (Page == 0)
    {
        auto Left = SNew(SVerticalBox);
        auto Work = SNew(SVerticalBox);
        Work->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Label(TEXT("AGV-01  /  任务调度"), 12, Teal)];
        Work->AddSlot().AutoHeight()[Label(TEXT("自动搬运"), 26)];
        Work->AddSlot().AutoHeight().Padding(0, 6, 0, 18)[Label(TEXT("取货 → 装载 → 运输 → 上架"), 13, Teal)];
        Work->AddSlot().AutoHeight()[SNew(STextBlock)
                                         .Text_Lambda([this]() { return FText::FromString(LiveText()); })
                                         .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
                                         .ColorAndOpacity(Paper)];
        Work->AddSlot().AutoHeight().Padding(0, 20, 0, 7)
            [SNew(SProgressBar).Style(&ProgressStyle).FillColorAndOpacity(FLinearColor::White).Percent_Lambda([this]() {
                return Mode->Jobs.Num()
                           ? FMath::Clamp((Mode->JobIndex + Mode->WorkPhase * .2f) / Mode->Jobs.Num(), 0.f, 1.f)
                           : 0.f;
            })];
        Work->AddSlot().AutoHeight().Padding(
            0, 0, 0, 18)[SNew(STextBlock)
                             .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                             .ColorAndOpacity(Paper)
                             .Text_Lambda([this]() {
                                 return FText::FromString(
                                     FString::Printf(TEXT("本轮任务  %d / %d"), Mode->JobIndex, Mode->Jobs.Num()));
                             })];
        Work->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("自动入库搬运"), 20, true)];
        Work->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("按货品整理货架"), 21)];
        Work->AddSlot().AutoHeight().Padding(
            0, 12)[SNew(SHorizontalBox) +
                   SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 5, 0)[Button(TEXT("暂停 / 继续"), 4)] +
                   SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("重置仓库"), 3)]];
        Left->AddSlot().AutoHeight()[Card(Work)];
        auto Facts = SNew(SVerticalBox);
        Facts->AddSlot().AutoHeight()[Label(TEXT("作业记录"), 17)];
        Facts->AddSlot().AutoHeight().Padding(
            0, 14)[SNew(STextBlock)
                       .Font(FCoreStyle::GetDefaultFontStyle("Bold", 23))
                       .ColorAndOpacity(Teal)
                       .Text_Lambda([this]() {
                           return FText::FromString(
                               FString::Printf(TEXT("%02d 次  /  %.1f m"), Mode->CompletedJobs, Mode->TravelDistance));
                       })];
        Facts->AddSlot().AutoHeight()[SNew(STextBlock)
                                          .AutoWrapText(true)
                                          .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                                          .ColorAndOpacity(Paper)
                                          .Text_Lambda([this]() { return FText::FromString(Mode->LastEvent); })];
        Left->AddSlot().AutoHeight().Padding(0, 14)[Card(Facts)];
        auto Right = Inventory();
        Right->AddSlot().AutoHeight()[Button(TEXT("总览 / 跟随视角"), 5)];
        Right->AddSlot().AutoHeight().Padding(
            0, 8)[SNew(SHorizontalBox) +
                  SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)[Button(TEXT("放大"), 30)] +
                  SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)[Button(TEXT("缩小"), 31)] +
                  SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("复位"), 32)]];
        Right->AddSlot().AutoHeight().Padding(
            0, 20, 0, 10)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("缓行推车"), 14)] +
                          SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock)
                                                                 .ColorAndOpacity(Teal)
                                                                 .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                                                                 .Text_Lambda([this]() {
                                                                     return FText::FromString(FString::Printf(
                                                                         TEXT("%.2f m/s"), Mode->CartSpeed));
                                                                 })]];
        Right->AddSlot().AutoHeight().Padding(
            0, 4)[SNew(SSlider)
                      .MinValue(0.f)
                      .MaxValue(.6f)
                      .StepSize(.01f)
                      .SliderBarColor(FLinearColor(.30f, .53f, .58f))
                      .SliderHandleColor(Teal)
                      .Value_Lambda([this]() { return float(Mode->CartSpeed); })
                      .OnValueChanged_Lambda([this](float Value) { Mode->SetCartSpeed(Value); })];
        Right->AddSlot().AutoHeight().Padding(
            0, 6)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("0 · 停止"), 11, Teal)] +
                  SHorizontalBox::Slot().AutoWidth()[Label(TEXT("0.60"), 11, Teal)]];
        auto View = SNew(SVerticalBox);
        View->AddSlot().AutoHeight().Padding(
            20, 8)[SNew(SHorizontalBox) +
                   SHorizontalBox::Slot().FillWidth(
                       1)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Label(TEXT("仓库全景"), 26)] +
                          SVerticalBox::Slot().AutoHeight().Padding(
                              0, 6)[Label(TEXT("右键拖动旋转 · 滚轮缩放 · C 切换跟随"), 13, Teal)]]];
        View->AddSlot().FillHeight(1)[SNew(SSpacer)];
        auto Telemetry = SNew(SHorizontalBox);
        const TCHAR *MetricNames[] = {TEXT("AGV 速度 / m/s"), TEXT("安全裕度 / m"), TEXT("累计搬运 / 次")};
        for (int Metric = 0; Metric < 3; ++Metric)
            Telemetry->AddSlot().FillWidth(1).Padding(
                14, 0)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Label(MetricNames[Metric], 12, Teal)] +
                       SVerticalBox::Slot().AutoHeight().Padding(
                           0, 6)[SNew(STextBlock)
                                     .Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
                                     .ColorAndOpacity(Paper)
                                     .Text_Lambda([this, Metric]() {
                                         return FText::FromString(
                                             Metric == 0   ? FString::Printf(TEXT("%.2f"), Mode->V.Size())
                                             : Metric == 1 ? FString::Printf(TEXT("%.2f"), Mode->Minimum)
                                                           : FString::Printf(TEXT("%02d"), Mode->CompletedJobs));
                                     })]];
        View->AddSlot().AutoHeight().Padding(20, 0, 20, 8)[Card(Telemetry)];
        Body->AddSlot().FillHeight(1).Padding(
            18)[SNew(SHorizontalBox) +
                SHorizontalBox::Slot()
                    .AutoWidth()[SNew(SBox).WidthOverride(310)[SNew(SScrollBox) + SScrollBox::Slot()[Left]]] +
                SHorizontalBox::Slot().FillWidth(1)[View] +
                SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(270)[Card(Right)]]];
        return;
    }
    if (Page == 1)
    {
        auto Content = SNew(SHorizontalBox);
        Content->AddSlot().FillWidth(.4)[Card(Inventory())];
        auto Detail = SNew(SVerticalBox);
        Detail->AddSlot().AutoHeight()[Label(TEXT("库存与货位"), 28)];
        Detail->AddSlot().AutoHeight().Padding(0, 12, 0,
                                               28)[Label(TEXT("货物按 A / B / C / D 分类，整理后进入同类货架。"), 16)];
        Detail->AddSlot().AutoHeight()[SNew(STextBlock)
                                           .Font(FCoreStyle::GetDefaultFontStyle("Regular", 19))
                                           .ColorAndOpacity(Paper)
                                           .Text_Lambda([this]() { return FText::FromString(Mode->InventoryText()); })];
        auto Ledger = SNew(SGridPanel);
        Ledger->SetColumnFill(0, 1.4f);
        Ledger->SetColumnFill(1, .7f);
        Ledger->SetColumnFill(2, 1.f);
        Ledger->SetColumnFill(3, 1.f);
        const TCHAR *Titles[] = {TEXT("货物编号"), TEXT("类别"), TEXT("货位"), TEXT("状态")};
        for (int Column = 0; Column < 4; Column++)
            Ledger->AddSlot(Column, 0).Padding(8, 14)[Label(Titles[Column], 15, Teal)];
        for (int Id = 0; Id < Mode->Cargo.Num(); Id++)
        {
            Ledger->AddSlot(0, Id + 1).Padding(8, 9)[Label(FString::Printf(TEXT("PAL-%04d"), Id + 1), 16)];
            Ledger->AddSlot(1, Id + 1).Padding(8, 9)[Label(FString::Chr(TCHAR('A' + Mode->Cargo[Id].SKU)), 16)];
            for (int Column = 2; Column < 4; Column++)
                Ledger->AddSlot(Column, Id + 1)
                    .Padding(8, 9)[SNew(STextBlock)
                                       .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
                                       .ColorAndOpacity(Teal)
                                       .Text_Lambda([this, Id, Column]() {
                                           int Slot = Mode->Cargo[Id].Slot;
                                           FString Text =
                                               Column == 2
                                                   ? (Slot >= 0 ? FString::Printf(TEXT("%c-L%d"), TCHAR('A' + Slot / 3),
                                                                                  Slot % 3 + 1)
                                                      : Slot == -2 ? TEXT("AGV-01")
                                                                   : TEXT("入库 / 周转区"))
                                                   : (Slot >= 0    ? TEXT("在库")
                                                      : Slot == -2 ? TEXT("运输中")
                                                                   : TEXT("待上架"));
                                           return FText::FromString(Text);
                                       })];
        }
        Detail->AddSlot().AutoHeight().Padding(0, 8)[Ledger];
        Detail->AddSlot().AutoHeight().Padding(0, 24)[Button(TEXT("执行自动货架整理"), 21, true)];

        Content->AddSlot().FillWidth(1).Padding(24, 0, 0, 0)[Card(Detail)];
        Body->AddSlot().FillHeight(1).Padding(24)[Content];
        return;
    }
    auto Content = SNew(SVerticalBox);
    Content->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Label(Page == 2 ? TEXT("训练分析") : TEXT("策略评估"), 28)];
    if (Page == 2)
    {
        Content->AddSlot().AutoHeight()[Label(TEXT("四阶段训练记录  ·  浅线为原始损失，主线为指数平滑"), 14, Teal)];
        auto Tabs = SNew(SHorizontalBox);
        const TCHAR *Names[] = {TEXT("屏障评价"), TEXT("切换策略"), TEXT("阈值校准"), TEXT("联合微调")};
        for (int i = 0; i < 4; i++)
            Tabs->AddSlot().AutoWidth().Padding(
                0, 18, 12, 18)[SNew(SButton)
                                   .ButtonStyle(&ButtonStyle)
                                   .IsFocusable(false)
                                   .ButtonColorAndOpacity(Stage == i ? FLinearColor(.75f, .88f, .88f)
                                                                     : FLinearColor(.9f, .94f, .95f))
                                   .ContentPadding(FMargin(20, 12))
                                   .OnClicked_Lambda([this, i]() {
                                       Stage = i;
                                       BuildBody();
                                       return FReply::Handled();
                                   })[Label(Names[i], 16)]];
        Content->AddSlot().AutoHeight()[Tabs];
        Content->AddSlot().FillHeight(1)[SNew(SBarrierChart).Points(ReadCurve())];
    }
    else
    {
        TArray<FString> Lines;
        FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectContentDir() / TEXT("Models/summary.csv")));
        auto Grid = SNew(SGridPanel);
        for (int i = 0; i < 8; i++)
            Grid->SetColumnFill(i, i == 0 ? 1.6f : 1.f);
        const TCHAR *Names[] = {TEXT("策略"),     TEXT("配对数"),   TEXT("恢复成功"), TEXT("困难恢复"),
                                TEXT("安全回退"), TEXT("控制能量"), TEXT("干预量"),   TEXT("恢复调用")};
        for (int j = 0; j < 8; j++)
            Grid->AddSlot(j, 0).Padding(12, 25)[Label(Names[j], 15, Teal)];
        for (int i = 1; i < Lines.Num(); i++)
        {
            TArray<FString> Cells;
            Lines[i].ParseIntoArray(Cells, TEXT(","));
            if (Cells.Num() != 8)
                continue;
            for (int j = 0; j < 8; j++)
            {
                FString Value = Cells[j];
                if (j >= 2 && j != 5 && j != 6)
                    Value = FString::Printf(TEXT("%.2f%%"), FCString::Atod(*Value) * 100);
                else if (j >= 5)
                    Value = FString::Printf(TEXT("%.3f"), FCString::Atod(*Value));
                if (j == 0)
                {
                    Value = Value == TEXT("brake")    ? TEXT("基础刹车")
                            : Value == TEXT("joint")  ? TEXT("受保护联合")
                            : Value == TEXT("staged") ? TEXT("分阶段策略")
                                                      : TEXT("全时边界");
                }
                Grid->AddSlot(j, i).Padding(12, 24)[Label(Value, 18)];
            }
        }
        Content->AddSlot().AutoHeight()[Grid];
        Content->AddSlot().AutoHeight().Padding(
            0, 30)[Label(TEXT("5 个随机种子 × 13 个环境  ·  预测时域 3 s  ·  步长 0.05 s"), 15, Teal)];
    }
    Body->AddSlot().FillHeight(1).Padding(24)[Card(Content)];
}
