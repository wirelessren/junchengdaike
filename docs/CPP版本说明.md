# C++ 版本说明

## 技术选型

- GUI: Qt Widgets
- 数据库: Qt SQL + SQLite
- Excel 导入导出: `xlnt` 负责 `xlsx`，`FreeXL` 负责原生 `.xls` 读取，`.xls` 导出由 C++ 端原生生成 Excel 可打开的工作簿格式，不依赖宿主机 Excel

## 当前已迁移内容

- 课表 Excel 导入并写入 `教师课表.db`
- 代课任务列表、任务新建、打开、保存、删除
- 基于已有任务继承代课安排
- 被代课老师课表渲染
- 同科目代课老师推荐
- 代课安排增删改
- 无课名单筛选、复制、导出
- 通知单模板导出

## 与 Python 版本的当前对齐情况

- 老师搜索已经支持“姓名 + 首字母”双匹配。
- 通知单导出已经恢复为“单工作表连续拼接多个通知”的输出方式。
- 被代课课表已经补齐 Python 版的交互细节：无原课节次提示、右键直接取消代课、左键单击选择逻辑。
- 当前已知的核心业务功能差异已清零；后续若继续扩展，重点会是界面微调和更细的流程回归测试。

## 构建依赖

需要安装以下组件：

- Qt 5 或 Qt 6
- Qt Widgets
- Qt Sql
- CMake 3.21+
- MSVC 或 MinGW

项目已经内置 `third_party/xlnt`，运行时不依赖 Excel。

## 目录说明

- `CMakeLists.txt`: C++ 工程入口
- `src/mainwindow.*`: 主界面和业务逻辑
- `src/datastore.*`: SQLite 数据层
- `src/excelhelper.*`: `xlsx/xls` 读写与导出
- `src/coreutils.*`: 归一化和公共逻辑
- `tools/smoke_check.cpp`: 导入/导出冒烟校验程序
- `scripts/build_ucrt64.ps1`: 一键构建脚本
- `scripts/package_ucrt64.ps1`: 一键打包脚本
- `scripts/smoke_validate_ucrt64.ps1`: 一键功能校验脚本

## 推荐构建方式

当前已验证通过的方案是 `MSYS2 UCRT64 + Qt6 + g++`。

直接执行：

```powershell
.\scripts\build_ucrt64.ps1
```

打包执行：

```powershell
.\scripts\package_ucrt64.ps1
```

功能校验执行：

```powershell
.\scripts\smoke_validate_ucrt64.ps1
```

## 打包产物

- 可执行目录：`dist/均程代课管理-ucrt64`
- 压缩包：`dist/均程代课管理-ucrt64.zip`

打包目录会带上：

- `均程代课管理.exe`
- Qt 运行库和插件
- MinGW/UCRT64 运行库
- `教师课表统计.xlsx`
- `教师源课表.xlsx`
- `通知单模版.xlsx`
- `教师课表.db`

## 运行要求

- `教师课表统计.xlsx`
- `通知单模版.xlsx`
- `教师课表.db`

建议与生成的 exe 放在同一目录。

当前课表导入支持 `xlsx` / `xls`，导出支持 `xlsx` / `xls`。

## 本机验证结果

已于 `2026-03-26` 在当前机器完成以下验证：

- 成功编译 `均程代课管理.exe`
- 成功生成独立打包目录和 zip
- 在干净 `PATH` 环境下直接启动打包后的 exe，8 秒内无崩溃
- 运行 `SubstituteSmokeCheck.exe` 完成导入/导出冒烟校验

本次冒烟校验结果：

- 样例课表读取条数：`1110`
- 数据库导入后回读条数：`1110`
- 无课名单导出：通过
- 无课名单导出 xls：通过
- 通知单导出：通过
- 通知单导出 xls：通过

冒烟输出目录：

- `build-ucrt64/smoke-output/free_teachers_smoke.xlsx`
- `build-ucrt64/smoke-output/free_teachers_smoke.xls`
- `build-ucrt64/smoke-output/notices_smoke.xlsx`
- `build-ucrt64/smoke-output/notices_smoke.xls`

## 备注

- 本机检测到可用 `cl.exe`，但当前已验证的 Qt 工具链来自 `MSYS2 UCRT64`，所以实际构建采用 `g++`。如果要切换到 `cl/MSVC`，需要安装与 MSVC ABI 匹配的 Qt 版本。
- `xlnt` 在新版本 CMake/GCC 下仍有少量第三方兼容性警告，但当前不影响构建、打包和运行。
