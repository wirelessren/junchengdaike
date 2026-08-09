#pragma once

#include <array>

namespace substitute::ui {

inline constexpr int kInitialWindowWidth = 1320;
inline constexpr int kInitialWindowHeight = 860;
inline constexpr int kPreviewWindowWidth = 1400;
inline constexpr int kPreviewWindowHeight = 900;

inline constexpr double kAppFontPointSize = 10.0;
inline constexpr double kScheduleTableFontPointSize = 8.7;
inline constexpr double kTabFontPointDelta = 1.0;

inline constexpr int kMainMargin = 16;
inline constexpr int kMainSpacing = 8;
inline constexpr int kSectionSpacing = 8;
inline constexpr int kCompactSpacing = 6;
inline constexpr int kTightSpacing = 4;
inline constexpr int kTableRowHeight = 34;
inline constexpr int kSplitterHandleWidth = 6;

inline constexpr int kStatusLabelMinHeight = 32;
inline constexpr int kStatusLabelMaxHeight = 36;

inline constexpr int kDefaultButtonMinWidth = 96;
inline constexpr int kDefaultButtonMinHeight = 30;
inline constexpr int kBaseTaskComboMinWidth = 220;
inline constexpr int kTaskActionsGap = 12;
inline constexpr int kTaskGroupMinHeight = 88;
inline constexpr int kTaskGroupMaxHeight = 96;
inline constexpr int kTaskGroupHorizontalSpacing = 8;
inline constexpr int kTaskGroupVerticalSpacing = 6;
inline constexpr int kTaskGroupMarginHorizontal = 8;
inline constexpr int kTaskGroupMarginVertical = 6;
inline constexpr int kLeftPanelMinWidth = 460;
inline constexpr int kMiddlePanelMinWidth = 152;
inline constexpr int kMiddlePanelMaxWidth = 170;
inline constexpr int kRightPanelMinWidth = 460;
inline constexpr int kAssignmentTableMinHeight = 180;
inline constexpr int kFreeSubjectComboMinWidth = 140;

inline constexpr int kSaveTaskButtonMinWidth = 88;
inline constexpr int kClearSelectionButtonMinWidth = 74;
inline constexpr int kAssignButtonMinWidth = 84;
inline constexpr int kDeleteAssignmentButtonMinWidth = 88;
inline constexpr int kExportAssignmentsButtonMinWidth = 98;
inline constexpr int kFreeExportButtonMinWidth = 102;
inline constexpr int kScheduleBrowseButtonMinWidth = 76;
inline constexpr int kTemplateBrowseButtonMinWidth = 84;
inline constexpr int kUpdateSettingsButtonMinWidth = 84;

inline constexpr std::array<int, 3> kEditSplitterSizes = {440, 120, 440};
inline constexpr std::array<int, 2> kContentSplitterSizes = {700, 300};

}  // namespace substitute::ui
