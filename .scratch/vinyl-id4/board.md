# 板上事实：slug `vinyl-id4`

> 这台设备此刻的状态，合计约 25 行。格式与加载规则在 `docs/resource_pack_design.md`。

## 已写入

- Pack v2 由用户 External Loader 写入 NOR；HEX DFU **不**更新该包。
- 包文件：`build/external-resources/resource_pack.bin`，`package_version` 2，长度 `0x77000`，映射 `0x90401000`。
- 探针：PackageVersion 在 `0x90401028`，8 B 小端，应为 `2`。
- 固件提交 `201fe5b` 已由用户烧录。

## 已确认

- 假唱盘第一帧上板通过（深灰盘 + 青中心）。
- ID 4 真底图叠假封面已上板通过。

## 待确认

- 无。
