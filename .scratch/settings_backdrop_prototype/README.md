# Settings Backdrop Prototype（可删除）

问题：在 `Boot → Lock → Settings → 更换壁纸` 的生命周期中，多张同时显示的圆角设置卡片能否共享一次全屏壁纸模糊，并让卡片外区域保持清晰，同时不让不同卡片或后续视觉效果覆写正在显示的像素数据？

本原型只建模缓冲区所有权；不包含 LVGL、SquareLine、文件系统、缓存或硬件。结论验证后删除本目录，正式实现仅吸收已确认的资源模型。

运行：

```powershell
gcc -std=c11 -Wall -Wextra -Werror backdrop_model.c main.c -o backdrop_prototype.exe; .\backdrop_prototype.exe
```

预期模型：

- `effect workspace`：一块可复用的完整 Alpha Canvas 工作缓冲；Boot 或重新合成 Settings 时独占。
- `settings composite`：另一块长期完整帧，包含清晰壁纸以及所有卡片圆角区域内的模糊壁纸；Settings 显示期间不能被工作区复用。
- 四张卡片不需要各自持有全屏或裁剪后的长期 Canvas；卡片数量只影响一次合成时的裁剪次数。
- 更换壁纸或改变卡片布局时，重新生成 `settings composite`；正常静止显示不重复运算。

当前 Alpha 原型的峰值为两张 `240 × 320 × 3 B` 帧，即 `460800 B`（约 `450 KiB`）。正式统一为不透明 RGB565 后，对应峰值约 `300 KiB`。
