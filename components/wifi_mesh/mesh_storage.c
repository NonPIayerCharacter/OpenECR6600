#include "mesh_storage.h"
#if defined (CONFIG_NV)
#include "system_config.h"

#define CUSTOMER_NV_WIFI_MESH_PARENT       CUSTOMER_NV_FLAG"MESHPARENT"
#define CUSTOMER_NV_WIFI_MESH_NETWORK_CFG  CUSTOMER_NV_FLAG"MESHNETWORKCFG"
#define CUSTOMER_NV_WIFI_MESH_CFG  CUSTOMER_NV_FLAG"MESHCFG"

int wifi_mesh_parent_node_write(char *data, unsigned int len)
{
    int result;

     result = hal_system_set_config(CUSTOMER_NV_WIFI_MESH_PARENT, data, len);
     return result >= 0? 0:-1;
}

int wifi_mesh_parent_node_read(char *data, unsigned int len)
{
     int result;

     result = hal_system_get_config(CUSTOMER_NV_WIFI_MESH_PARENT, data, len);
     return result > 0? 0:-1;
}

int wifi_mesh_parent_node_del(void)
{
    int result;

    result = hal_system_del_config(CUSTOMER_NV_WIFI_MESH_PARENT);
    return result > 0? 0:-1;
}

int wifi_mesh_network_cfg_write(char *data, unsigned int len)
{
    int result;

     result = hal_system_set_config(CUSTOMER_NV_WIFI_MESH_NETWORK_CFG, data, len);
     return result >= 0? 0:-1;
}

int wifi_mesh_network_cfg_read(char *data, unsigned int len)
{
     int result;

     result = hal_system_get_config(CUSTOMER_NV_WIFI_MESH_NETWORK_CFG, data, len);
     return result > 0? 0:-1;
}

int wifi_mesh_network_cfg_del(void)
{
    int result;

    result = hal_system_del_config(CUSTOMER_NV_WIFI_MESH_NETWORK_CFG);
    return result >= 0? 0:-1;
}

void wifi_mesh_reset_autoconn(void)
{
    unsigned char auto_conn = 0;
    hal_system_set_config(CUSTOMER_NV_WIFI_AUTO_CONN, &auto_conn, sizeof(auto_conn));
}

int wifi_mesh_cfg_write(char *data, unsigned int len)
{
    int result;

     result = hal_system_set_config(CUSTOMER_NV_WIFI_MESH_CFG, data, len);
     return result >= 0? 0:-1;
}

int wifi_mesh_cfg_read(char *data, unsigned int len)
{
     int result;

     result = hal_system_get_config(CUSTOMER_NV_WIFI_MESH_CFG, data, len);
     return result > 0? 0:-1;
}

int wifi_mesh_cfg_del(void)
{
    int result;

    result = hal_system_del_config(CUSTOMER_NV_WIFI_MESH_CFG);
    return result >= 0? 0:-1;
}

#else

#endif

