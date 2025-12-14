idf_component_register(SRCS "mesh_main.c"
                        "legacy_proto.c"                   
                        "stack_monitor.c"
                        "legacy_root_sender.c"
                        "relay_block_pca8574.c"
                        "humid_ctrl.c"
                    PRIV_REQUIRES esp_wifi esp_driver_gpio esp_adc driver nvs_flash
                    INCLUDE_DIRS "." "include")
