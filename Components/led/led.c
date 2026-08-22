/**
  ******************************************************************************
  * @file    led.c
  * @brief   LED Device 初始化与逻辑亮灭状态管理实现。
  ******************************************************************************
  */

#include "Components/led/led.h"

#include <stddef.h>

/**
  * @brief  清除 LED Handle 中保存的最近一次错误诊断。
  * @param  hled LED Device Handle。
  */
static void led_clear_error(LED_HandleTypeDef *hled)
{
    hled->ErrorCode = LED_ERROR_NONE;
    hled->LastPortStatus = LED_PORT_OK;
}

/**
  * @brief  统一记录 LED Device 的失败阶段、Port 状态和生命周期状态。
  * @param  hled LED Device Handle。
  * @param  error Device 层失败原因。
  * @param  port_status 归一化后的底层 Port 状态。
  * @param  lifecycle_state 失败后写入的生命周期状态。
  * @retval LED_ERROR。
  */
static LED_StatusTypeDef led_fail(LED_HandleTypeDef *hled,
                                  LED_ErrorTypeDef error,
                                  LED_PortStatusTypeDef port_status,
                                  LED_LifecycleStateTypeDef lifecycle_state)
{
    hled->ErrorCode = error;
    hled->LastPortStatus = port_status;
    hled->LifecycleState = lifecycle_state;
    return LED_ERROR;
}

/**
  * @brief  初始化已绑定 LED Port，并把 LED 强制置为逻辑关闭状态。
  * @param  hled 已由具体 Adapter 安装 PortOps 和 PortContext 的 LED Device Handle。
  * @retval LED_OK Port 初始化成功且已收到 OFF 请求。
  * @retval LED_ERROR Handle 无效、Adapter 未完整绑定或任一 Port 操作失败。
  * @note   OFF 是逻辑语义；实际输出高低电平完全由具体 Adapter 决定。只有 Port
  *         成功完成 OFF 请求后，Handle 才进入 READY，避免软件状态与物理状态不一致。
  */
LED_StatusTypeDef LED_Init(LED_HandleTypeDef *hled)
{
    LED_PortStatusTypeDef port_status;

    if (hled == NULL)
    {
        return LED_ERROR;
    }

    hled->LifecycleState = LED_LIFECYCLE_RESET;
    led_clear_error(hled);

    if ((hled->PortOps == NULL) ||
        (hled->PortOps->Initialize == NULL) ||
        (hled->PortOps->SetState == NULL) ||
        (hled->PortContext == NULL))
    {
        return led_fail(hled,
                        LED_ERROR_PORT_NOT_BOUND,
                        LED_PORT_OK,
                        LED_LIFECYCLE_ERROR);
    }

    hled->LifecycleState = LED_LIFECYCLE_BUSY;
    port_status = hled->PortOps->Initialize(hled->PortContext);
    if (port_status != LED_PORT_OK)
    {
        return led_fail(hled,
                        LED_ERROR_PORT_INITIALIZE,
                        port_status,
                        LED_LIFECYCLE_ERROR);
    }

    port_status = hled->PortOps->SetState(hled->PortContext, LED_OFF);
    if (port_status != LED_PORT_OK)
    {
        return led_fail(hled,
                        LED_ERROR_PORT_SET,
                        port_status,
                        LED_LIFECYCLE_ERROR);
    }

    hled->OnOffState = LED_OFF;
    led_clear_error(hled);
    hled->LifecycleState = LED_LIFECYCLE_READY;
    return LED_OK;
}

/**
  * @brief  向已初始化 LED 提交逻辑 ON 或 OFF 请求。
  * @param  hled 已完成初始化的 LED Device Handle。
  * @param  state 请求的逻辑亮灭状态。
  * @retval LED_OK Port 已接受请求，Handle 中的逻辑状态已同步更新。
  * @retval LED_ERROR 参数、绑定或生命周期无效，或 Port 未接受请求。
  * @note   本函数不把 ON/OFF 翻译为高低电平。若 Port 返回失败，保留旧的
  *         OnOffState，并回到 READY 以允许调用者在瞬态总线故障后重试。
  */
LED_StatusTypeDef LED_Set(LED_HandleTypeDef *hled, LED_OnOffTypeDef state)
{
    LED_PortStatusTypeDef port_status;

    if (hled == NULL)
    {
        return LED_ERROR;
    }

    if ((state != LED_OFF) && (state != LED_ON))
    {
        return led_fail(hled,
                        LED_ERROR_INVALID_PARAM,
                        LED_PORT_OK,
                        hled->LifecycleState);
    }

    if ((hled->PortOps == NULL) ||
        (hled->PortOps->SetState == NULL) ||
        (hled->PortContext == NULL))
    {
        return led_fail(hled,
                        LED_ERROR_PORT_NOT_BOUND,
                        LED_PORT_OK,
                        LED_LIFECYCLE_ERROR);
    }

    if (hled->LifecycleState != LED_LIFECYCLE_READY)
    {
        return led_fail(hled,
                        LED_ERROR_NOT_READY,
                        LED_PORT_OK,
                        hled->LifecycleState);
    }

    hled->LifecycleState = LED_LIFECYCLE_BUSY;
    port_status = hled->PortOps->SetState(hled->PortContext, state);
    if (port_status != LED_PORT_OK)
    {
        return led_fail(hled,
                        LED_ERROR_PORT_SET,
                        port_status,
                        LED_LIFECYCLE_READY);
    }

    hled->OnOffState = state;
    led_clear_error(hled);
    hled->LifecycleState = LED_LIFECYCLE_READY;
    return LED_OK;
}

/**
  * @brief  翻转已初始化 LED 的逻辑亮灭状态。
  * @param  hled 已完成初始化的 LED Device Handle。
  * @retval LED_OK 已向 Port 成功提交与当前逻辑状态相反的请求。
  * @retval LED_ERROR Handle 或生命周期无效，或底层 Port 拒绝状态更新。
  * @note   不读取 GPIO 输出锁存器或 I2C 寄存器；Device 只以最后一次成功提交的
  *         OnOffState 为依据，并复用 LED_Set() 保证失败语义一致。
  */
LED_StatusTypeDef LED_Toggle(LED_HandleTypeDef *hled)
{
    if (hled == NULL)
    {
        return LED_ERROR;
    }

    return LED_Set(hled, (hled->OnOffState == LED_ON) ? LED_OFF : LED_ON);
}
