# STM32 GPIO EXTI Adapter

本 Adapter 独占 `HAL_GPIO_EXTI_Callback()`，按照 GPIO PinMask 将 EXTI 边沿分发给长期注册的调用者节点。它使用侵入式单向链表，节点由调用者所有。

## 公开 Interface

- `GPIOEXTI_STM32HALAdapter_Register()`；
- `GPIOEXTI_STM32HALAdapter_Unregister()`；
- 回调节点、Handler 类型和状态类型。

## 调用的 Interface

- HAL GPIO EXTI 回调与 CMSIS 临界区原语。

## 资源与约束

- 调用者必须在普通上下文注册/注销，并在注册期间保持节点有效；
- 分发发生在 ISR，上层 Handler 只能执行常数时间操作；
- 不认识 SD、按键、PMIC 等产品语义，不能在此加入二次业务分发；
- 保存中断状态再关闭/恢复 IRQ 时，必须原样恢复调用前的 PRIMASK 状态。
