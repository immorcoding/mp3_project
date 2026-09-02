# ADR-0014：Filesystem 卷感知文件与目录接口

- 状态：已接受、已实施
- 日期：2026-09-02
- 相关决定：[ADR-0005](0005-sd-filesystem-task-ownership.md)、[ADR-0010](0010-fatfs-user-diskio-service-ownership.md)、[ADR-0012](0012-filesystem-public-seams.md)、[ADR-0013](0013-usb-msc-product-scope.md)

## 背景

ADR-0012 把 Filesystem 公开接缝切成卷生命周期、文件访问和启动诊断，当时文件接口只服务已显式挂载的 Flash 卷：单文件槽、根目录 ASCII 名、无卷选择。APP 侧 SD 读写测试仍直接走 FatFs；设备还保留 `FormatSD`。

播放器需要把 SD 与 Flash 的文件访问统一到同一 Module：上层只看到 Volume 与 UTF-8 相对路径，不接触 FatFs 盘符、`FIL`/`DIR` 或后端私有头。Flash 仍是独立后端；SD 仍由用户在电脑上格式化。

## 决定

Filesystem 仍是一个 Module，`sd/` 与 `flash/` 仍是私有实现分区。不新建通用 BlockDevice，也不改成笼统的 `Service/storage`。

公开 Interface 按调用者切开：

- `filesystem_service.h`：卷生命周期。SD 为 `InitSD` / `MountSD` / `UnmountSD`。Flash 为 `InitFlash` / `MountFlash` / `UnmountFlash` / `FormatAndMountFlash` / `RecoverAndMountFlash` / `ReclaimFlash`。
- `filesystem_file.h`：卷感知文件访问。调用时必须给出 Volume；相对路径只在该卷内解释。
- `filesystem_directory.h`：卷感知目录访问。空路径表示所选卷的根目录。
- `filesystem_flash_access.h`：启动诊断，仍是唯一允许包含 `platform_flash.h` 的公开头。

删除公开旧符号，不为过渡保留薄包装：`FormatSD`、`OpenFlash`、`FormatFlash`、`RecoverFlash`、`OpenFlashFile`、`RemoveFlashFile`。设备侧删除 `storage_sd_format_and_mount`。

路径契约：UTF-8、正斜杠、禁止盘符/绝对路径/`\`/`.`/`..`/非法 UTF-8/超长路径。Service 内部加盘符并转为 FatFs TCHAR。文件和目录句柄是带代次的不透明 Token，槽位静态分配；一卷卸载、拔卡、格式化或恢复后，该卷 Token 立即失效。

Flash 格式化与恢复成功返回时卷必须已经挂载。启动路径不得因未格式化而自动格式化。`ReclaimFlash` 只给 FTL 一次有限回收机会。

## 不采用的方案与后果

- 抽出 `Service/storage` 或通用 BlockDevice：当前仍只有 Storage Task 一个块消费者，会得到浅转发。
- 保留 Flash 专属单槽 API 并另开一套 SD 文件 API：上层必须知道后端差异，后续媒体库无法用同一「来源卷 + 相对路径」模型。
- 为过渡保留 `FormatSD` / `OpenFlash` 等旧符号包装：调用面分裂，搜索无法证明旧语义已退出。
- 设备侧格式化 SD：未格式化卡应由用户在电脑上处理；播放器只报告 `SERVICE_NO_FILESYSTEM` 或告警。
- 启动自动格式化 Flash：会在扫描失败时销毁用户数据。
- 本轮实现跨任务文件请求队列，或允许 GUI 直接调用同步文件/格式化接口：执行上下文仍必须是唯一 Storage Task。

ADR-0012 正文中「文件级卷选择不适用于当前文件槽 Interface」由本 ADR 取代；接缝切开、不建 `Service/storage`/BlockDevice、`Maintain` 改名 `Reclaim`、诊断头携带 Platform 区域枚举等其余决定保持有效。编号使用 0014，因为 0013 已用于 USB MSC 产品范围。
