/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <soc/tegra/pmc.h>

#include "nouveau_drm.h"
#include "nouveau_platform.h"

static int nouveau_platform_power_up(struct nouveau_platform_gpu *gpu)
{
	int err;

	err = regulator_enable(gpu->vdd);
	if (err)
		goto err_power;

	err = clk_prepare_enable(gpu->clk);
	if (err)
		goto err_clk;
	err = clk_prepare_enable(gpu->clk_pwr);
	if (err)
		goto err_clk_pwr;
	clk_set_rate(gpu->clk_pwr, 204000000);
	udelay(10);

	reset_control_assert(gpu->rst);
	udelay(10);

	err = tegra_powergate_remove_clamping(TEGRA_POWERGATE_3D);
	if (err)
		goto err_clamp;
	udelay(10);

	reset_control_deassert(gpu->rst);
	udelay(10);

	return 0;

err_clamp:
	clk_disable_unprepare(gpu->clk_pwr);
err_clk_pwr:
	clk_disable_unprepare(gpu->clk);
err_clk:
	regulator_disable(gpu->vdd);
err_power:
	return err;
}

static int nouveau_platform_power_down(struct nouveau_platform_gpu *gpu)
{
	int err;

	reset_control_assert(gpu->rst);
	udelay(10);

	clk_disable_unprepare(gpu->clk_pwr);
	clk_disable_unprepare(gpu->clk);
	udelay(10);

	err = regulator_disable(gpu->vdd);
	if (err)
		return err;

	return 0;
}

struct nouveau_platform_probe_data {
	const char *soc;
	const char *gpu;
	bool use_ext_firmware;
};

static const struct firmware *
nouveau_platform_load_netlist_fw(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct nouveau_platform_probe_data *pdata;
	const struct firmware *ctxsw_fw;
	char f[32];
	int err;

	match = of_match_device(pdev->dev.driver->of_match_table, &pdev->dev);
	if (!match) {
		dev_warn(&pdev->dev, "cannot find OF match for device\n");
		return NULL;
	}
	pdata = match->data;
	if (!pdata) {
		dev_warn(&pdev->dev, "no probe data for device\n");
		return NULL;
	}

	if (!pdata->use_ext_firmware)
		return NULL;

	err = snprintf(f, sizeof(f), "nvidia/%s/%s_ctxsw.bin", pdata->soc,
		 pdata->gpu);
	if (err >= sizeof(f)) {
		dev_err(&pdev->dev, "firmware path too long (max %d)\n",
			 sizeof(f));
		return ERR_PTR(-ENOSPC);
	}

	err = request_firmware(&ctxsw_fw, f, &pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "error loading firmware: %d\n", err);
		return ERR_PTR(err);
	}

	return ctxsw_fw;
}

static int nouveau_platform_probe(struct platform_device *pdev)
{
	struct nouveau_platform_gpu *gpu;
	struct nouveau_platform_device *device;
	struct drm_device *drm;
	int err;

	gpu = devm_kzalloc(&pdev->dev, sizeof(*gpu), GFP_KERNEL);
	if (!gpu)
		return -ENOMEM;

	gpu->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(gpu->vdd))
		return PTR_ERR(gpu->vdd);

	gpu->rst = devm_reset_control_get(&pdev->dev, "gpu");
	if (IS_ERR(gpu->rst))
		return PTR_ERR(gpu->rst);

	gpu->clk = devm_clk_get(&pdev->dev, "gpu");
	if (IS_ERR(gpu->clk))
		return PTR_ERR(gpu->clk);

	gpu->clk_pwr = devm_clk_get(&pdev->dev, "pwr");
	if (IS_ERR(gpu->clk_pwr))
		return PTR_ERR(gpu->clk_pwr);

	gpu->ctxsw_fw = nouveau_platform_load_netlist_fw(pdev);
	if (IS_ERR(gpu->ctxsw_fw))
		return PTR_ERR(gpu->ctxsw_fw);

	err = nouveau_platform_power_up(gpu);
	if (err)
		goto release_fw;

	drm = nouveau_platform_device_create(pdev, &device);
	if (IS_ERR(drm)) {
		err = PTR_ERR(drm);
		goto power_down;
	}

	device->gpu = gpu;

	err = drm_dev_register(drm, 0);
	if (err < 0)
		goto err_unref;

	/*
	 * we can release the firmware now since it has been consumed during
	 * probe
	 */
	goto release_fw;

err_unref:
	drm_dev_unref(drm);

power_down:
	nouveau_platform_power_down(gpu);

release_fw:
	if (gpu->ctxsw_fw)
		release_firmware(gpu->ctxsw_fw);

	return err;
}

static int nouveau_platform_remove(struct platform_device *pdev)
{
	struct drm_device *drm_dev = platform_get_drvdata(pdev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nouveau_device *device = nvkm_device(&drm->device);
	struct nouveau_platform_gpu *gpu = nv_device_to_platform(device)->gpu;

	nouveau_drm_device_remove(drm_dev);

	return nouveau_platform_power_down(gpu);
}

#if IS_ENABLED(CONFIG_OF)
static struct nouveau_platform_probe_data gk20a_probe_data = {
	.soc = "tegra124",
	.gpu = "gk20a",
	.use_ext_firmware = true,
};

static const struct of_device_id nouveau_platform_match[] = {
	{
		.compatible = "nvidia,gk20a",
		.data = &gk20a_probe_data,
	},
	{ }
};

MODULE_DEVICE_TABLE(of, nouveau_platform_match);
#endif

struct platform_driver nouveau_platform_driver = {
	.driver = {
		.name = "nouveau",
		.of_match_table = of_match_ptr(nouveau_platform_match),
	},
	.probe = nouveau_platform_probe,
	.remove = nouveau_platform_remove,
};

module_platform_driver(nouveau_platform_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
