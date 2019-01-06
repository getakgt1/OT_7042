
#include <linux/types.h>
#include "stk_cust_alsps.h"
#ifdef MT6573
#include <mach/mt6573_pll.h>
#endif
#ifdef MT6575
#include <mach/mt6575_pm_ldo.h>
#endif
#ifdef MT6577
#include <mach/mt6577_pm_ldo.h>
#endif
#ifdef MT6582
#include <mach/mt_pm_ldo.h>
#endif

static struct stk_alsps_hw stk_cust_alsps_hw = {
	.i2c_num = 2,		/* i2c bus number, for mt657x, default=0. For mt6589, default=3 */
	//.polling_mode =1,
	.polling_mode_ps = 0,
	.polling_mode_als = 1,
	.power_id = MT65XX_POWER_NONE,	/*LDO is not used */
	.power_vol = VOL_DEFAULT,	/*LDO is not used */
	.i2c_addr = {0x90, 0x00, 0x00, 0x00},	/*STK3x1x */
#if 0
	// .als_level  = {5,  9, 36, 59, 82, 132, 205, 273, 500, 845, 1136, 1545, 2364, 4655, 6982}, /* als_code */
	.als_level = {4, 40, 80, 120, 160, 250, 400, 800, 1200, 1600, 2000, 3000, 5000, 10000, 65535},	/* als_code */
	.als_value = {0, 10, 40, 65, 90, 145, 225, 300, 550, 930, 1250, 1700, 2600, 5120, 7680, 10240},	/* lux */
#else
	.als_level =
	    {5, 28, 28, 28, 100, 100, 200, 1000, 2000, 3000, 6000, 10000, 14000,
	     18000, 20000},
	.als_value =
	    {0, 20, 20, 20, 90, 90, 225, 320, 640, 1280, 1280, 2600,
	     2600, 2600, 10240, 10240},
#endif

	.state_val = 0x0,	/* disable all */
	.psctrl_val = 0x71,	/* ps_persistance=4, ps_gain=64X, PS_IT=0.391ms */
	.alsctrl_val = 0x39,	/* als_persistance=1, als_gain=64X, ALS_IT=100ms */
	.ledctrl_val = 0xFF,	/* 100mA IRDR, 64/64 LED duty */
	.wait_val = 0x10,	/* 100 ms */
	.ps_high_thd_val = 1700,
	.ps_low_thd_val = 1500
};

struct stk_alsps_hw *stk_get_cust_alsps_hw(void)
{
	return &stk_cust_alsps_hw;
}
