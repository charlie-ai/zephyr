/*
 * Copyright (c) 2023 Telink Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Linker script for the Telink B9x SoCs
 */

#include <zephyr/devicetree.h>
#include <zephyr/linker/linker-tool.h>

MEMORY
{
#if CONFIG_BOARD_TLSR9518ADK80D || CONFIG_BOARD_TLSR9528A
	RAMILM       (rwx) : ORIGIN = DT_REG_ADDR(DT_NODELABEL(ram_ilm)),              LENGTH = DT_REG_SIZE(DT_NODELABEL(ram_ilm))
#endif /* CONFIG_BOARD_TLSR9518ADK80D */

#if defined(CONFIG_BOARD_TLSR9518ADK80D_RETENTION) || defined(CONFIG_BOARD_TLSR9528A_RETENTION)
    RAM_ILM_N    (rwx) : ORIGIN = DT_REG_ADDR(DT_NODELABEL(ram_ilm_nonretention)), LENGTH = DT_REG_SIZE(DT_NODELABEL(ram_ilm_nonretention))
    RAM_DLM      (rwx) : ORIGIN = DT_REG_ADDR(DT_NODELABEL(ram_dlm)),              LENGTH = DT_REG_SIZE(DT_NODELABEL(ram_dlm))
#endif /* CONFIG_BOARD_TLSR9518ADK80D_RETENTION || CONFIG_BOARD_TLSR9528A_RETENTION */
}

#include <zephyr/arch/riscv/common/linker.ld>

#ifdef CONFIG_MCUBOOT
	ASSERT(__rom_region_size <= DT_REG_SIZE(DT_NODELABEL(boot_partition)), "boot_partition overflows")
#endif
