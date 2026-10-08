#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
class AWarehouseMode;
class SVerticalBox;
class SBarrierChart : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SBarrierChart)
    {
    }
    SLATE_ARGUMENT(TArray<FVector2D>, Points) SLATE_END_ARGS() void Construct(const FArguments &Args)
    {
        Points = Args._Points;
    }
    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(800, 370);
    }
    virtual int32 OnPaint(const FPaintArgs &, const FGeometry &, const FSlateRect &, FSlateWindowElementList &, int32,
                          const FWidgetStyle &, bool) const override;

  private:
    TArray<FVector2D> Points;
};
class SBarrierDashboard : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SBarrierDashboard)
    {
    }
    SLATE_ARGUMENT(AWarehouseMode *, Mode) SLATE_END_ARGS() void Construct(const FArguments &Args);
    void SetPage(int Page);
    virtual FReply OnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseButtonUp(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseMove(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseWheel(const FGeometry &, const FPointerEvent &) override;

  private:
    AWarehouseMode *Mode = nullptr;
    int Page = 0, Stage = 0;
    TSharedPtr<SVerticalBox> Body;
    void BuildBody();
    FReply Action(int Id);
    TArray<FVector2D> ReadCurve() const;
    FString LiveText() const;
};
