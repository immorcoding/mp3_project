# GUI · appearance

颜色来源、外观与系统背景。

[返回 GUI](gui.md)

### Rules

- **GUI-2** · provisional · 颜色只来自 theme/ 按颜色属性×调色板角色持有的共享 lv_style_t；外观切换更新共享颜色并 report_style_change，不用 color filter 或遍历对象改色；Screen 背景透明度与壁纸由 gui_service.c 编排。_Why:_ 角色共享使换色独立于对象树和 remove_style_all。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-5** · provisional · Accent 表示活动、Ink 表示文字图标、Muted 表示低对比轨道、Wash 表示薄填充、Ground 表示页底；Opa 属于对象；Default 使用无业务内容的系统壁纸与 Music 玻璃，Solid 关闭壁纸和模糊。_Why:_ 语义角色与效果开关共同保持两套外观一致，壁纸不绑定页面布局。_Source:_ [主题实现](../../Service/gui/README.md)

### References

- [Default 基线](../../Tools/gui_simulator/scenarios/theme_default.expected)、[外观切换基线](../../Tools/gui_simulator/scenarios/theme_toggle.expected)：已批准画面的哈希，截图由同名场景重现后审阅。

### Rejected

- 两色渐变与运行时渐变抖动：真机色带或颗粒不满足雾状背景；保留静态壁纸，LV_DITHER_GRADIENT 为 0。
