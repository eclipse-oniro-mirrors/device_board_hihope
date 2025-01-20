/*
 * Copyright (C) 2022 HiHope Open Source Organization .
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <linux/proc_fs.h>
#include "es8323_codec_impl.h"

#define HDF_LOG_TAG "es8323_codec_linux"
struct es8323_priv *es8323_info;

static const struct regmap_config es8323_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= ES8323_DACCONTROL30,
	.cache_type	= REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};
struct es8323_priv *GetCodecDevice(void)
{
    return es8323_info;
}
static int es8323_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	
	int ret = -1;
	struct i2c_adapter *adapter = to_i2c_adapter(i2c->dev.parent);
	//char reg;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
		return -EIO;
	}

	es8323_info = devm_kzalloc(&i2c->dev, sizeof(struct es8323_priv), GFP_KERNEL);
	if (!es8323_info)
		return -ENOMEM;

	es8323_info->regmap = devm_regmap_init_i2c(i2c, &es8323_regmap_config);
	if (IS_ERR(es8323_info->regmap))
		return PTR_ERR(es8323_info->regmap);

	i2c_set_clientdata(i2c, es8323_info);

	return ret;
}

static int es8323_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id es8323_i2c_id[] = {
	{"es8323", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, es8323_i2c_id);


static const struct of_device_id es8323_of_match[] = {
	{ .compatible = "everest,es8323", },
	{ }
};
MODULE_DEVICE_TABLE(of, es8323_of_match);

static struct i2c_driver es8323_i2c_driver = {
	.driver = {
		.name = "ES8323",
		.of_match_table = of_match_ptr(es8323_of_match),
		},
	.probe = es8323_i2c_probe,
	.remove = es8323_i2c_remove,
	.id_table = es8323_i2c_id,
};
module_i2c_driver(es8323_i2c_driver);