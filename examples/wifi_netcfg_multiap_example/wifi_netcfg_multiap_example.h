#ifndef WIFI_NETCFG_MULTIAP_EXAMPLE_H
#define WIFI_NETCFG_MULTIAP_EXAMPLE_H

void app_example_main(void);
int wifi_netcfg_multiap_example_start_auto_connect(void);
int wifi_netcfg_multiap_example_start_config_mode(void);
int wifi_netcfg_multiap_example_submit_ble_config(const char* ssid,
                                                  const char* password);
int wifi_netcfg_multiap_example_disconnect(void);
int wifi_netcfg_multiap_example_trigger_scan(void);
int wifi_netcfg_multiap_example_print_status(void);
int wifi_netcfg_multiap_example_print_storage(void);
int wifi_netcfg_multiap_example_add(const char* ssid, const char* password);
int wifi_netcfg_multiap_example_delete(const char* token);
int wifi_netcfg_multiap_example_clear(void);
int wifi_netcfg_multiap_example_connect(const char* token);
int wifi_netcfg_multiap_example_smartcfg_start(void);
int wifi_netcfg_multiap_example_smartcfg_stop(void);
int wifi_netcfg_multiap_example_smartcfg_print_status(void);
void wifi_netcfg_multiap_example_print_help(void);

#endif /* WIFI_NETCFG_MULTIAP_EXAMPLE_H */
