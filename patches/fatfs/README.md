# FatFs 补丁

对 `Middlewares/Third_Party/FatFs/src/ff.c`（FatFs R0.12c）的本仓库补丁，依据 [ADR-0022](../../docs/adr/0022-fatfs-open-by-locator-patch.md)。Vendor 目录不手改，只经补丁文件维护（HAR-15）。

| 补丁 | 内容 |
|---|---|
| `0001-open-by-dirent.patch` | 只新增 `f_open_dirent`：按目录项位置读出目录项、核对（是文件、未删除、非目录、起始簇与长度一致），像 `f_open` 结尾那样填 FIL 并登记锁。原型在 `Service/filesystem/filesystem_ff_patch.h`，不进 `ff.h`，不改 `ffconf.h`。 |

补丁以 LF 存放（`.gitattributes` 固定）。

## 重打

CubeMX 重新导出或更新 FatFs 后，先运行：

```powershell
./scripts/apply-vendor-patches.ps1
```

可重复执行：已应用的跳过，能干净应用的应用，冲突则停下并报出 git 给出的文件与行位置，交人工改写 patch。`-Check` 只读核对，供 FAST 使用，未生效时报“FatFs 补丁未应用”。脚本只改工作树；`Middlewares/` 的改动由用户设 `ALLOW_GENERATED_UPDATE` 后提交（HAR-4），助手不设置该变量。

## 新增补丁

新补丁放进 `patches/<上游>/`，文件名按序号递增；在 `ff.c` 副本上改好后以 `git diff --no-index` 生成（路径折成 `a/Middlewares/...`、`b/Middlewares/...`），保存为 LF。主机测试模块 `Tests/filesystem_locator` 会把全部补丁应用到 `ff.c` 副本上，改动补丁后先跑它。
