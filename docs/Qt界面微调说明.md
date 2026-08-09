# Qt 界面微调说明

当前主界面仍然是 `Qt Widgets + C++` 搭建，业务逻辑没有整体迁移到 `.ui`，但已经补齐了适合本地微调的能力：

- 支持外部 `QSS` 覆盖文件
- 支持 `--ui-preview` 预览模式
- 关键控件都补了 `objectName`
- 常用布局尺寸集中在 `src/uimetrics.h`
- 设置页和关于页已经迁到 `.ui`
- 主要页签的静态属性已经尽量下沉到 `.ui`
- 页签顺序和标题现在由 `ui/main_window_shell.ui` 管理

目前可直接用 Qt Designer 打开的文件：

- `ui/main_window_shell.ui`
- `ui/edit_tab.ui`
- `ui/free_tab.ui`
- `ui/task_tab.ui`
- `ui/settings_tab.ui`
- `ui/about_tab.ui`

## 1. 启动预览模式

先编译：

```powershell
.\scripts\build_ucrt64.ps1
```

再启动：

```powershell
.\scripts\run_ui_preview.ps1
```

等价命令：

```powershell
.\build-ucrt64\均程代课管理.exe --ui-preview --style ui/local_override.qss
```

预览模式下窗口会以固定尺寸打开，便于反复对比界面细节。

## 2. 使用本地 QSS 覆盖

仓库里已经带了一个可直接编辑的 [`ui/local_override.qss`](../ui/local_override.qss)。
如果你想恢复示例内容，也可以重新复制模板：

```powershell
Copy-Item ui\local_override.qss.example ui\local_override.qss -Force
```

程序启动后会自动加载 `ui/local_override.qss`。
如果文件已经加载，保存后会自动重新应用样式，不需要重新启动程序。

适合放进 `QSS` 的内容：

- 颜色
- 圆角
- 边框
- 字体大小
- 内边距
- 按钮高度
- 表格边框和选中颜色

## 3. 常用控件名称

可以直接在 `ui/local_override.qss` 里写这些选择器：

- `#mainWindow`
- `#mainTabs`
- `#taskTable`
- `#taskEditGroup`
- `#teacherSearchEdit`
- `#teacherCombo`
- `#absentScheduleTable`
- `#recommendTable`
- `#assignButton`
- `#subScheduleTable`
- `#assignmentTable`
- `#freeScheduleTable`
- `#freeListTable`
- `#schedulePathEdit`
- `#templatePathEdit`
- `#statusSummaryLabel`

## 4. 调整布局尺寸

`QSS` 主要负责视觉样式。
如果你要改布局和尺寸，直接改 [`src/uimetrics.h`](../src/uimetrics.h) 里的集中参数，然后重新编译。

常用参数包括：

- 窗口初始大小
- 主边距和间距
- 表格行高
- 按钮最小宽高
- 左中右面板最小宽度
- 拆分器默认比例

## 5. 推荐工作流

1. 用 `ui/local_override.qss` 调颜色、圆角、字体和控件观感
2. 用 `src/uimetrics.h` 调尺寸、间距和分栏比例
3. 只有需要改控件结构时，再进 `src/mainwindow.cpp`
