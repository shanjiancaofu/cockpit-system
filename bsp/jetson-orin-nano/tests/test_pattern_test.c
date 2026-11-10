/* SPDX-License-Identifier: GPL-2.0-only */
/* Host fault-injection test of the real sensor-local implementation header.
 * This mocks I2C/V4L2; it is deliberately NOT a loaded-driver hardware test.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <linux/v4l2-controls.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define SWITCH_ON 1
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))
#define dev_info(...) ((void)0)
struct v4l2_ctrl { u32 id; int val; void *priv; };
struct v4l2_ctrl_ops { int (*s_ctrl)(struct v4l2_ctrl *); };
struct v4l2_ctrl_handler { int error; int count; struct v4l2_ctrl controls[5]; };
struct v4l2_subdev { struct v4l2_ctrl_handler *ctrl_handler; };
struct power { int state; };
struct camera_common_data { struct v4l2_subdev subdev; void *priv; int mode; struct power *power; void *dev; };
struct imx219 {
 pthread_mutex_t state_lock;
 bool needs_reset;
 u8 test_pattern_mode;
 u16 test_colours[4];
 struct camera_common_data *s_data;
};
static const struct { struct { int width, height; } size; } imx219_frmfmt[] = {
 {{3280,2464}}, {{3280,1848}}, {{1920,1080}}, {{1640,1232}}, {{1280,720}}
};
static u16 addresses[64];
static u8 values[64];
static int writes, fail_at = -1;
static int imx219_write_reg(struct camera_common_data *data, u16 address, u8 value) {
 (void)data;
 addresses[writes] = address; values[writes] = value;
 return writes++ == fail_at ? -EREMOTEIO : 0;
}
static void mutex_lock(pthread_mutex_t *lock) { assert(!pthread_mutex_lock(lock)); }
static void mutex_unlock(pthread_mutex_t *lock) { assert(!pthread_mutex_unlock(lock)); }
static struct v4l2_ctrl *v4l2_ctrl_find(struct v4l2_ctrl_handler *handler, u32 id) {
 for(int i=0;i<handler->count;i++)if(handler->controls[i].id==id)return &handler->controls[i];
 return NULL;
}
static struct v4l2_ctrl *v4l2_ctrl_new_std(struct v4l2_ctrl_handler *handler, const struct v4l2_ctrl_ops *ops,
 u32 id, int min, int max, int step, int def) {
 (void)ops;(void)min;(void)max;(void)step;
 assert(handler->count<5);
 struct v4l2_ctrl *ctrl=&handler->controls[handler->count++];ctrl->id=id;ctrl->val=def;return ctrl;
}
static struct v4l2_ctrl *v4l2_ctrl_new_std_menu_items(struct v4l2_ctrl_handler *handler,
 const struct v4l2_ctrl_ops *ops,u32 id,int max,int mask,int def,const char *const *menu) {
 (void)mask;assert(!strcmp(menu[1],"Color Bars"));assert(max==4);
 return v4l2_ctrl_new_std(handler,ops,id,0,max,1,def);
}
#include "cockpit_imx219_test_pattern.h"
static void reset_io(void) { writes=0;fail_at=-1;memset(values,0,sizeof(values)); }
int main(void) {
 struct power power={0};struct camera_common_data data={.mode=2,.power=&power};
 struct imx219 sensor={.state_lock=PTHREAD_MUTEX_INITIALIZER,.s_data=&data,.test_colours={1023,0,0,0}};
 data.priv=&sensor;
 struct v4l2_ctrl control={.id=V4L2_CID_TEST_PATTERN,.val=1,.priv=&sensor};
 reset_io();assert(!cockpit_imx219_s_ctrl(&control));assert(writes==0);assert(sensor.test_pattern_mode==1);
 power.state=SWITCH_ON;
 reset_io();assert(!cockpit_imx219_apply_pattern(&sensor));assert(writes==14);
 assert(addresses[0]==0x0624 && values[0]==7 && values[1]==0x80); // 1920
 assert(addresses[2]==0x0626 && values[2]==4 && values[3]==0x38); // 1080
 assert(addresses[4]==0x0602 && values[4]==3 && values[5]==0xff);
 assert(addresses[12]==0x0600 && values[12]==0 && values[13]==2); // menu 1 -> register 2
 for(int i=0;i<14;i++) {reset_io();fail_at=i;assert(cockpit_imx219_apply_pattern(&sensor)==-EREMOTEIO);assert(writes==i+1);}
 reset_io();control.val=2;fail_at=3;
 assert(cockpit_imx219_s_ctrl(&control)==-EREMOTEIO);assert(sensor.test_pattern_mode==1);assert(sensor.needs_reset);
 reset_io();assert(cockpit_imx219_s_ctrl(&control)==-EIO);assert(writes==0);
 sensor.needs_reset=false;reset_io();control.val=0;
 assert(!cockpit_imx219_s_ctrl(&control));assert(writes==2 && values[0]==0 && values[1]==0);
 reset_io();data.mode=5;assert(cockpit_imx219_apply_pattern(&sensor)==-EINVAL);assert(writes==0);
 data.mode=2;control.val=5;assert(cockpit_imx219_s_ctrl(&control)==-EINVAL);
 struct v4l2_ctrl_handler handler={0};data.subdev.ctrl_handler=&handler;
 assert(!cockpit_imx219_registered(&data.subdev));assert(handler.count==5);
 for(int i=0;i<5;i++)assert(handler.controls[i].priv==&sensor);
 assert(!cockpit_imx219_registered(&data.subdev));assert(handler.count==5);
 puts("mocked I2C register mapping, first-error propagation, cache/rollback and V4L2 registration passed");
 return 0;
}
