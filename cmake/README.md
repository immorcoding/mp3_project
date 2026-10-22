# CMake

本目录保存 ARM GCC 工具链和 CubeMX 生成的构建描述。根 `CMakeLists.txt` 递归收集自维护源码目录。

## 约束

- 新增 `.c` 到 `APP`、`Service`、`Platform`、`Components`、`Adapters` 后由 `GLOB_RECURSE` 自动纳入；
- 不把临时试验 `.c` 放入这些目录；
- 外部库和生成目标的修改应保持在 `cmake/` 或根 CMake 中，不修改 Vendor 源；
- `fw_version.cmake` 由根 `CMakeLists.txt` 的 `fw_version` 目标在每次构建时运行 `git describe --always --dirty`，生成 `<构建目录>/generated/fw_version.h`（`FW_VERSION_STRING`、`FW_BUILD_TYPE_STRING`），内容不变不覆盖，因此不重新 configure 也能得到最新提交号且版本不变时不触发重编；`APP/tasks/app_tasks.c` 启动时以 `BOOT` 标签打印 `FW <提交号>[-dirty] <构建类型>`；
- 头文件包含使用工程根目录起始的完整路径，避免依赖隐式叶目录 IncludePath。

