# SPDX-License-Identifier: Apache-2.0
idf_component_register(
    SRCS
        "test_app.c"
        "../../../main/humid_ctrl.c"
        "../../../main/legacy_proto.c"
    INCLUDE_DIRS
        "."
        "../../../main"
    REQUIRES
        unity
        freertos
        log
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_uart
)
