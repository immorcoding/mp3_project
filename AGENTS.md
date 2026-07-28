# AGENTS.md

## 项目说明

本项目是基于 STM32H743 的便携式媒体播放器固件。

当前主要功能包括：

- AXP2101 电源管理；
- PCM5102A I2S 音频输出；
- SDMMC SD 卡访问与热插拔；
- USB CDC 日志；
- FreeRTOS；
- 后续将加入 FatFs、音频解码、LVGL、LCD、QSPI Flash 和 USB MSC。

技术文档和代码注释统一使用中文。

## 开始工作前

涉及架构、模块边界或公共接口修改前，必须先阅读：

- `CONTEXT.md`
- `docs/architecture_standard.md`
- 与当前模块对应的 `docs/*.md`

不要仅根据目录名称推测职责，应结合现有代码和文档判断。

## 工程分层

工程主要遵循以下逻辑分层：

```text
Vendor/HAL
    ↓
Adapters
    ↓
Components
    ↓
Platform
    ↓
Application

具体的情况参考每个工程目录下的 分层.png