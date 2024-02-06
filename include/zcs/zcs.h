#ifndef __ZCS_H__
#define __ZCS_H__

#include "../multicast/multicast.h"
#include "../logs/log_manager.h"
#include "../messages/message_creation.h"
#include "zcs_structs.h"
#include "zcs_utils.h"
#include "local_registry.h"

#define ZCS_APP_TYPE 1
#define ZCS_SERVICE_TYPE 2

int zcs_init(int type);
int zcs_start(char *name, zcs_attribute_t attr[], int num);
int zcs_post_ad(char *ad_name, char *ad_value);
int zcs_query(char *attr_name, char *attr_value, char *node_names[],
              int namelen);
int zcs_get_attribs(char *name, zcs_attribute_t attr[], int *num);
int zcs_listen_ad(char *name, zcs_cb_f cback);
int zcs_shutdown();
void zcs_log();

#endif
