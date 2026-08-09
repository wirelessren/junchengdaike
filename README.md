# 均程代课管理

一款面向学校代课安排场景的 Windows 桌面工具，使用 C++20 和 Qt Widgets 开发。

## 主要功能

- 从 `xlsx` / `xls` 课表导入教师上课信息
- 新建、编辑、搜索、排序和管理代课任务
- 按学科推荐代课教师，查询指定时间的无课教师
- 根据 Excel 模板生成代课通知单
- 备份、恢复和导出代课数据
- 按时间段或全部导出代课统计
- 自适应不同学校的每周天数和每日节数

## 技术栈

- Qt 5/6 Widgets + Qt SQL
- SQLite
- xlnt（`xlsx` 读写，已内置源码）
- FreeXL（`xls` 读取）
- CMake 3.21+ / C++20

## 构建

已验证的构建环境是 Windows + MSYS2 UCRT64 + Qt 6 + Ninja。请先安装 Qt、CMake、Ninja、GCC 和 FreeXL，然后执行：

```powershell
.\scripts\build_ucrt64.ps1
```

如果 MSYS2 不在常见安装位置，请先设置：

```powershell
$env:MSYS2_ROOT = "D:\msys64"
```

打包和功能校验：

```powershell
.\scripts\package_ucrt64.ps1
.\scripts\smoke_validate_ucrt64.ps1
```

默认打包不包含本地课表、数据库和通知单模板。如果是校内使用并确定需要携带当前数据，可执行：

```powershell
.\scripts\package_ucrt64.ps1 -IncludeDataFiles
```

更详细的工程说明见 [docs/CPP版本说明.md](docs/CPP%E7%89%88%E6%9C%AC%E8%AF%B4%E6%98%8E.md)。


## 许可证

本项目使用 [Apache License 2.0](LICENSE)。`third_party/xlnt` 使用其自带的许可证。
