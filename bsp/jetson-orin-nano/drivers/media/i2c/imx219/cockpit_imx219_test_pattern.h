/* SPDX-License-Identifier: GPL-2.0-only */
/* Sensor-local additions to NVIDIA nv_imx219.c, jetson_36.5.2.
 * Included after imx219_write_reg/table; not a replacement upstream driver.
 */
#ifndef COCKPIT_IMX219_TEST_PATTERN_H
#define COCKPIT_IMX219_TEST_PATTERN_H

#define IMX219_TEST_PATTERN_REG 0x0600
#define IMX219_TEST_WINDOW_WIDTH 0x0624
#define IMX219_TEST_WINDOW_HEIGHT 0x0626

/* The menu matches the upstream V4L2 IMX219 convention; register values do not. */
static const char * const cockpit_imx219_patterns[] = {
	"Disabled", "Color Bars", "Solid Color", "Grey Color Bars", "PN9",
};
static const u8 cockpit_imx219_pattern_values[] = { 0, 2, 1, 3, 4 };
static const u32 cockpit_imx219_colour_cids[] = {
	V4L2_CID_TEST_PATTERN_RED, V4L2_CID_TEST_PATTERN_GREENR,
	V4L2_CID_TEST_PATTERN_BLUE, V4L2_CID_TEST_PATTERN_GREENB,
};

/* Every 16-bit sensor register is big endian over the existing 8-bit regmap.
 * Never OR errors together or continue after the first failed I2C operation.
 */
static int cockpit_imx219_write16(struct imx219 *priv, u16 reg, u16 value)
{
	int err = imx219_write_reg(priv->s_data, reg, value >> 8);

	return err ? err : imx219_write_reg(priv->s_data, reg + 1, value & 0xff);
}

/* Caller holds priv->state_lock and the sensor must already be powered. */
static int cockpit_imx219_apply_pattern(struct imx219 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	int i, err;

	if (priv->test_pattern_mode >= ARRAY_SIZE(cockpit_imx219_pattern_values))
		return -EINVAL;
	if (s_data->mode < 0 || s_data->mode >= ARRAY_SIZE(imx219_frmfmt))
		return -EINVAL;
	if (priv->test_pattern_mode) {
		err = cockpit_imx219_write16(priv, IMX219_TEST_WINDOW_WIDTH,
			imx219_frmfmt[s_data->mode].size.width);
		if (err)
			return err;
		err = cockpit_imx219_write16(priv, IMX219_TEST_WINDOW_HEIGHT,
			imx219_frmfmt[s_data->mode].size.height);
		if (err)
			return err;
		for (i = 0; i < ARRAY_SIZE(priv->test_colours); i++) {
			err = cockpit_imx219_write16(priv, 0x0602 + i * 2,
				priv->test_colours[i]);
			if (err)
				return err;
		}
	}
	return cockpit_imx219_write16(priv, IMX219_TEST_PATTERN_REG,
		cockpit_imx219_pattern_values[priv->test_pattern_mode]);
}

static int cockpit_imx219_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx219 *priv = ctrl->priv;
	u16 old_colours[4];
	u8 old_mode;
	int i, err = 0;

	if (!priv)
		return -EINVAL;
	mutex_lock(&priv->state_lock);
	old_mode = priv->test_pattern_mode;
	memcpy(old_colours, priv->test_colours, sizeof(old_colours));
	if (ctrl->id == V4L2_CID_TEST_PATTERN) {
		if (ctrl->val < 0 || ctrl->val >= ARRAY_SIZE(cockpit_imx219_patterns)) {
			err = -EINVAL;
			goto done;
		}
		priv->test_pattern_mode = ctrl->val;
	} else {
		for (i = 0; i < ARRAY_SIZE(cockpit_imx219_colour_cids); i++)
			if (ctrl->id == cockpit_imx219_colour_cids[i])
				break;
		if (i == ARRAY_SIZE(cockpit_imx219_colour_cids)) {
			err = -EINVAL;
			goto done;
		}
		priv->test_colours[i] = ctrl->val;
	}
	/* Cache while off. STREAMON reapplies after the vendor software reset. */
	if (priv->s_data->power->state == SWITCH_ON) {
		if (priv->needs_reset)
			err = -EIO;
		else
			err = cockpit_imx219_apply_pattern(priv);
		if (err) {
			priv->test_pattern_mode = old_mode;
			memcpy(priv->test_colours, old_colours, sizeof(old_colours));
			priv->needs_reset = true;
		}
	}
done:
	mutex_unlock(&priv->state_lock);
	return err;
}

static const struct v4l2_ctrl_ops cockpit_imx219_ctrl_ops = {
	.s_ctrl = cockpit_imx219_s_ctrl,
};

/* Called after tegracam creates the handler, before the VI notifier completes.
 * VI imports sensor controls on first open; do not replace its control ops.
 */
static int cockpit_imx219_registered(struct v4l2_subdev *sd)
{
	struct camera_common_data *s_data =
		container_of(sd, struct camera_common_data, subdev);
	struct imx219 *priv = s_data->priv;
	struct v4l2_ctrl *ctrl;
	int i;

	if (!priv || !sd->ctrl_handler)
		return -EINVAL;
	if (v4l2_ctrl_find(sd->ctrl_handler, V4L2_CID_TEST_PATTERN))
		return 0;
	ctrl = v4l2_ctrl_new_std_menu_items(sd->ctrl_handler,
		&cockpit_imx219_ctrl_ops, V4L2_CID_TEST_PATTERN,
		ARRAY_SIZE(cockpit_imx219_patterns) - 1, 0, 0,
		cockpit_imx219_patterns);
	if (ctrl)
		ctrl->priv = priv;
	for (i = 0; i < ARRAY_SIZE(cockpit_imx219_colour_cids); i++) {
		ctrl = v4l2_ctrl_new_std(sd->ctrl_handler, &cockpit_imx219_ctrl_ops,
			cockpit_imx219_colour_cids[i], 0, 1023, 1,
			i == 0 ? 1023 : 0);
		if (ctrl)
			ctrl->priv = priv;
	}
	if (sd->ctrl_handler->error)
		return sd->ctrl_handler->error;
	dev_info(s_data->dev, "cockpit BSP01: V4L2 sensor test pattern registered\n");
	return 0;
}
#endif
