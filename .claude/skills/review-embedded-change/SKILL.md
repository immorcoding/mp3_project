---
name: review-embedded-change
description: Review MCU-sensitive firmware changes involving DMA, cache coherency, ISR callbacks, FreeRTOS synchronization, STM32 HAL adapters, Platform assembly, linker sections, CubeMX configuration, timing, or hardware resource lifetime. Use automatically when such behavior changes; do not use for pure host-only algorithms or documentation-only edits.
---

<!-- 由 scripts/sync-agent-config.ps1 从 .agents/skills/review-embedded-change/SKILL.md 生成，勿手改。 -->

# 嵌入式敏感变更审阅

先读取 [嵌入式审阅清单](references/embedded-checklist.md)，再结合相关架构文档和实际 diff 审阅。调用只读 `embedded-reviewer`，并让它独立形成第一遍结论，不向它灌输主 Agent 的预期答案。

审阅不能替代确定性脚本或上板。先确保适用的 FAST/CHANGED/FULL 已通过；若结论依赖真实中断顺序、DMA、Cache、掉电、总线时序或外设状态，最终状态保持 `NEEDS_HARDWARE_VALIDATION`，直到用户提供板级证据。
