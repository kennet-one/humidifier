idf_component_register(SRCS "mesh_main.c"
                        "legacy_proto.c"                   
                        "stack_monitor.c"
                        "legacy_root_sender.c"
                        "relay_block_pca8574.c"
                        "humid_ctrl.c"
                        "water_led.c"
                        "pms5003_node.c"
                        "mesh_time_sync.c"
                        "log_time_vprintf.c"
                        "mesh_log_stream.c"
                    PRIV_REQUIRES esp_wifi esp_driver_gpio esp_adc driver led_strip nvs_flash
                    INCLUDE_DIRS "." "include")
